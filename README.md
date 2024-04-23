# dg-cat
=================================================

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Latest release](https://img.shields.io/github/v/release/mckelvie-org/dg-cat.svg?style=flat-square&color=b44e88)](https://github.com/mckelvie-org/dg-cat/releases)

A simple command-line utility that reads datagrams from a random generator, UDP socket,
file, or pipe, and forwards them to another UDP socket, file, or pipe. For files and
pipes, each datagram is prefixed with a 4-byte length field in network byte order
(big-endian).

Table of contents
-----------------

* [Introduction](#introduction)
* [Installation](#installation)
* [Usage](#usage)
  * [Command line](#command-line)
  * [API](api)
* [Known issues and limitations](#known-issues-and-limitations)
* [Getting help](#getting-help)
* [Contributing](#contributing)
* [License](#license)
* [Authors and history](#authors-and-history)


Introduction
------------

_dg-cat_ is a C++ library and a simple command-line utility that can copy datagram-type messages
between different a datagram source and a datagram destination.

A datagram source can be one of:

* a unidirectional incoming stream of UDP messages delivered to a local UDP socket
* a file or piped byte stream
* a configurable generator of random datagrams

A datagram destination can be one of:

* a unidirectional outgoing stream of UDP messages delivered to a UDP host/socket
* a file or piped byte stream

Features:

* Reading and writing are performed in separate threads.
* A very large threadsafe intermediate buffer is used to
  minimize the chance of dropped UDP packets.
* recvmmsg() is used for UDP sources to reduce system call
  overhead and minimize dropped UDP packets.
* For files and pipes, each datagram is prefixed with a 4-byte
  length in network byte order (big-endian). This allows message boundaries
  to be preserved in these byte-stream protocols.
* Pending datagrams are coalesced when written to files/pipes
  to reduce system call overhead.
* SIGINT is handled and causes already received datagrams to be
  cleanly drained to the destination before exiting.
* SIGUSR1 is handled and causes progress statistics to be
  written to stderr.
* EOF can optionally be inferred from incoming UDP data with any of:
  * a configurable maximum number of datagrams to copy.
  * a configurable time passed with no new packets received.
  * SIGINT is received.
* A "random://" pseudo-source is provided that can generate
  random datagrams with a configurable range of sizes.
* For UDP destinations, outgoing datagram rate can be limited to
  a configurable number of datagrams/second, to minimize
  the chance of dropped packets between dg-cat and a receiving
  agent.

Usage
=====

Command Line
------------

There is a single command tool `dg-cat` that is installed with the package.

```bash
Usage: dg-cat [--help] [--version] [--max-datagram-size VAR] [--max-backlog VAR] [--eof-timeout VAR] [--start-timeout VAR] [--max-datagram-rate VAR] [--max-datagrams VAR] [--max-read-size VAR] [--max-write-size VAR] [--max-iovecs VAR] [--append] [--no-handle-signals] [--log-level VAR] [--tb] src dst

Copy between datagram streams while preserving message lengths.

A simple command-line utility that reads datagrams from a random generator, UDP socket,
file, or pipe, and forwards them to another UDP socket, file, or pipe. For files and
pipes, each datagram is prefixed with a 4-byte length field in network byte order
(big-endian).

Positional arguments:
  src                      The source of datagrams. Can be one of: 
                               "<filename>"
                               "file://<filename>"
                               "udp://<local-port"
                               "udp://<local-bind-addr>:<local-port>"
                               "random://[?][n=<num-datagrams>][&min=<min-bytes>][&max=<max-bytes>][&seed=<seed>]"
                               "stdin"
                               "-"        (alias for stdin)
                           If omitted, "stdin" is used. [nargs=0..1] [default: "stdin"]
  dst                      The destination of datagrams. Can be one of: 
                               "<filename>"
                               "file://<filename>"
                               "udp://<remote-addr>:<remote-port>"
                               "stdout"
                               "-"       (alias for stdout)
                           If omitted, stdout is used. [nargs=0..1] [default: "stdout"]

Optional arguments:
  -h, --help               shows help message and exits 
  -v, --version            prints version information and exits 
  -d, --max-datagram-size  For UDP input, the per-datagram buffer size. Datagrams larger than this are discarded.
                             [nargs=0..1] [default: 65535]
  -b, --max-backlog        The maximum number of bytes (including 4-byte per-datagram length prefixes) to buffer
                           before stalling input. For UDP input, stalling input may cause datagrams to be dropped.
                             [nargs=0..1] [default: 2147483648]
  -t, --eof-timeout        For UDP sources, a number of seconds with no datagrams received that should be interpreted
                           as an EOF. If <= 0.0, allows unlimited time between datagrams (copying will not terminate
                           until a signal is received). [nargs=0..1] [default: 60]
  --start-timeout          For UDP sources, a number of seconds to wait for the first datagram before ending with an
                           empty stream. If < 0, the value for --eof-timeout will be used. If 0.0, will wait forever
                           for the first datagram. By default, the value for --eof-timeout is used.
                             [nargs=0..1] [default: -1]
  -r, --max-datagram-rate  For UDP outputs, the maximun datagrams per second to send. If <= 0.0, does not limit
                           datagram send rate (may cause datagrams to be dropped by receiver or enroute).
                             [nargs=0..1] [default: 0]
  -n, --max-datagrams      Stop after copying the specified number of datagrams. If 0, copy all datagrams.
                             [nargs=0..1] [default: 0]
  -R, --max-read-size      For file inputs, the maximum number of bytes to read in a single system call.
                             [nargs=0..1] [default: 262144]
  -w, --max-write-size     For file outputs, the maximum number of bytes to write in a single system call.
                             [nargs=0..1] [default: 262144]
  --max-iovecs             For UDP inputs, the maximum number of datagrams that can be received in a single
                           recvmmsg() call. Regardless of value, will be limited to sysconf(_SC_IOV_MAX).
                           0 means use the maximum possible. [nargs=0..1] [default: 0]
  -a, --append             For file outputs, append to the file instead of truncating it. 
  --no-handle-signals      Do not intercept SIGINT and SIGUSR1.  By default, SIGINT will cleanly drain
                           buffered datagrams before shutting down, and SIGUSR1 will cause a brief summary
                           of progress statistics to be printed to stderr. 
  -l, --log-level          Set the logging level. Choices are ('debug', 'info', 'warning', 'error',
                           or 'critical'). [nargs=0..1] [default: "warning"]
  --tb                     On exception, display full stack traceback. 

Examples:
    dg-cat udp://9876
        Listen on UDP port 9876 and copy datagrams to stdout.
```

Known issues and limitations
----------------------------

* dg-cat requires a linux kernel that supports recvmmsg().
* Has only been tested on Ubuntu 22.04.

Getting help
------------

Please report any problems/issues [here](https://github.com/mckelvie-org/dg-cat/issues).

Contributing
------------

Pull requests welcome.

License
-------

dg-cat is distributed under the terms of the [MIT License](https://opensource.org/licenses/MIT).
The license applies to this file and other files in the [GitHub repository](http://github.com/mckelvie-org/dg-cat) hosting this file.

Authors and history
---------------------------

The author of dg-cat is [Sam McKelvie](https://github.com/sammck).
