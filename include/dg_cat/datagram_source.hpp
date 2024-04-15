#pragma once

#include <memory>

#include "config.hpp"
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
     * @param stats        The threadsafe stats object to update with real-time progress.
     */
    virtual void copy_to_buffer_queue(BufferQueue& buffer_queue, LockableDgSourceStats& stats) = 0;

    /**
     * @brief Force an EOF condition on the source as soon as possible. This method will be called from a different
     *        thread than copy_to_buffer_queue(). This method will be called when an asynchronous signal is received
     *       to terminate the program cleanly.
     */
    virtual void force_eof() = 0;

    /**
     * @brief static factory method to create a typed DatagramSource based on a pathname
     * 
     * @param config   The configuration object
     * @param path     The path to the source
     * 
     * @return unique_ptr<DatagramSource>
     */
    static std::unique_ptr<DatagramSource> create(const DgCatConfig& config, const std::string& path);
};

