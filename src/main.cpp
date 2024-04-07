#include "udp_capture/udp_capture.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

//#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <time.h>
#include <csignal>
#include <memory>
#include <chrono>
#include <sys/uio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cassert>
#include <cmath>


// NOTE: This implementation does not use boost::asio, because it uses recvmmsg() which is not supported by boost::asio.
//       Also, to simplify distribution, we avoid any boost dependencies that require linking to boost libraries. The resulting executable is
//       self-contained and does not require any shared libraries other than libstdc++.

static const double DEFAULT_DATAGRAM_TIMEOUT_SECS = 60.0;
static const size_t DEFAULT_MAX_DATAGRAM_SIZE = 65535;                // 65535 is the maximum allowed by UDP
static const size_t DEFAULT_NUM_DATAGRAM_BUFFERS = 2048;              // Maximum number of datagrams that can be received in one go with recvmmsg().
                                                                      //   (Will be further restricted by the kernel's maximum iovec count.)
static const size_t DEFAULT_MAX_BACKLOG = 2UL*1024*1024*1024;         // Maximum file buffer size (2GB)
static const size_t DEFAULT_MAX_WRITE_SIZE = 256*1024;                // Maximum number of bytes to write in one system call
static const size_t PREFIX_LEN = sizeof(uint32_t);                    // Length of the network-byte-order datagram-length prefix used when writing output


/**
 * @brief Normalize a tv_sec, tv_nsec pair into a timespec such that tv_nsec is in the range [0, 999999999]
 * 
 * Per the timespec specification, tv_nsec should be positive, in the range 0 to 999,999,999. It is always
 * added to tv_sec (even when tv_sec is negative).
 * For this reason, if t is the time in secs, tv_sec is actually floor(t), and nsec is (t - floor(t)) * 1.0e9.
 * 
 * @param tv_sec   (long) time in seconds. Msy be negative.
 * @param tv_nsec  (long) time in nanoseconds to be added to tv_sec. May be negative or >= 1000000000.
 * 
 * @return timespec  Normalized timespec value with tv_nsec in the range [0, 999999999]
 */
inline static struct timespec normalize_timespec(long tv_sec, long tv_nsec) {
    if (tv_nsec >= 1000000000) {
        tv_sec += tv_nsec / 1000000000;
        tv_nsec = tv_nsec % 1000000000;
    } else if (tv_nsec < 0) {
        if (tv_nsec <= -1000000000) {
            tv_sec -= -tv_nsec / 1000000000;
            tv_nsec = -(-tv_nsec % 1000000000);
        }
        if (tv_nsec != 0) {
            tv_sec -= 1;
            tv_nsec += 1000000000;
        }
    }
    return { tv_sec, tv_nsec };
}

/**
 * @brief Subtract two timespec values
 * 
 * @param time1 Ending time
 * @param time0 Starting time
 * @return timespec  (time1 - time0)
 */
inline static struct timespec timespec_subtract(const struct timespec& time1, const struct timespec& time0) {
    return normalize_timespec(time1.tv_sec - time0.tv_sec, time1.tv_nsec - time0.tv_nsec);
}

/**
 * @brief Add two timespec values
 * 
 * @param time1 Ending time
 * @param time0 Starting time
 * @return timespec  (time1 + time0)
 */
inline static struct timespec timespec_add(const struct timespec& time1, const struct timespec& time2) {
    return normalize_timespec(time1.tv_sec + time2.tv_sec, time1.tv_nsec + time2.tv_nsec);
}

/**
 * @brief Convert a timespec to a double in seconds
 * 
 * @param ts 
 * @return double 
 */
inline double timespec_to_secs(const struct timespec& ts) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

/**
 * @brief Convert a double in seconds to a timespec
 */
inline struct timespec secs_to_timespec(double secs) {
    auto sec = (long)floor(secs);
    auto nsec = (long)round((secs - (double)sec) * 1.0e9);
    return normalize_timespec(sec, nsec);
}

/**
 * @brief Writes a 4-byte network-byte-ordER length prefix to a buffer
 * 
 * @param len     The length value to be written as a prefix
 * @param buffer  The buffer to which the prefix will be written
 */
