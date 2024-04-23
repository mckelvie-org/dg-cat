/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include <memory>

#include "config.hpp"
#include "buffer_queue.hpp"
#include "stats.hpp"

/**
 * @brief Abstract base class for datagram destinations.
 */
class DatagramDestination {
public:
    virtual ~DatagramDestination() = default;
    /**
     * @brief Copy datagrams from the BufferQueue until an EOF is encountered.
     * 
     * @param buffer_queue The buffer queue to read datagrams from.
     * @param stats        The threadsafe stats object to update with real-time progress.
     */
    virtual void copy_from_buffer_queue(BufferQueue& buffer_queue, LockableDgDestinationStats& stats) = 0;

    /**
     * @brief static factory method to create a typed DatagramDestination based on a pathname
     * 
     * @param config   The configuration object
     * @param path     The path to the destination
     * 
     * @return unique_ptr<DatagramDestination>
     */
    static std::unique_ptr<DatagramDestination> create(const DgCatConfig& config, const std::string& path);
};
