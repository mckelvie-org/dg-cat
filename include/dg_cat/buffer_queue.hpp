#pragma once


#include "constants.hpp"
#include "stats.hpp"

#include <boost/endian/conversion.hpp>
#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cassert>

#include <sys/socket.h>
#include <sys/uio.h>

class BufferQueue {
    /**
     * @brief A class that manages a circular buffers of bytes that can be used to store data in a producer-consumer pattern.
     *        Used for thread-safe file buffering with background flushing.
     */
protected:
    std::mutex _mutex;
    std::condition_variable _cv;

private:

    size_t _max_n;            // Max number of bytes that can be in the queue at once

    std::vector<char> _data;  // Circular buffer of bytes

    size_t _producer_index;   // Index of the next byte to be filled by a producer
    size_t _consumer_index;   // Index of the next byte to be consumed by a consumer

protected:
    const DgCatConfig& _config;
    LockableDgBufferStats& _shared_stats;
    DgBufferStats _stats;
    size_t _n;                // Number of bytes in the queue
    bool _is_eof;

public:

    class ConsumerBatch {
        /**
         * @brief A class that represents a batch of bytes that can be consumed by a consumer.
         *        May contain 1 or 2 contiguous buffers, depending on whether the consumer needs to wrap around the end of the queue.
         */
    public:
        /* The 0, 1, or 2 contiguous buffers that can be consumed */
        struct iovec iov[2];

        /* The number of iov segments (0, 1, or 2) */
        size_t n_iov;

        /* The total number of bytes in both segments */
        size_t n;

        ConsumerBatch(const char *b1, size_t n1, const char *b2=nullptr, size_t n2=0)
        {
            if (n1 == 0) {
                n1 = n2;
                b1 = b2;
                n2 = 0;
            }
            iov[0].iov_base = (n1 == 0) ? nullptr : (void *)b1;
            iov[0].iov_len = n1;
            iov[1].iov_base = (n2 == 0) ? nullptr : (void *)b2;
            iov[1].iov_len = n2;
            n_iov = (n2 == 0) ? ((n1 == 0) ? 0 : 1) : 2;
            n = n1 + n2;
        }

        ConsumerBatch(const ConsumerBatch&) = default;
        ConsumerBatch& operator=(const ConsumerBatch&) = default;
    };

    BufferQueue(
        const DgCatConfig& config,
        LockableDgBufferStats& stats
    ) :
        _max_n(config.max_backlog),
        _producer_index(0),
        _consumer_index(0),
        _config(config),
        _shared_stats(stats),
        _n(0),
        _is_eof(false)
    {
        _data.resize(_max_n);
    }

    /**
     * @brief Allows the producer to set the eof flag, indicating that no more data will be written to the queue.
     *       This will cause the queue to be drained by the consumer (consumer will never block waiting for more data).
     *       The consumer will still be able to read data from the queue until it is empty. If the consumer reads
     *       0 buffers, it should check for eof with is_eof() and stop reading if true.
     *       The producer will not be able to write any more data to the queue after this is called.
     */
    void producer_set_eof() {
        std::lock_guard<std::mutex> lock(_mutex);
        _is_eof = true;
        _cv.notify_all();
    }