inline static void write_length_prefix(size_t len, char *buffer) {
    if (len > 0xffffffff) {
        throw std::runtime_error("Length prefix too large for prefix header: " + std::to_string(len) + " bytes");
    }
    auto ui32_network_byte_order = boost::endian::native_to_big((uint32_t)len);
    memcpy(buffer, &ui32_network_byte_order, sizeof(ui32_network_byte_order));
}

/**
 * @brief Reads a 4-byte network-byte-order length prefix from a buffer
 * 
 * @param buffer  The buffer from which the prefix will be read
 * @return size_t The length value read from the prefix
 */
inline static size_t read_length_prefix(const char *buffer) {
    uint32_t ui32_network_byte_order;
    memcpy(&ui32_network_byte_order, buffer, sizeof(ui32_network_byte_order));
    return (size_t)boost::endian::big_to_native(ui32_network_byte_order);
}

/**
 * @brief Calculate the total buffer length of an array of iovec structures
 * 
 * @param iov       The array of iovec structures
 * @param n_iovecs  The number of iovec structures in the array
 * @return size_t   The total buffer length of the iovec array
 */
inline size_t total_iovec_len(const struct iovec *iov, size_t n_iovecs) {
    size_t len = 0;
    for (size_t i = 0; i < n_iovecs; ++i) {
        len += iov[i].iov_len;
    }
    return len;
}

class AddrInfoResultRef {
    /**
     * @brief A class that wraps the result of getaddrinfo() and provides RAII semantics for the addrinfo linked list structure.
     *       The addrinfo structure is freed with freeaddrinfo() when the object is destroyed.
     *       Instances of this class cannot be copied, but can be moved. Use AddrInfoList for a shared abstraction that can be copied.
     */

public:
    AddrInfoResultRef() : _addrinfo(nullptr) {}

    explicit AddrInfoResultRef(struct addrinfo *_addrinfo) : _addrinfo(_addrinfo) {}

    // AddrInfoResultRef cannot be copied due to the single-owner nature of freeaddrinfo(). Use AddrInfoList instead.
    AddrInfoResultRef(const AddrInfoResultRef&) = delete;
    AddrInfoResultRef& operator=(const AddrInfoResultRef&) = delete;

    AddrInfoResultRef(AddrInfoResultRef&& other) :
        _addrinfo(other._addrinfo)
    {
        other._addrinfo = nullptr;
    }

    AddrInfoResultRef& operator=(AddrInfoResultRef&& other) {
        if (this != &other) {
            if (_addrinfo != nullptr) {
                freeaddrinfo(_addrinfo);
            }
            _addrinfo = other._addrinfo;
            other._addrinfo = nullptr;
        }
        return *this;
    }

    void swap(AddrInfoResultRef& other) {
        std::swap(_addrinfo, other._addrinfo);
    }

    ~AddrInfoResultRef() {
        if (_addrinfo != nullptr) {
            freeaddrinfo(_addrinfo);
        }
    }

    const struct addrinfo * get() const {
        return _addrinfo;
    }

    const struct addrinfo& operator*() const {
        if (_addrinfo == nullptr) {
              throw std::out_of_range("Attempt to dereference null addrinfo");
        }
        return *_addrinfo;
    }

    const struct addrinfo *operator->() const {
        if (_addrinfo == nullptr) {
              throw std::out_of_range("Attempt to dereference null addrinfo");
        }
        return _addrinfo;
    }

private:
    struct addrinfo *_addrinfo;
};

