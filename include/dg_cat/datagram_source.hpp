#pragma once

#include "buffer_queue.hpp"
#include "stats.hpp"
/**
 * @brief Abstract base class for datagram sources.
 */
class DatagramSource {
public:
    virtual ~DatagramSource() = default;
    /**
     * @brief Copy datagrams from the source until an EOF is encountered or force_eof() is called.
     * 
     * @param buffer_queue The buffer queue to write datagrams to.
     */
    virtual void copy_to_buffer_queue(BufferQueue& buffer_queue, LockableDgSourceStats& stats) = 0;

    /**
     * @brief Force an EOF condition on the source. This method will be called from a different
     *        thread than copy_to_buffer_queue().
     */
    virtual void force_eof(void) = 0;
};

