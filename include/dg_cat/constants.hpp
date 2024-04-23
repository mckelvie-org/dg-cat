/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include <cstddef>
#include <cstdint>

static const double DEFAULT_DATAGRAM_TIMEOUT_SECS = 60.0;
static const size_t DEFAULT_MAX_DATAGRAM_SIZE = 65535;                // 65535 is the maximum allowed by UDP
static const size_t DEFAULT_NUM_DATAGRAM_BUFFERS = 2048;              // Maximum number of datagrams that can be received in one go with recvmmsg().
                                                                      //   (Will be further restricted by the kernel's maximum iovec count.)
static const size_t DEFAULT_MAX_BACKLOG = 2UL*1024*1024*1024;         // Maximum file buffer size (2GB)
static const size_t DEFAULT_MAX_READ_SIZE = 256*1024;                 // Maximum number of bytes to read from a file in one system call
static const size_t DEFAULT_MAX_WRITE_SIZE = 256*1024;                // Maximum number of bytes to write to a file in one system call
static const size_t PREFIX_LEN = sizeof(uint32_t);                    // Length of the network-byte-order datagram-length prefix used when writing output
static const double DEFAULT_POLLING_INTERVAL = 1.0;                   // Datagram polling interval
static const double DEFAULT_EOF_TIMEOUT_SECS = 60.0;                  // timeout waiting for datagrams on UDP before an EOF is inferred. <= 0 means no timeout.
static const double DEFAULT_START_TIMEOUT_SECS = 0.0;                 // Timeout waiting for the first datagram on UDP. < 0 means use eof_timeout == 0 means no timeout.
static const double DEFAULT_MAX_DATAGRAM_RATE = 0.0;                  // For UDP sender, max rate in datagrams/second. if <= 0.0, no limit.
static const uint64_t DEFAULT_MAX_DATAGRAMS = 0;                      // Max datagrams to copy.  0 == no limit
static const size_t DEFAULT_MAX_IOVECS = 0;                           // Maximum number of iovecs that can be used in a single recvmmsg() call.
                                                                      //   Will be limited to sysconf(_SC_IOV_MAX). 0 means use max possible.