class AddrInfoList {
    /**
     * @brief A class that wraps the getaddrinfo() result and provides a range-based interface to the results.
     *        Provides RAII semantics for the addrinfo structure (calls freaddrinfo() on release of last reference). A
     *        shared reference is used so that this class can be copied and assigned.
     */
public:
    class Entry {
        /**
         * @brief A class that wraps a single addrinfo entry in an AddrInfoResultRef list. The AddrInfoResultRef is kept alive by a shared_ptr.
         */
    public:
        Entry() : _addrinfo(nullptr) {}
        Entry(const std::shared_ptr<AddrInfoResultRef>& addrinfo_result_ref, const struct addrinfo *addrinfo) :
            _addrinfo_result_ref(addrinfo_result_ref),
            _addrinfo(addrinfo)
        {
        }
        Entry(const Entry&) = default;
        Entry& operator=(const Entry&) = default;
        Entry(Entry&& other) :
            _addrinfo_result_ref(std::move(other._addrinfo_result_ref)),
            _addrinfo(other._addrinfo)
        {
            other._addrinfo = nullptr;
        }
        Entry& operator=(Entry&& other) {
            if (this != &other) {
                _addrinfo_result_ref = std::move(other._addrinfo_result_ref);
                _addrinfo = other._addrinfo;
                other._addrinfo = nullptr;
            }
            return *this;
        }

        const struct addrinfo * get() const {
            return _addrinfo;
        }

        const struct addrinfo& operator*() const {
            if (_addrinfo == nullptr) {
                  throw std::out_of_range("Attempt to dereference null addrinfo");
            }
            return *_addrinfo;
        }

        const struct addrinfo *operator->() const {
            if (_addrinfo == nullptr) {
                  throw std::out_of_range("Attempt to dereference null addrinfo");
            }
            return _addrinfo;
        }

        /**
         * @brief Return a string representation of the address (e.g., IPv4 or IPv6 address) in the entry
         * 
         * @return std::string String representation of address
         */
        std::string addr_string() const {
            char buf[NI_MAXHOST];
            if (getnameinfo(_addrinfo->ai_addr, _addrinfo->ai_addrlen, buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST) != 0) {
                throw std::system_error(errno, std::system_category(), "getnameinfo() failed");
            }
            return std::string(buf);
        }

    private:
        std::shared_ptr<AddrInfoResultRef> _addrinfo_result_ref;
        const struct addrinfo *_addrinfo;
    };

    class iterator {
        /**
         * @brief A class that provides an iterator iterator for AddrInfoList. Lifetime is scoped to the AddrInfoList.
         */
    public:
        iterator(const AddrInfoList *addrinfo_list, size_t index) :
            _addrinfo_list(addrinfo_list),
            _index(index)
        {
        }
        iterator(const iterator&) = default;
        iterator& operator=(const iterator&) = default;

        const Entry& operator*() const {
            if (_index >= _addrinfo_list->size()) {
                  throw std::out_of_range("Attempt to dereference addrinfo at end()");
            }
            return (*_addrinfo_list)[_index];
        }

        const Entry *operator->() const {
            if (_index >= _addrinfo_list->size()) {
                return nullptr;
            }
            return &((*_addrinfo_list)[_index]);
        }

        iterator& operator++() {
            if (_index >= _addrinfo_list->size()) {
                  throw std::out_of_range("Attempt to advance addrinfo iterator past end()");
            }
            ++_index;
            return *this;
        }

        bool operator==(const iterator& other) const {
            return _addrinfo_list == other._addrinfo_list && _index == other._index;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }

    private:
        const AddrInfoList * _addrinfo_list;
        size_t _index;
    };

public:
    AddrInfoList() {}

    explicit AddrInfoList(struct addrinfo *addrinfo) : 
        _addrinfo(std::make_shared<AddrInfoResultRef>(addrinfo))
    {
        updateResults();
    }

    AddrInfoList(
        const char *__restrict name,
        const char *__restrict service,
        const struct addrinfo *__restrict req
    )
    {
        struct addrinfo *raw_addrinfo = nullptr;
        if (getaddrinfo(name, service, req, &raw_addrinfo) != 0) {
            throw std::system_error(errno, std::system_category(), "getaddrinfo() failed");
        }
        _addrinfo = std::make_shared<AddrInfoResultRef>(raw_addrinfo);
        updateResults();
    }

