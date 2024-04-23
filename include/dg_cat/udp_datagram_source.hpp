/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include "datagram_source.hpp"
#include "buffer_queue.hpp"
#include "config.hpp"
#include "timespec_math.hpp"
#include "addrinfo.hpp"

#include <boost/endian/conversion.hpp>
#include <boost/log/trivial.hpp>

#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <cassert>

#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>

typedef int SockFd;

/**
 * @brief Datagram source that reads from a UDP socket.
 */
class UdpDatagramSource : public DatagramSource {
private:
    std::mutex _mutex;
    std::condition_variable _cv;
    const DgCatConfig& _config;
    std::string _path;
    SockFd _sock = -1;
    bool _force_eof = false;
    bool _closed = false;
    std::vector<std::vector<char>> _buffers;
    std::vector<struct mmsghdr> _msgs;
    std::vector<struct iovec> _iovs;

public:
    UdpDatagramSource(const DgCatConfig& config, const std::string& path) :
        _config(config),
        _path(path),
        _buffers(config.max_iovecs),
        _msgs(config.max_iovecs),
        _iovs(config.max_iovecs)
    {
        // Parse "udp://<local-bind-ip-addr>:<port" or "udp://<port>"
        auto addr_and_port = path;

        if (addr_and_port.compare(0, 6, "udp://") == 0) {
            addr_and_port.erase(0, 6);
        }
        std::string addr_s;
        uint64_t port;
        size_t colon_pos = addr_and_port.rfind(':');
        if (colon_pos == std::string::npos) {
            addr_s = std::string("0.0.0.0");
            port = std::stoi(addr_and_port);
        } else {
            addr_s = addr_and_port.substr(0, colon_pos);
            port = std::stoi(addr_and_port.substr(colon_pos + 1));
        }

        AddrInfoList addrinfo_list(
            addr_s.c_str(),
            std::to_string(port).c_str(),
            AI_PASSIVE,
            AF_UNSPEC,
            SOCK_DGRAM
        );

        if (addrinfo_list.size() == 0) {
            throw std::runtime_error(std::string("No addresses found for ") + path + ":" + std::to_string(port));
        }

        for(auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
            auto& entry = *ai;
            BOOST_LOG_TRIVIAL(debug) << "Addr=" << entry.addr_string() << " (" << entry->ai_addr << ") Family=" << entry->ai_family << " SockType=" << entry->ai_socktype << " Protocol=" << entry->ai_protocol << "\n";
        }

        SockFd s = -1;
        try {
            AddrInfoList::Entry matching_entry;
            for(auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
                auto& entry = *ai;
                s = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
                if (s == -1) {
                    continue;
                }

                if (bind(s, entry->ai_addr, entry->ai_addrlen) == 0) {
                    matching_entry = entry;
                    break;
                }

                ::close(s);
            }

            if (s == -1) {
                throw std::runtime_error("Could not bind socket to any addresses");
            }

            BOOST_LOG_TRIVIAL(debug) << "Bound to " << matching_entry.addr_string() << ":" << port << "\n";

            // Allocate reusable buffers for recvmmsg
            for (size_t i = 0; i < _config.max_iovecs; ++i) {
                auto& buffer = _buffers[i];
                auto& msg = _msgs[i];
                auto& iov = _iovs[i];
                buffer.resize(_config.bufsize);
                iov.iov_base = buffer.data();
                iov.iov_len = buffer.size();
                msg.msg_hdr.msg_iov = &iov;
                msg.msg_hdr.msg_iovlen = 1;
            }

            _sock = s;

        } catch (...) {
            if (s != -1) {
                ::close(s);
            }
            throw;
        }
    }

    /**
     * @brief factory-invoked static method to create a UdpDatagramSource
     * 
     * @param config   The configuration object
     * @param path     The path to the source
     * 
     * @return unique_ptr<DatagramSource>
     */
    static std::unique_ptr<DatagramSource> create(const DgCatConfig& config, const std::string& path) {
        return std::make_unique<UdpDatagramSource>(config, path);
    }


    ~UdpDatagramSource() override
    {
        close();
    }

