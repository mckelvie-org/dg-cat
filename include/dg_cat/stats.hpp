/**
 * @file stats.hpp
 * @author Sam McKelvie (dev@mckelvie.org)
 * @brief Real-time updated statistics for the datagram copier
 * 
 * @licence MIT License
 * 
 * @copyright Copyright (c) 2024 Samuel J. McKelvie
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include "timespec_math.hpp"

#include <cstdint>
#include <algorithm>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <mutex>

/**
 * @brief Class template that adds a mutex to a stats object, and holds mutexes during copy construct/assignment operations.
 * 
 * @tparam _T   The stats object type.
 */
template<class _T> class LockableStats : public _T {
public:
    std::mutex _mutex;

    LockableStats() = default;
    LockableStats(LockableStats& other)
    {
        std::lock_guard<std::mutex> lock(other._mutex);
        _T::operator=(static_cast<const _T&>(other));
    }
    LockableStats(const _T& other) : _T(other) {}
    LockableStats(LockableStats&& other) = default;
    LockableStats& operator=(LockableStats& other) {
        // Use a temporary intermediate object to avoid deadlock
        LockableStats<_T> temp(other);
        std::lock_guard<std::mutex> lock(_mutex);
        _T::operator=(static_cast<const _T&>(temp));
        return *this;
    }
    LockableStats& operator=(const _T& other) {
        _T::operator=(other);
        return *this;
    }

    _T get() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _T(static_cast<const _T&>(*this));
    }
};

/**
 * @brief Stats provided by the datagram source
 */
class DgSourceStats {
public:
    uint64_t n_datagrams;               // Number of datagrams produced
    uint64_t n_datagram_bytes;          // Number of datagram bytes produced (not including length prefixes)
    uint64_t n_datagrams_discarded;     // Number of datagrams discarded due to buffer overflow
    size_t max_clump_size;              // Maximum number of datagrams produced in a single call to recvmmsg
    size_t min_datagram_size;           // Minimum datagram size produced
    size_t max_datagram_size;           // Maximum datagram size produced
    size_t first_datagram_size;         // Size of the first datagram produced
    struct timespec start_time;         // Time the first datagram was produced
    struct timespec end_time;           // Time the last datagram was produced

    DgSourceStats() :
        n_datagrams(0),
        n_datagram_bytes(0),
        n_datagrams_discarded(0),
        max_clump_size(0),
        min_datagram_size(0),
        max_datagram_size(0),
        first_datagram_size(0)
    {
        memset(&start_time, 0, sizeof(start_time));
        memset(&end_time, 0, sizeof(end_time));
    }

    DgSourceStats(const DgSourceStats&) = default;
    DgSourceStats(DgSourceStats&&) = default;
    DgSourceStats& operator=(const DgSourceStats&) = default;
    DgSourceStats& operator=(DgSourceStats&&) = default;

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

typedef LockableStats<DgSourceStats> LockableDgSourceStats;

/**
 * @brief Stats provided by the datagram destination
 */
class DgDestinationStats {
public:

    DgDestinationStats()
    {
    }

    DgDestinationStats(const DgDestinationStats&) = default;
    DgDestinationStats(DgDestinationStats&&) = default;
    DgDestinationStats& operator=(const DgDestinationStats&) = default;
    DgDestinationStats& operator=(DgDestinationStats&&) = default;
};

typedef LockableStats<DgDestinationStats> LockableDgDestinationStats;

/**
 * @brief Stats provided by the intermediate BufferQueue between the source and destination
 */
class DgBufferStats {
public:
    size_t max_backlog_bytes;           // Maximum number of bytes buffered for writing

    DgBufferStats() :
        max_backlog_bytes(0)
    {
    }

    DgBufferStats(const DgBufferStats&) = default;
    DgBufferStats(DgBufferStats&&) = default;
    DgBufferStats& operator=(const DgBufferStats&) = default;
    DgBufferStats& operator=(DgBufferStats&&) = default;

};

typedef LockableStats<DgBufferStats> LockableDgBufferStats;

/**
 * @brief Aggregated stats for the datagram copier
 */
class DgCatStats {
public:
    uint64_t stat_seq;
    DgSourceStats source_stats;
    DgDestinationStats destination_stats;
    DgBufferStats buffer_stats;