    explicit AddrInfoList(
        const char *__restrict name,  // DNS name or IP address
        const char *__restrict service = nullptr,  // nullptr means "any service"
        int ai_flags = AI_CANONNAME, // AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST, AI_NUMERICSERV
        int ai_family = PF_UNSPEC,  // AF_INET, AF_INET6
        int ai_socktype = 0  // SOCK_DGRAM, SOCK_STREAM

    )
    {
        struct addrinfo req;
        memset(&req, 0, sizeof(req));
        req.ai_flags = ai_flags;
        req.ai_family = ai_family;
        req.ai_socktype = ai_socktype;
        struct addrinfo *raw_addrinfo = nullptr;
        if (getaddrinfo(name, service, &req, &raw_addrinfo) != 0) {
            throw std::system_error(errno, std::system_category(), "getaddrinfo() failed");
        }
        _addrinfo = std::make_shared<AddrInfoResultRef>(raw_addrinfo);
        updateResults();
    }

    // AddrInfoList cannot be copied

    AddrInfoList(const AddrInfoList&) = default;
    AddrInfoList& operator=(const AddrInfoList&) = default;

    AddrInfoList(AddrInfoList&& other) :
        _addrinfo(std::move(other._addrinfo)),
        _results(std::move(other._results))
    {
    }

    AddrInfoList& operator=(AddrInfoList&& other) {
        _addrinfo = std::move(other._addrinfo);
        _results = std::move(other._results);
        return *this;
    }

    size_t size() const {
        return _results.size();
    }

    const Entry& operator[](size_t i) const {
        return _results[i];
    }

    const iterator begin() const {
        return iterator(this, 0);
    }

    const iterator end() const {
        return iterator(this, size());
    }

    const struct addrinfo * get_raw() const {
        return _addrinfo->get();
    }

private:
    void updateResults() {
        const struct addrinfo *pai = _addrinfo->get();
        while (pai != nullptr) {
            _results.push_back(Entry(_addrinfo, pai));
            pai = pai->ai_next;
        }
    }

private:
    std::shared_ptr<AddrInfoResultRef> _addrinfo;
    std::vector<Entry> _results;
};


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
    size_t _n;                // Number of bytes in the queue
    uint64_t _n_total_bytes_produced;
    uint64_t _n_total_datagrams_produced;
    uint64_t _n_total_datagrams_discarded;
    size_t _min_produced_datagram_size;
    size_t _max_produced_datagram_size;
    size_t _first_produced_datagram_size;
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
        size_t max_n = DEFAULT_MAX_BACKLOG
    ) :
        _max_n(max_n),
        _producer_index(0),
        _consumer_index(0),
        _n(0),
        _n_total_bytes_produced(0),
        _n_total_datagrams_produced(0),
        _n_total_datagrams_discarded(0),
        _min_produced_datagram_size(0),
        _max_produced_datagram_size(0),
        _first_produced_datagram_size(0),
        _is_eof(false)
    {
        _data.resize(max_n);
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
                    _n_total_datagrams_discarded++;
                    continue;
                }
                if (_max_n < dg_len + PREFIX_LEN) {
                    throw std::runtime_error("Datagram + PREFIX too large for buffer: " + std::to_string(dg_len) + " + 4 bytes, max=" + std::to_string(_max_n) + " bytes");
                }
                if (n_free_locked() < dg_len + PREFIX_LEN) {
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
                _max_produced_datagram_size = std::max(_max_produced_datagram_size, dg_len);
                _min_produced_datagram_size = (_n_total_datagrams_produced == 0) ? dg_len : std::min(_min_produced_datagram_size, dg_len);
                if (_n_total_datagrams_produced == 0) {
                    _first_produced_datagram_size = dg_len;
                }
                _n_total_datagrams_produced++;
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
                    n_buffers_committed++;
                    continue;
                }
                if (_max_n < dg_len + PREFIX_LEN) {
                    throw std::runtime_error("Datagram + PREFIX too large for buffer: " + std::to_string(dg_len) + " + 4 bytes, max=" + std::to_string(_max_n) + " bytes");
                }
                if (n_free_locked() < dg_len + PREFIX_LEN) {
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
                _n_total_datagrams_produced++;
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
            _n_total_bytes_produced += n;
        }
    }

    inline size_t n_free_locked() {
        return _max_n - _n;
    }
};

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

