#pragma once

#include "buffer_queue.hpp"

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
     */
    virtual void copy_from_buffer_queue(BufferQueue& buffer_queue) = 0;
};