    /**
     * @brief Waits until at least n_min bytes are free to be filled by a producer.
     *        Returns the number of free bytes.
     * 
     * @param n_min
     *             The mininum number of bytes that must be available for filling by a producer before returning.
     *             Default is 1. If zero, this function will not wait. will be clamped to maximum buffer size.
     * @return size_t
     *             The number of bytes that can be written without blocking. Will be >= n_min.
     */
    size_t producer_reserve_bytes(size_t n_min=1) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_is_eof) {
             throw std::runtime_error("Producer attempted to write to BufferQueue after EOF");
        }
        n_min = std::min(n_min, _max_n);
        if (n_min > n_free_locked()) {
            _cv.wait(
                lock,
                [this, n_min]()
                    {
                        return _n + n_min <= _max_n;
                    }
            );
        }
        return n_free_locked();
    }

    /**
     * @brief Waits until at least n_min bytes are free to be filled by a producer, or a timeout occurs.
     *        Returns a contiguous iovec list of fillable buffers, and the maximum length of the list.
     *         pair of iovec pointers and the number of iovecs.
     * 
     * @param __atime
     *            The time point at which to stop waiting for buffers.
     * @param n_min
     *             The mininum number of bytes that must be available for filling by a producer before returning.
     *             Default is 1. If zero, this function will not wait. Will be clamped to maximun buffer size.
     * @return size_t
     *             The number of bytes that can be written without blocking. May return < n_min (including 0) if a timeout occurs.
     */
    template<typename _Clock, typename _Duration>
    size_t producer_reserve_bytes(const std::chrono::time_point<_Clock, _Duration>& __atime, size_t n_min=1) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_is_eof) {
             throw std::runtime_error("Producer attempted to write to BufferQueue after EOF");
        }
        n_min = std::min(n_min, _max_n);
        if (n_min > n_free_locked()) {
            _cv.wait_until(
                lock,
                __atime,
                [this, n_min]()
                    {
                        return _n + n_min <= _max_n;
                    }
            );
        }
        return n_free_locked();
    }

    inline size_t n_free() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _max_n - _n;
    }

    /**
     * @brief Commits buffers filled by the producer as indicated by recvmmsg() results. Will block
     *        if necessary to complete the write.
     *
     * @param mmsg_hdrs     Array of mmsghdr structures returned by recvmmsg() that indicate the datagrams received.
     *                      Entries that are ancillary data or truncated datagrams are replaced with 0-length iovecs.
     * @param n_buffers     Length of the mmsg_hdrs array as returned by recvmmsg().
     */
    void producer_commit_batch(const struct mmsghdr *mmsg_hdrs, size_t n_buffers) {
        if (n_buffers > 0) {

            std::unique_lock<std::mutex> lock(_mutex);
            if (_is_eof) {
                throw std::runtime_error("Producer attempted to write to BufferQueue after EOF");
            }

            bool need_notify = false;
            bool need_update_stats = false;
            for (size_t i = 0; i < n_buffers; ++i) {
                const struct mmsghdr& mmsg_hdr = mmsg_hdrs[i];
                const struct msghdr& msg_hdr = mmsg_hdr.msg_hdr;
                size_t dg_len = mmsg_hdr.msg_len;
                auto flags = msg_hdr.msg_flags;
                if (flags & (MSG_OOB | MSG_ERRQUEUE | MSG_TRUNC)) {
                    if (flags & (MSG_OOB | MSG_ERRQUEUE)) {
                        std::cerr << "   WARNING: ancillary data discarded, len=" << dg_len << " bytes, flags=" << std::hex << flags << std::dec << "\n";
                    } else {
                        std::cerr << "   WARNING: datagram truncated; discarding, len=" << dg_len << " bytes, flags=" << std::hex << flags << std::dec << "\n";
                    }
                    _stats.n_datagrams_discarded++;
                    need_update_stats = true;
                    continue;
                }
                if (_max_n < dg_len + PREFIX_LEN) {
                    throw std::runtime_error("Datagram + PREFIX too large for buffer: " + std::to_string(dg_len) + " + 4 bytes, max=" + std::to_string(_max_n) + " bytes");
                }
                if (n_free_locked() < dg_len + PREFIX_LEN) {
                    if (need_update_stats) {
                         _shared_stats = _stats;
                        need_update_stats = false;
                    }
                    if (need_notify) {
                        _cv.notify_all();
                        need_notify = false;
                    }
                    _cv.wait(
                        lock,
                        [this, dg_len]()
                            {
                                return _n + dg_len + PREFIX_LEN <= _max_n;
                            }
                    );
                    assert (_n + dg_len + PREFIX_LEN <= _max_n);
                }
                const char *dg_data = (const char *)msg_hdr.msg_iov->iov_base;
                uint32_t len_network_byte_order = boost::endian::native_to_big((uint32_t)dg_len);
                need_notify = true;
                put_data_locked_no_notify((const char *)&len_network_byte_order, PREFIX_LEN);
                put_data_locked_no_notify(dg_data, dg_len);
                _stats.max_datagram_size = std::max(_stats.max_datagram_size, dg_len);
                _stats.min_datagram_size = (_stats.n_datagrams == 0) ? dg_len : std::min(_stats.min_datagram_size, dg_len);
                if (_stats.n_datagrams == 0) {
                    _stats.first_datagram_size = dg_len;
                }
                _stats.n_datagrams++;
                _stats.n_datagram_bytes += dg_len;
                need_update_stats = true;
            }
            if (need_update_stats) {
                 _shared_stats = _stats;
            }
            if (need_notify) {
                _cv.notify_all();
            }
        }
    }

    /**
     * @brief Commits buffers filled by the producer as indicated by recvmmsg() results, with timeout. Will block
     *        if necessary to complete the write. Returns early on timeout
     *
     * @param mmsg_hdrs     Array of mmsghdr structures returned by recvmmsg() that indicate the datagrams received.
     *                      Entries that are ancillary data or truncated datagrams are replaced with 0-length iovecs.
     * @param n_buffers     Length of the mmsg_hdrs array as returned by recvmmsg().
     * @param __atime       The time point at which to stop waiting for buffers. If the timeout occurs, the function will return early.
     * 
     * @return size_t       The number of buffers successfully committed. May be less than n_buffers if a timeout occurs.
     */
    template<typename _Clock, typename _Duration>
    size_t producer_commit_batch(const struct mmsghdr *mmsg_hdrs, size_t n_buffers, const std::chrono::time_point<_Clock, _Duration>& __atime) {
        size_t n_buffers_committed = 0;
        if (n_buffers > 0) {

            std::unique_lock<std::mutex> lock(_mutex);
            if (_is_eof) {
                throw std::runtime_error("Producer attempted to write to BufferQueue after EOF");
            }

            bool need_notify = false;
            bool need_update_stats = false;
            for (size_t i = 0; i < n_buffers; ++i) {
                const struct mmsghdr& mmsg_hdr = mmsg_hdrs[i];
                const struct msghdr& msg_hdr = mmsg_hdr.msg_hdr;
                size_t dg_len = mmsg_hdr.msg_len;
                auto flags = msg_hdr.msg_flags;
                if (flags & (MSG_OOB | MSG_ERRQUEUE | MSG_TRUNC)) {
                    if (flags & (MSG_OOB | MSG_ERRQUEUE)) {
                        std::cerr << "   WARNING: ancillary data discarded, len=" << dg_len << " bytes, flags=" << std::hex << flags << std::dec << "\n";
                    } else {
                        std::cerr << "   WARNING: datagram truncated; discarding, len=" << dg_len << " bytes, flags=" << std::hex << flags << std::dec << "\n";
                    }
                    _stats.n_datagrams_discarded++;
                    need_update_stats = true;
                    n_buffers_committed++;
                    continue;
                }
                if (_max_n < dg_len + PREFIX_LEN) {
                    throw std::runtime_error("Datagram + PREFIX too large for buffer: " + std::to_string(dg_len) + " + 4 bytes, max=" + std::to_string(_max_n) + " bytes");
                }
                if (n_free_locked() < dg_len + PREFIX_LEN) {
                    if (need_update_stats) {
                         _shared_stats = _stats;
                        need_update_stats = false;
                    }
                    if (need_notify) {
                        _cv.notify_all();
                        need_notify = false;
                    }
                    _cv.wait_until(
                        lock,
                        __atime,
                        [this, dg_len]()
                            {
                                return _n + dg_len + PREFIX_LEN <= _max_n;
                            }
                    );
                    if (_n + dg_len + PREFIX_LEN > _max_n) {
                        break;
                    }
                }
                const char *dg_data = (const char *)msg_hdr.msg_iov->iov_base;
                uint32_t len_network_byte_order = boost::endian::native_to_big((uint32_t)dg_len);
                need_notify = true;
                put_data_locked_no_notify((const char *)&len_network_byte_order, PREFIX_LEN);
                put_data_locked_no_notify(dg_data, dg_len);
                n_buffers_committed++;
                _stats.max_datagram_size = std::max(_stats.max_datagram_size, dg_len);
                _stats.min_datagram_size = (_stats.n_datagrams == 0) ? dg_len : std::min(_stats.min_datagram_size, dg_len);
                if (_stats.n_datagrams == 0) {
                    _stats.first_datagram_size = dg_len;
                }
                _stats.n_datagrams++;
                _stats.n_datagram_bytes += dg_len;
                need_update_stats = true;
            }
            if (need_update_stats) {
                    _shared_stats = _stats;
                need_update_stats = false;
            }
            if (need_notify) {
                _cv.notify_all();
            }
        }

        return n_buffers_committed;
    }


    /**
      * @brief Wait until at least n_min bytes are available for consumption by a consumer, or eof is set.
     *       Returns a ConsumerBatch, which holds a 1- or 2-part iovec.
     * 
     * @param n_min   The minimum number of buffers that must be available for consumption by a consumer before returning.
     * @return ConsumerBatch 
     *      A 1- or 2-par iovec list that can be consumed in order, with a total length >= n_min.
     *      On eof, the total length may be less than n_min (even 0).
     */
    ConsumerBatch consumer_start_batch(size_t n_min=1, size_t n_max=SIZE_MAX) {
        if (n_min > _max_n) {
            throw std::runtime_error("Consumer requested too many bytes: " + std::to_string(n_min) + " bytes, max=" + std::to_string(_max_n) + " bytes");
        }
        std::unique_lock<std::mutex> lock(_mutex);
        if (_n < n_min) {
            _cv.wait(
                lock,
                [this, n_min]()
                    {
                        return _is_eof || _n >= n_min;
                    }
            );
        }
        return get_data_locked(n_max);
    }

    /**
     * @brief Wait until at least n_min bytes are available for consumption by a consumer, eof is set, or a timeout occurs.
     *       Returns a ConsumerBatch, which holds a 1- or 2-part iovec.
     * 
     * @param __atime         The time point at which to stop waiting for buffers.
     * @param n_min   The minimum number of buffers that must be available for consumption by a consumer before returning.
     * @return ConsumerBatch 
     *      A 1- or 2-par iovec list that can be consumed in order, with a total length >= n_min.
     *      On timeout or eof, the total length may be less than n_min (even 0).
     */
    template<typename _Clock, typename _Duration>
    ConsumerBatch consumer_start_batch(const std::chrono::time_point<_Clock, _Duration>& __atime, size_t n_min=1, size_t n_max=SIZE_MAX) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_n < n_min) {
            _cv.wait_until(
                lock,
                __atime,
                [this, n_min]()
                    {
                        return _is_eof || _n >= n_min;
                    }
            );
        }
        return get_data_locked(n_max);
    }

    /**
     * @brief Frees bytes previously provided with consumer_start_batch() and consumed by the consumer.
     * 
     * @param n   The number of bytes to be consumed. Must be less than or equal to the total number of bytes returned by consumer_start_batch().
     */
    void consumer_commit_batch(size_t n) {
        if (n > 0) {
            std::lock_guard<std::mutex> lock(_mutex);
            if (n > _n) {
                throw std::runtime_error("Consumer freed too many bytes: " + std::to_string(n) + " bytes, " + std::to_string(_n) + " bytes available");
            }
            _consumer_index = (_consumer_index + n) % _max_n;
            _n -= n;
            _cv.notify_all();
        }
    }

    bool is_eof() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _is_eof;
    }

