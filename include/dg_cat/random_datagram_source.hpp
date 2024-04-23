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

#include <boost/endian/conversion.hpp>
#include <boost/log/trivial.hpp>

//#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <cassert>
#include <random>

#include <arpa/inet.h>
//#include <sys/uio.h>
//#include <unistd.h>
//#include <fcntl.h>
#include <time.h>

/**
 * @brief Datagram source that generates random datagrams.
 */
class RandomDatagramSource : public DatagramSource {
private:
    std::mutex _mutex;
    std::condition_variable _cv;
    const DgCatConfig& _config;
    std::string _path;
    size_t _n_to_generate = 0;  // if 0, generate forever
    size_t _min_size = 0;
    size_t _max_size = 1472;  // Default max size fits in a single UDP packet with 1500 MTU
    uint32_t _seed = 0;       // 0 means use random seed
    bool _force_eof = false;
    bool _closed = false;
    std::vector<char> _buffer;
    std::unique_ptr<std::mt19937> _gen;
    std::unique_ptr<std::uniform_int_distribution<size_t>> _size_dist;
    std::uniform_int_distribution<char> _hex_dig_dist;

public:
    RandomDatagramSource(const DgCatConfig& config, const std::string& path) :
        _config(config),
        _path(path),
        _buffer(config.bufsize),
        _hex_dig_dist(0, 15)
    {
        auto argstr = _path;

        if (argstr.compare(0, 9, "random://") == 0) {
            argstr.erase(0, 9);
        }
        if (argstr.compare(0, 1, "?") == 0) {
            argstr.erase(0, 1);
        }
        while (!argstr.empty()) {
            size_t ampersand_pos = argstr.find('&');
            std::string key_val;
            if (ampersand_pos == std::string::npos) {
                key_val = argstr;
                argstr.clear();
            } else {
                key_val = argstr.substr(0, ampersand_pos);
                argstr.erase(0, ampersand_pos + 1);
            }
            size_t eq_pos = key_val.find('=');
            if (eq_pos == std::string::npos) {
                throw std::runtime_error(std::string("Invalid argument to random:// (missing '='): ") + key_val);
            }
            std::string key = key_val.substr(0, eq_pos);
            std::string val_s = key_val.substr(eq_pos + 1);
            if (key == "n") {
                _n_to_generate = std::stoul(val_s);
            } else if (key == "min_size") {
                _min_size = std::stoul(val_s);
            } else if (key == "max_size") {
                _max_size = std::stoul(val_s);
            } else if (key == "seed") {
                _seed = std::stoul(val_s);
            } else {
                throw std::runtime_error("Invalid argument: " + key);
            }
        }

        if (_seed == 0) {
            std::random_device rd;
            _seed = rd();
        }

        _gen = std::make_unique<std::mt19937>(_seed);
        _size_dist = std::make_unique<std::uniform_int_distribution<size_t>>(_min_size, _max_size);
    }

    char random_hex_digit() {
        char dig = _hex_dig_dist(*_gen);
        if (dig < 10) {
            return '0' + dig;
        } else {
            return 'a' + dig - 10;
        }
    }

    size_t random_size() {
        return (*_size_dist)(*_gen);
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
        return std::make_unique<RandomDatagramSource>(config, path);
    }


    ~RandomDatagramSource() override
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
            struct mmsghdr _msg{0};
            struct iovec _iov{0};
            uint64_t n_datagrams = 0;
            struct timespec end_time;
            struct timespec start_time;
            time_t start_clock_time = 0;
            struct timespec *current_timeout = nullptr;
            while (true) {
                if (_n_to_generate != 0 && n_datagrams >= _n_to_generate) {
                    BOOST_LOG_TRIVIAL(debug) << "Generated " << n_datagrams << " datagrams; stopping generation\n";
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_force_eof) {
                        BOOST_LOG_TRIVIAL(debug) << "Forced EOF; stopping generation\n";
                        break;
                    }
                }
                auto dg_size = random_size();
                BOOST_LOG_TRIVIAL(debug) << "Generating datagram of size " << dg_size << "\n";

                if (_buffer.size() < dg_size) {
                    _buffer.resize(dg_size);
                }

                for (size_t i = 0; i < dg_size; ++i) {
                    _buffer[i] = random_hex_digit();
                }

                _iov.iov_base = _buffer.data();
                _iov.iov_len = dg_size;
                _msg.msg_hdr.msg_iov = &_iov;
                _msg.msg_hdr.msg_iovlen = 1;
                _msg.msg_len = dg_size;

                clock_gettime(CLOCK_REALTIME, &end_time);
                if (n_datagrams == 0) {
                    start_time = end_time;
                    start_clock_time = time(nullptr);

                    BOOST_LOG_TRIVIAL(debug) << "First datagram generated...\n";
                }

                buffer_queue.producer_commit_batch(&_msg, 1);
                n_datagrams += 1;

                {
                    // update stats here
                    std::lock_guard<std::mutex> lock(stats._mutex);
                    stats.max_clump_size = 1;
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
    }

    void close() {
        force_eof();
    }
};

