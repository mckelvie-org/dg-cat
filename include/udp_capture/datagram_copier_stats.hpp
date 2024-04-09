#pragma once

#include "timespec_math.hpp"

#include <cstdint>
#include <algorithm>
#include <unistd.h>

// #include <sys/socket.h>
// #include <netdb.h>

// //#include <boost/asio.hpp>
// #include <boost/endian/conversion.hpp>
// #include <fstream>
// #include <iostream>
// #include <vector>
// #include <array>
// #include <thread>
// #include <mutex>
// #include <condition_variable>
// #include <exception>
// #include <time.h>
// #include <csignal>
#include <cstring>
#include <memory>
// #include <chrono>
// #include <sys/uio.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <cassert>
// #include <cmath>

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
