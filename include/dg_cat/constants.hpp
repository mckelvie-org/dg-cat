#pragma once

#include <cstddef>
#include <cstdint>

static const double DEFAULT_DATAGRAM_TIMEOUT_SECS = 60.0;
static const size_t DEFAULT_MAX_DATAGRAM_SIZE = 65535;                // 65535 is the maximum allowed by UDP
static const size_t DEFAULT_NUM_DATAGRAM_BUFFERS = 2048;              // Maximum number of datagrams that can be received in one go with recvmmsg().
                                                                      //   (Will be further restricted by the kernel's maximum iovec count.)
static const size_t DEFAULT_MAX_BACKLOG = 2UL*1024*1024*1024;         // Maximum file buffer size (2GB)
static const size_t DEFAULT_MAX_WRITE_SIZE = 256*1024;                // Maximum number of bytes to write in one system call
static const size_t PREFIX_LEN = sizeof(uint32_t);                    // Length of the network-byte-order datagram-length prefix used when writing output

