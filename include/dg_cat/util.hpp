/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include <boost/endian/conversion.hpp>
#include <sys/uio.h>

#include <stdexcept>

/**
 * @brief Writes a 4-byte network-byte-order length prefix to a buffer
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

