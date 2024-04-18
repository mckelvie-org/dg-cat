#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <boost/log/trivial.hpp>

#include <unistd.h>
#include <fcntl.h>

#include "addrinfo.hpp"
#include "buffer_queue.hpp"
#include "datagram_destination.hpp"
#include "config.hpp"
#include "stats.hpp"
#include "object_closer.hpp"

/**
 * @brief Datagram Destination that writes to a file.
 */
class UdpDatagramDestination : public DatagramDestination {
private:
    std::mutex _mutex;
    const DgCatConfig& _config;
    std::string _path;
    int _sock = -1;
    bool _closed = false;

public:
    UdpDatagramDestination(const DgCatConfig& config, const std::string& path) :
        _config(config),
        _path(path)
    {
        ObjectCloser sock_closer(this);
        auto host_and_port = _path;
        if (host_and_port.compare(0, 6, "udp://") == 0) {
            host_and_port.erase(0, 6);
        }

        std::string addr_s;
        uint64_t port;
        size_t colon_pos = host_and_port.rfind(':');
        if (colon_pos == std::string::npos) {
            throw std::runtime_error("Invalid UDP destination address format: " + path);
        } else {
            addr_s = host_and_port.substr(0, colon_pos);
            port = std::stoi(host_and_port.substr(colon_pos + 1));
        }

        AddrInfoList addrinfo_list(
            addr_s.c_str(),
            std::to_string(port).c_str(),
            AI_PASSIVE,
            AF_UNSPEC,
            SOCK_DGRAM
        );

        if (addrinfo_list.size() == 0) {
            throw std::runtime_error(std::string("Unable to resolve host address for ") + path + ":" + std::to_string(port));
        }

        for(auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
            auto& entry = *ai;
            BOOST_LOG_TRIVIAL(debug) << "Addr=" << entry.addr_string() << " (" << entry->ai_addr << ") Family=" << entry->ai_family << " SockType=" << entry->ai_socktype << " Protocol=" << entry->ai_protocol << "\n";
        }

        AddrInfoList::Entry matching_entry;
        for (auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
            auto& entry = *ai;
            _sock = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
            if (_sock == -1) {
                continue;
            }

            if (connect(_sock, entry->ai_addr, entry->ai_addrlen) == 0) {
                matching_entry = entry;
                break;
            }

            ::close(_sock);
            _sock = -1;
        }

        if (_sock == -1) {
            throw std::runtime_error("Could not connect socket to any resolved addresses");
        }

        BOOST_LOG_TRIVIAL(debug) << "Bound to " << matching_entry.addr_string() << ":" << port << "\n";

        sock_closer.detach();
    }

    ~UdpDatagramDestination() override {
        close();
    }

    /**
     * @brief Copy datagrams from the BufferQueue until an EOF is encountered.
     * 
     * On exit the file handle will be closed, even on exception.
     * 
     * @param buffer_queue The buffer queue to read datagrams from.
     * @param stats        The threadsafe stats object to update with real-time progress.
     */
    void copy_from_buffer_queue(BufferQueue& buffer_queue, LockableDgDestinationStats& stats) override {
        ObjectCloser fd_closer(this);  // close the file descriptor before returning
        auto next_send_time = std::chrono::steady_clock::now();
        double send_interval_secs = (_config.max_datagram_rate <= 0.0) ? 0.0 : (1.0 / _config.max_datagram_rate);
        auto send_interval = std::chrono::nanoseconds(static_cast<int64_t>(send_interval_secs * 1e9));

        struct msghdr msg{0};
        size_t n_min = PREFIX_LEN;
        bool done = false;
        while (!done) {
            BufferQueue::ConsumerBatch batch = buffer_queue.consumer_start_batch(n_min);
            if (batch.n < n_min) {
                if (batch.n != 0) {
                    BOOST_LOG_TRIVIAL(error) << "Unexpected EOF with partial datagram";
                }
                done = true;
                break;
            }

            uint32_t nbo_prefix;
            batch.copy_and_remove_bytes(&nbo_prefix, PREFIX_LEN);
            size_t nb_datagram = ntohl(nbo_prefix);
            if (batch.n < nb_datagram) {
                n_min = nb_datagram + PREFIX_LEN;
                continue;
            }

            msg.msg_iov = batch.iov;
            msg.msg_iovlen = batch.n_iov;

            if (send_interval_secs != 0.0) {
                while (std::chrono::steady_clock::now() < next_send_time) {
                    std::this_thread::sleep_until(next_send_time);
                }
            }
            ssize_t ret = sendmsg(_sock, &msg, 0);
            if (ret < 0) {
                throw std::system_error(errno, std::system_category(), "sendmsg() failed");
            }
            buffer_queue.consumer_commit_batch(batch.n + PREFIX_LEN);
            if (send_interval_secs != 0.0) {
                next_send_time += send_interval;
            }

            {
                // update stats here
                // std::lock_guard<std::mutex> lock(stats._mutex);
            }
        }
    }

    /**
     * @brief Close the socket.
     */
    void close() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_closed) {
            _closed = true;
            if (_sock != -1) {
                ::close(_sock);
                _sock = -1;
            }
        }
    }

    /**
     * @brief static factory method to create a typed DatagramDestination based on a pathname
     * 
     * @param config   The configuration object
     * @param path     The path to the destination
     * 
     * @return unique_ptr<DatagramDestination>
     */
    static std::unique_ptr<DatagramDestination> create(const DgCatConfig& config, const std::string& path) {
        return std::make_unique<UdpDatagramDestination>(config, path);
    }
};
