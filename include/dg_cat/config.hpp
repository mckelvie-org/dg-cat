#pragma once

#include "constants.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

#include <unistd.h>

class DgCatConfig {
public:
    size_t bufsize;                // Max datagram size(not including length prefix);
    size_t max_backlog;            // Max number of unwritten bytes to buffer, including length prefixes.
    double polling_interval;       // Datagram polling interval
    double eof_timeout;            // timeout waiting for datagrams on UDP before an EOF is inferred. <= 0 means no timeout.
    double start_timeout;          // Timeout waiting for the first datagram on UDP. <= 0 means no timeout.
    double max_datagram_rate;      // For UDP sender, max rate in datagrams/second. if <= 0.0, no limit.
    uint64_t max_datagrams;        // maximum number of datagrams to write. 0 means no limit.
    size_t max_read_size;          // Maximum number of bytes to read from a file/pipe in one system call
    size_t max_write_size;         // Maximum number of bytes to write to a file/pipe in one system call
    size_t max_iovecs;             // Maximum number of iovecs that can be used in a single recvmmsg() call.
                                   //   Will be limited to sysconf(_SC_IOV_MAX). 0 means use max possible.
    bool append;                   // For file output, true if existing file should be appended.

    /**
     * @brief Construct a new DgCatConfig object
     * 
     * @param bufsize             Max datagram size(not including length prefix);
     * @param max_backlog         Max number of unwritten bytes to buffer, including length prefixes.
     * @param polling_interval    Datagram polling interval
     * @param eof_timeout         timeout waiting for datagrams on UDP before an EOF is inferred. <= 0 means no timeout.
     * @param start_timeout       Timeout waiting for the first datagram on UDP. < 0 means use eof_timeout. == 0 means no timeout.
     * @param max_datagram_rate   For UDP sender, max rate in datagrams/second. if <= 0.0, no limit.
     * @param max_datagrams       maximum number of datagrams to write. 0 means no limit.
     * @param max_read_size       Maximum number of bytes to read from a file/pipe in one system call
     * @param max_write_size      Maximum number of bytes to write to a file/pipein one system call
     * @param max_iovecs          Maximum number of iovecs that can be used in a single recvmmsg() call.
     *                                Will be limited to sysconf(_SC_IOV_MAX). 0 means use max possible.
     * @param append              For file output, true if existing file should be appended.
     */
    DgCatConfig(
            size_t bufsize = DEFAULT_MAX_DATAGRAM_SIZE,
            size_t max_backlog = DEFAULT_MAX_BACKLOG,
            double polling_interval = DEFAULT_POLLING_INTERVAL,
            double eof_timeout = DEFAULT_EOF_TIMEOUT_SECS,
            double start_timeout = DEFAULT_START_TIMEOUT_SECS,
            double max_datagram_rate = DEFAULT_MAX_DATAGRAM_RATE,
            uint64_t max_datagrams = DEFAULT_MAX_DATAGRAMS,
            size_t max_read_size = DEFAULT_MAX_READ_SIZE,
            size_t max_write_size = DEFAULT_MAX_WRITE_SIZE,
            size_t max_iovecs = DEFAULT_MAX_IOVECS,
            bool append = false
        ) :
            bufsize(bufsize),
            max_backlog(max_backlog),
            polling_interval(polling_interval),
            eof_timeout(eof_timeout),
            start_timeout((start_timeout < 0.0) ? eof_timeout: start_timeout),
            max_datagram_rate(max_datagram_rate),
            max_datagrams(max_datagrams),
            max_read_size(max_read_size),
            max_write_size(max_write_size),
            append(append)
    {
        auto sys_max_iovecs = (size_t)sysconf(_SC_IOV_MAX);
        if (sys_max_iovecs < 0) {
            throw std::system_error(errno, std::system_category(), "sysconf(_SC_IOV_MAX) failed");
        }
        if (max_iovecs == 0 || max_iovecs > sys_max_iovecs) {
            max_iovecs = sys_max_iovecs;
        }
        this->max_iovecs = max_iovecs;
    }

    DgCatConfig(const DgCatConfig&) = default;
    DgCatConfig& operator=(const DgCatConfig&) = default;
    DgCatConfig(DgCatConfig&&) = default;
    DgCatConfig& operator=(DgCatConfig&&) = default;

    std::string to_string() const {
        return "DgCatConfig{ "
            "bufsize=" + std::to_string(bufsize) + ", "
            "max_backlog=" + std::to_string(max_backlog) + ", "
            "polling_interval=" + std::to_string(polling_interval) + ", "
            "eof_timeout=" + std::to_string(eof_timeout) + ", "
            "start_timeout=" + std::to_string(start_timeout) + ", "
            "max_datagram_rate=" + std::to_string(max_datagram_rate) + ", "
            "max_datagrams=" + std::to_string(max_datagrams) + ", "
            "max_read_size=" + std::to_string(max_read_size) + ", "
            "max_write_size=" + std::to_string(max_write_size) + ", "
            "max_iovecs=" + std::to_string(max_iovecs) + ", "
            "append=" + (append ? "true" : "false") + " }";
    }
};