    /**
     * @brief Copy datagrams from the source until an EOF is encountered or force_eof() is called.
     * 
     * @param buffer_queue The buffer queue to write datagrams to.
     */
    void copy_to_buffer_queue(BufferQueue& buffer_queue, LockableDgSourceStats& stats) override {
        {
            struct timespec first_dg_timespec{0};
            struct timespec dg_timespec{0};

            if (_config.start_timeout > 0.0) {
                first_dg_timespec = secs_to_timespec(_config.start_timeout);
                BOOST_LOG_TRIVIAL(debug) << "First datagram timeout: " << first_dg_timespec.tv_sec << " seconds, " << first_dg_timespec.tv_nsec << " nanoseconds\n";
            }

            if (_config.eof_timeout > 0.0) {
                dg_timespec = secs_to_timespec(_config.eof_timeout);
                BOOST_LOG_TRIVIAL(debug) << "Datagram timeout: " << dg_timespec.tv_sec << " seconds, " << dg_timespec.tv_nsec << " nanoseconds\n";
            }

            // Run recvmmsg forever
            uint64_t n_datagrams = 0;
            struct timespec end_time;
            struct timespec start_time;
            time_t start_clock_time = 0;
            struct timespec *current_timeout = nullptr;
            while (true) {
                auto old_n_datagrams = n_datagrams;
                auto timeout = (n_datagrams == 0) ? &first_dg_timespec : &dg_timespec;
                if (timeout != current_timeout) {
                    // BOOST_LOG_TRIVIAL(debug) << "Setting timeout to " << timeout->tv_sec << " seconds, " << timeout->tv_nsec << " nanoseconds\n";
                    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(*timeout));
                    current_timeout = timeout;
                }
                int n = recvmmsg(_sock, _msgs.data(), _msgs.size(), MSG_WAITFORONE, nullptr);
                if (n == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        BOOST_LOG_TRIVIAL(debug) << "Timeout waiting for datagram; generating EOF\n";
                        break;
                    }
                    if (errno == EBADF || errno == ENOTSOCK) {
                        std::lock_guard<std::mutex> lock(_mutex);
                        if (_force_eof) {
                            BOOST_LOG_TRIVIAL(debug) << "recvmmsg got closed socket handle with _force_eof; generating EOF\n";
                            break;
                        }
                    }
                    if (errno == EINTR) {
                        BOOST_LOG_TRIVIAL(debug) << "Interrupted by signal; continuing\n";
                        continue;
                    }
                    throw std::system_error(errno, std::system_category(), "recvmmsg() failed");
                }
                if (n == 0) {
                    BOOST_LOG_TRIVIAL(debug) << "Timeout waiting for datagram; shutting down\n";
                    break;
                }
                clock_gettime(CLOCK_REALTIME, &end_time);
                if (n_datagrams == 0) {
                    start_time = end_time;
                    start_clock_time = time(nullptr);

                    BOOST_LOG_TRIVIAL(debug) << "First datagram received...\n";
                }
                if (n > 1 && n == _config.max_iovecs) {
                    BOOST_LOG_TRIVIAL(debug) << "   WARNING: recvmmsg response full (" << n << " datagrams), possible packet loss)\n";
                }
                buffer_queue.producer_commit_batch(_msgs.data(), n);
                n_datagrams += n;
                {
                    std::lock_guard<std::mutex> lock(stats._mutex);
                    stats.max_clump_size = std::max(stats.max_clump_size, n_datagrams - old_n_datagrams);
                    stats.start_clock_time = start_clock_time;
                    stats.start_time = start_time;
                    stats.end_time = end_time;
                }
            }
        }
    }

    /**
     * @brief Force an EOF condition on the source. This method will be called from a different
     *        thread than copy_to_buffer_queue().
     */
    void force_eof() override {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _force_eof = true;
        }

        // This will wake up the thread that is blocked on recvmmsg(). It will see _force_eof and not
        // freak out about the handle being rudely closed.
        close();
    }

    void close() {
        bool need_notify = false;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_closed) {
                return;
            }
            _closed = true;
            if (_sock != -1) {
                ::close(_sock);
                _sock = -1;
                need_notify = true;
            }

        }
        if (need_notify) {
            _cv.notify_all();
        }
    }
};

