#pragma once


// NOTE: This implementation does not use boost::asio, because it uses recvmmsg() which is not supported by boost::asio.
//       Also, to simplify distribution, we avoid any boost dependencies that require linking to boost libraries. The resulting executable is
//       self-contained and does not require any shared libraries other than libstdc++.

#include "constants.hpp"
#include "buffer_queue.hpp"

#include <algorithm>

// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netdb.h>

// //#include <boost/asio.hpp>
// #include <boost/endian/conversion.hpp>
// #include <fstream>
// #include <iostream>
// #include <vector>
// #include <array>
#include <thread>
// #include <mutex>
// #include <condition_variable>
// #include <exception>
// #include <time.h>
// #include <csignal>
// #include <memory>
// #include <chrono>
// #include <sys/uio.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <cassert>
// #include <cmath>

class NoBlockFileWriter: public BufferQueue {
    /**
     * @brief A class that writes data to a file in a separate thread and provides large buffering so that
     *        the writer will likely never block.
     */
private:
    int _file;
    std::thread _thread;

    std::exception_ptr _exception;

public:
    NoBlockFileWriter(
        int file,
        size_t max_datagram_size = DEFAULT_MAX_DATAGRAM_SIZE,
        size_t max_buffer_bytes = DEFAULT_MAX_BACKLOG,
        size_t max_write_size = DEFAULT_MAX_WRITE_SIZE
    ) :
        BufferQueue(std::max(max_buffer_bytes, max_datagram_size + PREFIX_LEN)),
        _file(file)
    {
        _thread = std::thread(
            [this, max_write_size]()
                {
                    try {
                        bool done = false;
                        while (!done) {
                            BufferQueue::ConsumerBatch batch = consumer_start_batch(1, max_write_size);
                            const struct iovec *iov = batch.iov;
                            size_t n_iovecs = batch.n_iov;
                            if (n_iovecs == 0) {
                                if (is_eof()) {
                                    done = true;
                                }
                                continue;
                            }

                            if (n_iovecs == 1) {
                                ssize_t ret = write(_file, iov[0].iov_base, iov[0].iov_len);
                                if (ret < 0) {
                                    throw std::system_error(errno, std::system_category(), "write() failed");
                                }
                            } else {
                                ssize_t ret = writev(_file, iov, n_iovecs);
                                if (ret < 0) {
                                    throw std::system_error(errno, std::system_category(), "writev() failed");
                                }
                            }
                            consumer_commit_batch(batch.n);
                        }
                        fsync(_file);
                    } catch (...) {
                        {
                            std::lock_guard<std::mutex> lock(_mutex);
                            _exception = std::current_exception();
                        }
                        _cv.notify_all();
                    }
                }
        );
    }

    void close() {
         producer_set_eof();
        _thread.join();
        if (_exception) {
            std::rethrow_exception(_exception);
        }
   }

    ~NoBlockFileWriter() {
        close();
    }
};
