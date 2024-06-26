/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <boost/log/trivial.hpp>

#include <signal.h>

#include "config.hpp"
#include "buffer_queue.hpp"
#include "stats.hpp"
#include "datagram_source.hpp"
#include "datagram_destination.hpp"

/**
 * @brief An object that can copy datagrams between an abstract source an an abstract destination.
 */
class DatagramCopier {
private:
    std::mutex _mutex;
    std::condition_variable _cond;
    const DgCatConfig& _config;
    LockableDgCatStats _stats;
    BufferQueue _buffer_queue;
    std::unique_ptr<DatagramSource> _source;
    std::unique_ptr<DatagramDestination> _destination;
    std::atomic<uint64_t> _stat_seq;
    std::thread _source_thread;
    std::thread _destination_thread;
    std::thread _signal_thread;
    bool _signal_thread_waiting = false;

    /**
     * @brief An exception that was raised in a worker thread. This will be rethrown in the main thread when
     *        wait() is called.
     */
    std::exception_ptr _exception;

public:
    /**
     * @brief Construct a DatagramCopier object from abstract source and destination
     * 
     * @param config The configuration object
     * @param source The datagram source
     * @param destination The datagram destination
     */
    DatagramCopier(
                const DgCatConfig& config,
                std::unique_ptr<DatagramSource>&& source,
                std::unique_ptr<DatagramDestination>&& destination
            ) :
        _config(config),
        _buffer_queue(config, _stats.buffer_stats),
        _source(std::move(source)),
        _destination(std::move(destination))
    {
    }

    /**
     * @brief Construct a DatagramCopier object from source and destination paths
     * 
     * @param config The configuration object
     * @param source The datagram source path
     * @param destination The datagram destination path
     */
    DatagramCopier(
                const DgCatConfig& config,
                const std::string& source,
                const std::string& destination
            ) :
        _config(config),
        _buffer_queue(config, _stats.buffer_stats),
        _source(DatagramSource::create(config, source)),
        _destination(DatagramDestination::create(config, destination))
    {
    }

    ~DatagramCopier()
    {
        close();
    }

    /**
     * @brief Get real-time progress statistics. Thread-safe.
     * 
     * @return DgCatStats The current statistics. The stat_seq field will be incremented for each call to get_stats().
     */
    DgCatStats get_stats()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto seq = _stat_seq++;
        return _stats.get(seq);
    }

    /**
     * @brief Copy datagrams from the source until an EOF is encountered or force_eof() is called.
     * 
     * @param buffer_queue The buffer queue to write datagrams to.
     * @param stats        The threadsafe stats object to update with real-time progress.
     */
    void start() {
        if (_config.handle_signals) {
            mask_signals();
            _signal_thread = std::thread([&] {
                handle_signals();
            });
        }
        _destination_thread = std::thread([&] {
            try {
                if (_config.handle_signals) {
                    mask_signals();
                }
                _destination->copy_from_buffer_queue(_buffer_queue, _stats.destination_stats);
            } catch (...) {
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (!_exception) {
                        _exception = std::current_exception();
                    }
                }
            }
            _source->force_eof();
        });

        try {
            _source_thread = std::thread([&] {
                try {
                    if (_config.handle_signals) {
                        mask_signals();
                    }
                    _source->copy_to_buffer_queue(_buffer_queue, _stats.source_stats);
                } catch (...) {
                    {
                        std::lock_guard<std::mutex> lock(_mutex);
                        if (!_exception) {
                            _exception = std::current_exception();
                        }
                    }
                }
                _buffer_queue.producer_set_eof();
            });
        } catch (...) {
            _buffer_queue.producer_set_eof();
            throw;
        }

    }

    void wait() {
        if (_source_thread.joinable()) {
            _source_thread.join();
        }
        if (_destination_thread.joinable()) {
            _destination_thread.join();
        }
        if (_signal_thread.joinable()) {
            auto retry_timer = std::chrono::seconds(1);
            while (true) {
                ::pthread_kill(_signal_thread.native_handle(), SIGUSR1);
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    if (!_signal_thread_waiting) {
                        break;
                    }
                    _cond.wait_for(lock, retry_timer);
                    if (!_signal_thread_waiting) {
                        break;
                    }
                }
            }
            _signal_thread.join();
        }
        if (_exception) {
            std::rethrow_exception(_exception);
        }
    }

    /**
     * @brief Force an EOF condition on the source as soon as possible. This method will be called
     *        when an asynchronous signal is received to terminate the program cleanly.
     */
    void force_eof() {
        _source->force_eof();
    }

    void close() {
        force_eof();
        wait();
    }

protected:
    void mask_signals() {
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGUSR1);
        pthread_sigmask(SIG_BLOCK, &sigset, nullptr);
    }

    void handle_signals() {
        BOOST_LOG_TRIVIAL(debug) << "Signal thread started";

        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGUSR1);
        int n_sigint = 0;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _signal_thread_waiting = true;
        }

        while (true) {
            int sig = 0;
            if (_buffer_queue.is_eof()) {
                BOOST_LOG_TRIVIAL(debug) << "EOF detected; exiting signal thread";
                break;
            }
            BOOST_LOG_TRIVIAL(debug) << "Waiting for signal...";
            int ret = sigwait(&sigset, &sig);
            if (ret != 0) {
                std::cerr << "sigwait() failed: " << strerror(errno) << std::endl;
                abort();
            }
            BOOST_LOG_TRIVIAL(debug) << "Received signal: " << sig << std::endl;
            if (_buffer_queue.is_eof()) {
                BOOST_LOG_TRIVIAL(debug) << "EOF detected; exiting signal thread";
                break;
            }
            switch (sig) {
                case SIGINT:
                    ++n_sigint;
                    if (n_sigint >= 2) {
                        std::cerr << "Received SIGINT twice; aborting" << std::endl;
                        exit(1);
                    }
                    BOOST_LOG_TRIVIAL(info) << "Forcing EOF due to SIGINT\n";
                    force_eof();
                    BOOST_LOG_TRIVIAL(debug) << "Done forcing EOF\n";
                    break;
                case SIGUSR1:
                    BOOST_LOG_TRIVIAL(debug) << "Dumping stats due to SIGUSR1\n";
                    std::cerr << get_stats().brief_str() << std::endl;
                    BOOST_LOG_TRIVIAL(debug) << "Done dumping stats\n";
                    break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _signal_thread_waiting = false;
        }
        _cond.notify_all();
        BOOST_LOG_TRIVIAL(debug) << "Signal thread shutting down";
    }
};

