#pragma once

#if 0

#include "constants.hpp"
#include "timespec_math.hpp"
#include "util.hpp"
#include "buffer_queue.hpp"
#include "no_block_file_writer.hpp"
#include "stats.hpp"
#include "file_closer.hpp"

class DatagramCopier : public NoBlockFileWriter {
private:
    int _sock;
    size_t _max_datagram_size;
    double _datagram_timeout_secs;
    double _first_datagram_timeout_secs;
    size_t _n_max_iovecs;

    DgCatStats _stats;
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

    DgCatStats get_stats() {
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

#endif