class DatagramCopierStats {
public:
    uint64_t stat_seq;
    uint64_t n_datagrams;
    uint64_t n_datagram_bytes;
    uint64_t n_datagrams_discarded;
    size_t max_clump_size;
    size_t min_datagram_size;
    size_t max_datagram_size;
    size_t max_backlog_bytes;

    size_t first_datagram_size;
    struct timespec start_time;
    struct timespec end_time;

    DatagramCopierStats() :
        stat_seq(0),
        n_datagrams(0),
        n_datagram_bytes(0),
        n_datagrams_discarded(0),
        max_clump_size(0),
        min_datagram_size(0),
        max_datagram_size(0),
        max_backlog_bytes(0),
        first_datagram_size(0)
    {
        memset(&start_time, 0, sizeof(start_time));
        memset(&end_time, 0, sizeof(end_time));
    }

    DatagramCopierStats(const DatagramCopierStats&) = default;
    DatagramCopierStats& operator=(const DatagramCopierStats&) = default;

    double elapsed_secs() const {
        return std::max(timespec_to_secs(timespec_subtract(end_time, start_time)), 0.0);
    }

    double throughput_datagrams_per_sec() const {
        auto secs = elapsed_secs();
        // since start is time of first packet, and end is time of last packet, we need to
        // subtract 1 from the number of packets to get the number of intervals between packets
        return secs == 0.0 ? 0.0 : ((std::max(n_datagrams, (uint64_t)1) - 1) / elapsed_secs());
    }

    double throughput_bytes_per_sec() const {
        auto secs = elapsed_secs();
        // since start is time of first packet, and end is time of last packet, we need to
        // not count the first packet in the number of bytesused to calculate throughput
        return secs == 0.0 ? 0.0 : ((std::max(n_datagram_bytes, first_datagram_size) - first_datagram_size) / elapsed_secs());
    }

    double mean_datagram_size() const {
        return n_datagrams == 0 ? 0.0 : (double)n_datagram_bytes / (double)n_datagrams;
    }
};

/**
 * @brief A class that closes a file descriptor when it goes out of scope.
 */
class FileCloser {
public:
    int fd;

public:
    FileCloser(int _fd) : fd(_fd) {}

    void close() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    ~FileCloser() {
        close();
    }
};

class DatagramCopier : public NoBlockFileWriter {
private:
    int _sock;
    size_t _max_datagram_size;
    double _datagram_timeout_secs;
    double _first_datagram_timeout_secs;
    size_t _n_max_iovecs;

    DatagramCopierStats _stats;
    uint64_t _stat_version;
    bool _copying;


public:
    DatagramCopier(
        int sock,
        int output_file,
        size_t max_datagram_size = DEFAULT_MAX_DATAGRAM_SIZE,            // Max datagram size. 65535 is the maximum allowed by UDP. Truncated datagrams are discarded.
        size_t max_buffer_bytes = DEFAULT_MAX_BACKLOG,                   // Maximum number of buffered bytes to hold for background writing
        size_t max_write_size = 256*1024,                                // Maximum number of bytes to write in a single write() or writev() call
        double datagram_timeout_secs = DEFAULT_DATAGRAM_TIMEOUT_SECS,    // Timeout between datagrams; after this time, stream is considered ended. If 0, wait forever for next datagram.
        double first_datagram_timeout_secs = -1.0,                       // Timeout for first datagram. If 0, wait forever for first datagram. If < 0, use datagram_timeout_secs.
        size_t n_max_iovecs = 0                                          // Maximum number of iovecs that can be used in a single recvmmsg() call. limited to sysconf(_SC_IOV_MAX). 0 means use max possible.
    ) :
        NoBlockFileWriter(output_file, max_datagram_size, max_buffer_bytes, max_write_size),
        _sock(sock),
        _max_datagram_size(max_datagram_size),
        _datagram_timeout_secs(datagram_timeout_secs),
        _first_datagram_timeout_secs(first_datagram_timeout_secs),
        _n_max_iovecs(n_max_iovecs),
        _stat_version(0),
        _copying(false)
    {
        if (_first_datagram_timeout_secs < 0.0) {
            _first_datagram_timeout_secs = _datagram_timeout_secs;
        }
        auto sys_max_iovecs = (size_t)sysconf(_SC_IOV_MAX);
        if (sys_max_iovecs < 0) {
            throw std::system_error(errno, std::system_category(), "sysconf(_SC_IOV_MAX) failed");
        }
        if (_n_max_iovecs == 0 || _n_max_iovecs > (size_t)sys_max_iovecs) {
            _n_max_iovecs = (size_t)sys_max_iovecs;
        }
        std::cerr << "DataGramCopier: max iovecs per recvmmsg=" << _n_max_iovecs << "\n";
    }

