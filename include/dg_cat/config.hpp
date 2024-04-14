#pragma once

#include "constants.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

class DgCatConfig {
public:
    size_t bufsize;                // Max datagram size(not including length prefix);
    size_t max_backlog;            // Max number of unwritten bytes to buffer, including length prefixes.
    double polling_interval;       // Datagram polling interval
    double eof_timeout;            // timeout waiting for datagrams on UDP before an EOF is inferred. <= 0 means no timeout.
    double start_timeout;          // Timeout waiting for the first datagram on UDP. <= 0 means no timeout.
    double max_datagram_rate;      // For UDP sender, max rate in datagrams/second. if <= 0.0, no limit.
    uint64_t max_datagrams;        // maximum number of datagrams to write. 0 means no limit.
    size_t max_write_size;         // Maximum number of bytes to write in one system call
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
     * @param max_write_size      Maximum number of bytes to write in one system call
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
            size_t max_write_size = DEFAULT_MAX_WRITE_SIZE,
            bool append = false
        ) :
            bufsize(bufsize),
            max_backlog(max_backlog),
            polling_interval(polling_interval),
            eof_timeout(eof_timeout),
            start_timeout((start_timeout < 0.0) ? eof_timeout: start_timeout),
            max_datagram_rate(max_datagram_rate),
            max_datagrams(max_datagrams),
            max_write_size(max_write_size),
            append(append)
    {
    }

    DgCatConfig(const DgCatConfig&) = default;
    DgCatConfig& operator=(const DgCatConfig&) = default;
    DgCatConfig(DgCatConfig&&) = default;
    DgCatConfig& operator=(DgCatConfig&&) = default;

    std::string to_string() const {
        return "DgCatConfig{"
            "bufsize=" + std::to_string(bufsize) + ", "
            "max_backlog=" + std::to_string(max_backlog) + ", "
            "polling_interval=" + std::to_string(polling_interval) + ", "
            "eof_timeout=" + std::to_string(eof_timeout) + ", "
            "start_timeout=" + std::to_string(start_timeout) + ", "
            "max_datagram_rate=" + std::to_string(max_datagram_rate) + ", "
            "max_datagrams=" + std::to_string(max_datagrams) + ", "
            "max_write_size=" + std::to_string(max_write_size) + ", "
            "append=" + (append ? "true" : "false") + "}";
    }
};
