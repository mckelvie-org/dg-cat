/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include <memory>
#include <mutex>

#include <unistd.h>
#include <fcntl.h>

#include "buffer_queue.hpp"
#include "config.hpp"
#include "stats.hpp"
#include "object_closer.hpp"

/**
 * @brief Datagram Destination that writes to a file.
 */
class FileDatagramDestination : public DatagramDestination {
private:
    std::mutex _mutex;
    const DgCatConfig& _config;
    std::string _path;
    std::string _filename;
    int _fd;
    bool _closed = false;

public:
    FileDatagramDestination(const DgCatConfig& config, const std::string& path) :
        _config(config),
        _path(path),
        _fd(-1)
    {
        ObjectCloser fd_closer(this);
        if (_path == "-" || _path == "stdout") {
            _filename = "stdout";
            // duplicate the file descriptor for stdout so it can be closed without affecting the original
            _fd = dup(STDOUT_FILENO);
        } else {
            _filename = _path;
            if (_filename.compare(0, 7, "file://") == 0) {
                _filename.erase(0, 7);
            }
            int oflags;
            if (_config.append) {
                oflags = O_WRONLY | O_CREAT | O_APPEND;
            } else {
                oflags = O_WRONLY | O_CREAT | O_TRUNC;
            }
            _fd = ::open(_filename.c_str(), oflags, 0666);
            if (_fd == -1) {
                throw std::runtime_error("Failed to open file: " + path + ": " + strerror(errno));
            }
        }
        fd_closer.detach();
    }

    ~FileDatagramDestination() override {
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
        bool done = false;
        while (!done) {
            BufferQueue::ConsumerBatch batch = buffer_queue.consumer_start_batch(1, _config.max_write_size);
            const struct iovec *iov = batch.iov;
            size_t n_iovecs = batch.n_iov;
            if (n_iovecs == 0) {
                if (buffer_queue.is_eof()) {
                    done = true;
                }
                continue;
            }

            if (n_iovecs == 1) {
                ssize_t ret = write(_fd, iov[0].iov_base, iov[0].iov_len);
                if (ret < 0) {
                    throw std::system_error(errno, std::system_category(), "write() failed");
                }
            } else {
                ssize_t ret = writev(_fd, iov, n_iovecs);
                if (ret < 0) {
                    throw std::system_error(errno, std::system_category(), "writev() failed");
                }
            }
            buffer_queue.consumer_commit_batch(batch.n);


            {
                // update stats here
                // std::lock_guard<std::mutex> lock(stats._mutex);
            }
        }
        fsync(_fd);
    }

    /**
     * @brief Close the file descriptor.
     */
    void close() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_closed) {
            _closed = true;
            if (_fd != -1) {
                ::close(_fd);
                _fd = -1;
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
        return std::make_unique<FileDatagramDestination>(config, path);
    }
};