    DgCatStats() :
        stat_seq(0)
    {
    }

    DgCatStats(const DgCatStats&) = default;
    DgCatStats(DgCatStats&&) = default;
    DgCatStats(
                uint64_t stat_seq,
                const DgSourceStats& source_stats,
                const DgDestinationStats& destination_stats,
                const DgBufferStats& buffer_stats
            ) :
        stat_seq(stat_seq),
        source_stats(source_stats),
        destination_stats(destination_stats),
        buffer_stats(buffer_stats)
    {
    }
    DgCatStats(
                uint64_t stat_seq,
                DgSourceStats&& source_stats,
                DgDestinationStats&& destination_stats,
                DgBufferStats&& buffer_stats
            ) :
        stat_seq(stat_seq),
        source_stats(std::move(source_stats)),
        destination_stats(std::move(destination_stats)),
        buffer_stats(std::move(buffer_stats))
    {
    }
    DgCatStats& operator=(const DgCatStats&) = default;
    DgCatStats& operator=(DgCatStats&&) = default;
};

/**
 * @brief Aggregated lockable stats for the datagram copier
 */
class LockableDgCatStats {
public:
    LockableDgSourceStats source_stats;
    LockableDgDestinationStats destination_stats;
    LockableDgBufferStats buffer_stats;

    LockableDgCatStats()
    {
    }

    LockableDgCatStats(LockableDgCatStats&) = default;
    LockableDgCatStats(const DgCatStats& other) :
        source_stats(other.source_stats),
        destination_stats(other.destination_stats),
        buffer_stats(other.buffer_stats)
    {
    }
    LockableDgCatStats(LockableDgCatStats&&) = default;
    LockableDgCatStats(DgCatStats&& other) :
        source_stats(std::move(other.source_stats)),
        destination_stats(std::move(other.destination_stats)),
        buffer_stats(std::move(other.buffer_stats))
    {
    }
    LockableDgCatStats(
                LockableDgSourceStats& source_stats,
                LockableDgDestinationStats& destination_stats,
                LockableDgBufferStats& buffer_stats
            ) :
        source_stats(source_stats),
        destination_stats(destination_stats),
        buffer_stats(buffer_stats)
    {
    };
    LockableDgCatStats(
                LockableDgSourceStats&& source_stats,
                LockableDgDestinationStats&& destination_stats,
                LockableDgBufferStats&& buffer_stats
            ) :
        source_stats(std::move(source_stats)),
        destination_stats(std::move(destination_stats)),
        buffer_stats(std::move(buffer_stats))
    {
    };
    LockableDgCatStats(
                const DgSourceStats& source_stats,
                const DgDestinationStats& destination_stats,
                const DgBufferStats& buffer_stats
            ) :
        source_stats(source_stats),
        destination_stats(destination_stats),
        buffer_stats(buffer_stats)
    {
    }
    LockableDgCatStats& operator=(LockableDgCatStats& other) = default;
    LockableDgCatStats& operator=(const DgCatStats& other) {
        source_stats = other.source_stats;
        destination_stats = other.destination_stats;
        buffer_stats = other.buffer_stats;
        return *this;
    }
    LockableDgCatStats& operator=(DgCatStats&& other) {
        source_stats = std::move(other.source_stats);
        destination_stats = std::move(other.destination_stats);
        buffer_stats = std::move(other.buffer_stats);
        return *this;
    }

    DgCatStats get(uint64_t stat_seq) {
        auto simple_source_stats = source_stats.get();
        auto simple_destination_stats = destination_stats.get();
        auto simple_buffer_stats = buffer_stats.get();

        return DgCatStats(
            stat_seq,
            std::move(simple_source_stats),
            std::move(simple_destination_stats),
            std::move(simple_buffer_stats)
        );
    }
};