    DatagramCopierStats get_stats(void) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _stats;
    }

    void copy(
    )
    {
        {
            struct timespec first_dg_timespec;
            struct timespec dg_timespec;

            if (_first_datagram_timeout_secs > 0.0) {
                first_dg_timespec.tv_sec = (time_t)_first_datagram_timeout_secs;
                first_dg_timespec.tv_nsec = (long)((_first_datagram_timeout_secs - (double)first_dg_timespec.tv_sec) * 1.0e9);
                // std::cerr << "First datagram timeout: " << first_dg_timespec.tv_sec << " seconds, " << first_dg_timespec.tv_nsec << " nanoseconds\n";
            }


            if (_datagram_timeout_secs > 0.0) {
                dg_timespec.tv_sec = (time_t)_datagram_timeout_secs;
                dg_timespec.tv_nsec = (long)((_datagram_timeout_secs - (double)dg_timespec.tv_sec) * 1.0e9);
                // std::cerr << "Datagram timeout: " << dg_timespec.tv_sec << " seconds, " << dg_timespec.tv_nsec << " nanoseconds\n";
            }

            // Allocate reusable buffers for recvmmsg
            std::vector<std::vector<char>> buffers(_n_max_iovecs);
            std::vector<struct mmsghdr> msgs(_n_max_iovecs);
            std::vector<struct iovec> iovs(_n_max_iovecs);
            for (size_t i = 0; i < _n_max_iovecs; ++i) {
                auto& buffer = buffers[i];
                auto& msg = msgs[i];
                auto& iov = iovs[i];
                buffer.resize(_max_datagram_size);
                iov.iov_base = buffer.data();
                iov.iov_len = buffer.size();
                msg.msg_hdr.msg_iov = &iov;
                msg.msg_hdr.msg_iovlen = 1;
            }

            // Run recvmmsg forever
            uint64_t n_datagrams = 0;
            struct timespec end_time;
            struct timespec start_time;
            struct timespec *current_timeout = nullptr;
            while (true) {
                auto old_n_datagrams = n_datagrams;
                auto timeout = (n_datagrams == 0) ? &first_dg_timespec : &dg_timespec;
                if (timeout != current_timeout) {
                    // std::cerr << "Setting timeout to " << timeout->tv_sec << " seconds, " << timeout->tv_nsec << " nanoseconds\n";
                    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(*timeout));
                    current_timeout = timeout;
                }
                int n = recvmmsg(_sock, msgs.data(), msgs.size(), MSG_WAITFORONE, nullptr);
                if (n == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::cerr << "Timeout waiting for datagram; shutting down\n";
                        break;
                    }
                    if (errno == EINTR) {
                        std::cerr << "Interrupted by signal; continuing\n";
                        continue;
                    }
                    throw std::system_error(errno, std::system_category(), "recvmmsg() failed");
                }
                if (n == 0) {
                    std::cerr << "Timeout waiting for datagram; shutting down\n";
                    break;
                }
                clock_gettime(CLOCK_REALTIME, &end_time);
                if (n_datagrams == 0) {
                    start_time = end_time;
                    std::cerr << "First datagram received...\n";
                }
                if (n > 1 && n == _n_max_iovecs) {
                    std::cerr << "   WARNING: recvmmsg response full (" << n << " datagrams), possible packet loss)\n";
                }
                producer_commit_batch(msgs.data(), n);
                n_datagrams += n;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _stats.stat_seq++;
                    _stats.n_datagrams = n_datagrams;
                    _stats.n_datagram_bytes = _n_total_bytes_produced - (n_datagrams * PREFIX_LEN);
                    _stats.n_datagrams_discarded = _n_total_datagrams_discarded;
                    _stats.min_datagram_size = _min_produced_datagram_size;
                    _stats.max_datagram_size = _max_produced_datagram_size;
                    _stats.first_datagram_size = _first_produced_datagram_size;
                    _stats.max_clump_size = std::max(_stats.max_clump_size, n_datagrams - old_n_datagrams);
                    _stats.max_backlog_bytes = std::max(_stats.max_backlog_bytes, _n);
                    _stats.start_time = start_time;
                    _stats.end_time = end_time;
                }
            }

            // TODO: Update final stats.
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " [<local-bind-addr>:]<port> <output_file>\n";
        return 1;
    }

    size_t max_dg_size = 65535;
    size_t max_backlog = DEFAULT_MAX_BACKLOG;
    size_t max_write_size = DEFAULT_MAX_WRITE_SIZE;
    double first_datagram_timeout_secs = 60.0;
    double datagram_timeout_secs = 10.0;

    std::string addr_and_port(argv[1]);
    std::string output_file(argv[2]);

    size_t colon_pos = addr_and_port.rfind(':');
    std::string addr_str;
    uint16_t  port;
    if (colon_pos == std::string::npos) {
        addr_str = std::string("0.0.0.0");
        port = std::stoi(addr_and_port);
    } else {
        addr_str = addr_and_port.substr(0, colon_pos);
        port = std::stoi(addr_and_port.substr(colon_pos + 1));
    }


    AddrInfoList addrinfo_list(
        addr_str.c_str(),
        std::to_string(port).c_str(),
        AI_PASSIVE,
        AF_UNSPEC,
        SOCK_DGRAM
      );

    if (addrinfo_list.size() == 0) {
        throw std::runtime_error("No addresses found for " + addr_str + ":" + std::to_string(port));
    }

    for(auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
        auto& entry = *ai;
        std::cerr << "Addr=" << entry.addr_string() << " (" << entry->ai_addr << ") Family=" << entry->ai_family << " SockType=" << entry->ai_socktype << " Protocol=" << entry->ai_protocol << "\n";
    }
        

    int s = -1;
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

        close(s);
    }

    if (s == -1) {
        throw std::runtime_error("Could not bind socket to any addresses");
    }

    std::cerr << "Bound to " << matching_entry.addr_string() << ":" << port << "\n";

    {
        FileCloser file(open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC));
        if (file.fd < 0) {
            throw std::system_error(errno, std::system_category(), "open() failed");
        }
        DatagramCopier copier(
            s,
            file.fd,
            max_dg_size,
            max_backlog,
            max_write_size,
            datagram_timeout_secs,
            first_datagram_timeout_secs
        );

        copier.copy();
        file.close();

        auto stats = copier.get_stats();

        std::cerr << "\nFinished copying datagrams\n";

        std::cerr << "Received " << stats.n_datagrams << " datagrams totaling " << stats.n_datagram_bytes << " datagram bytes (not including length prefixes)\n";
        std::cerr << "Discarded " << stats.n_datagrams_discarded << " datagrams\n";
        std::cerr << "Max clump size: " << stats.max_clump_size << " datagrams\n";
        std::cerr << "Min datagram size: " << stats.min_datagram_size << " bytes\n";
        std::cerr << "Max datagram size: " << stats.max_datagram_size << " bytes\n";
        std::cerr << "Mean datagram size: " << stats.mean_datagram_size() << " bytes\n";
        std::cerr << "Elapsed time: " << stats.elapsed_secs()  << " seconds\n";
        std::cerr << "Max backlog: " << stats.max_backlog_bytes << " bytes\n";
        std::cerr << "Throughput: " << stats.throughput_datagrams_per_sec() << " datagrams/second (" << stats.throughput_bytes_per_sec() << " bytes/second)\n";
    }

    return 0;
}