protected:
    inline ConsumerBatch get_data_locked(size_t n_max=SIZE_MAX) {
        size_t n = std::min(_n, n_max);
        if (n == 0) {
            return ConsumerBatch(nullptr, 0);
        }
        const char *b1 = &_data[_consumer_index];
        size_t n1 = std::min(_n, _max_n - _consumer_index);
        if (n1 >= n) {
            return ConsumerBatch(b1, n);
        }
        size_t n2 = n - n1;
        if (n2 == 0) {
            return ConsumerBatch(b1, n1);
        }
        const char *b2 = &_data[0];
        return ConsumerBatch(b1, n1, b2, n2);
    }


    inline void put_data_locked_no_notify(const char *data, size_t n) {
        if (n > 0) {
            assert (n_free_locked() >= n);
            assert (_producer_index < _max_n);
            auto n_rem = n;
            size_t n1 = std::min(n_rem, _max_n - _producer_index);
            memcpy(&_data[_producer_index], data, n1);
            _producer_index = (_producer_index + n1) % _max_n;
            n_rem -= n1;
            if (n_rem > 0) {
                memcpy(&_data[_producer_index], data + n1, n_rem);
                _producer_index = (_producer_index + n_rem) % _max_n;
            }
            _n += n;
        }
    }

    inline size_t n_free_locked() {
        return _max_n - _n;
    }
};

