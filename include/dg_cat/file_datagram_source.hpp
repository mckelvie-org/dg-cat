#pragma once

#include "datagram_source.hpp"
#include "buffer_queue.hpp"
#include "config.hpp"
#include "timespec_math.hpp"

#include <boost/endian/conversion.hpp>
#include <boost/log/trivial.hpp>

#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <cassert>

#include <arpa/inet.h>
//#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/**
 * @brief Datagram source that reads from a file or pipe.
 */
class FileDatagramSource : public DatagramSource {
private:
    std::mutex _mutex;
    std::condition_variable _cv;
    const DgCatConfig& _config;
    std::string _path;
    std::string _filename;
    int _fd = -1;
    bool _force_eof = false;
    bool _closed = false;
    std::vector<char> _buffer;
    std::vector<struct mmsghdr> _msgs;
    std::vector<struct iovec> _iovs;

public:
    FileDatagramSource(const DgCatConfig& config, const std::string& path) :
        _config(config),
        _path(path),
        _buffer(config.max_read_size),
        _msgs(config.max_read_size / PREFIX_LEN),
        _iovs(config.max_read_size / PREFIX_LEN)
    {
        _filename = _path;

        if (_filename == "-" || _filename == "stdin") {
            _filename = "stdin";
            // duplicate the file descriptor for stdin so it can be closed without affecting the original
            _fd = dup(STDIN_FILENO);
        } else {
            if (_filename.compare(0, 7, "file://") == 0) {
                _filename.erase(0, 7);
            }
            _fd = ::open(_filename.c_str(), O_RDONLY);
        }
        if (_fd == -1) {
            throw std::runtime_error("Failed to open file: " + path + ": " + strerror(errno));
        }

        for (size_t i = 0; i < config.max_read_size / PREFIX_LEN; ++i) {
            auto& msg = _msgs[i];
            auto& iov = _iovs[i];
            msg.msg_hdr.msg_iov = &iov;
            msg.msg_hdr.msg_iovlen = 1;
        }
    }

    /**
     * @brief factory-invoked static method to create a FileDatagramSource
     * 
     * @param config   The configuration object
     * @param path     The path to the source
     * 
     * @return unique_ptr<DatagramSource>
     */
    static std::unique_ptr<DatagramSource> create(const DgCatConfig& config, const std::string& path) {
        return std::make_unique<FileDatagramSource>(config, path);
    }


    ~FileDatagramSource() override
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
            uint64_t n_datagrams = 0;
            struct timespec end_time;
            struct timespec start_time;
            time_t start_clock_time = 0;
            struct timespec *current_timeout = nullptr;
            size_t n_read = 0;
            size_t n_min = PREFIX_LEN;
            while (true) {
                if (_buffer.size() < n_min) {
                    _buffer.resize(n_min);
                }

                ssize_t nb1 = ::read(_fd, _buffer.data() + n_read, _buffer.size() - n_read);
                 if (nb1 < 0) {
                    if (errno == EBADF) {
                        // The file handle was rudely closed. This is expected if force_eof() was called.
                        std::lock_guard<std::mutex> lock(_mutex);
                        if (_force_eof) {
                            BOOST_LOG_TRIVIAL(debug) << "read() got closed file handle with _force_eof; generating EOF\n";
                            break;
                        }
                    }
                    if (errno == EINTR) {
                        BOOST_LOG_TRIVIAL(debug) << "Interrupted by signal; continuing\n";
                        continue;
                    }
                    throw std::system_error(errno, std::system_category(), "read() failed");
                }
                if (nb1 == 0) {
                    if (n_read != 0) {
                        BOOST_LOG_TRIVIAL(error) << "Unexpected EOF with partial datagram";
                    }
                    BOOST_LOG_TRIVIAL(debug) << "EOF; shutting down\n";
                    break;
                }
                n_read += nb1;
                if (n_read < n_min) {
                    continue;
                }

                size_t n_batch_datagrams = 0;
                size_t i_next_datagram = 0;
                while (i_next_datagram < n_read) {
                    uint32_t nbo_prefix;
                    memcpy(&nbo_prefix, _buffer.data() + i_next_datagram, PREFIX_LEN);
                    size_t nb_datagram = ntohl(nbo_prefix);
                    _iovs[n_batch_datagrams].iov_base = _buffer.data() + i_next_datagram + PREFIX_LEN;
                    _iovs[n_batch_datagrams].iov_len = nb_datagram;
                    _msgs[n_batch_datagrams].msg_len = nb_datagram;
                    if (i_next_datagram + PREFIX_LEN + nb_datagram > n_read) {
                        break;
                    }
                    i_next_datagram += PREFIX_LEN + nb_datagram;
                    ++n_batch_datagrams;
                }

                if (n_batch_datagrams == 0) {
                    n_min = _iovs[0].iov_len + PREFIX_LEN;
                    continue;
                }

                clock_gettime(CLOCK_REALTIME, &end_time);
                if (n_datagrams == 0) {
                    start_time = end_time;
                    start_clock_time = time(nullptr);

                    BOOST_LOG_TRIVIAL(debug) << "First datagram received...\n";
                }

                buffer_queue.producer_commit_batch(_msgs.data(), n_batch_datagrams);
                n_datagrams += n_batch_datagrams;

                if (i_next_datagram < n_read) {
                    // Move the remaining incomplete datagram to the front of the buffer
                    memmove(_buffer.data(), _buffer.data() + i_next_datagram, n_read - i_next_datagram);
                    n_read -= i_next_datagram;
                } else {
                    n_read = 0;
                }
                n_min = PREFIX_LEN;

                {
                    // update stats here
                    std::lock_guard<std::mutex> lock(stats._mutex);
                    stats.max_clump_size = std::max(stats.max_clump_size, n_batch_datagrams);
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

        // This will wake up the thread that is blocked on read(). It will see _force_eof and not
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
            if (_fd != -1) {
                ::close(_fd);
                _fd = -1;
                need_notify = true;
            }

        }
        if (need_notify) {
            _cv.notify_all();
        }
    }
};

