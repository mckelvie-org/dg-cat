#pragma once


// NOTE: This implementation does not use boost::asio, because it uses recvmmsg() which is not supported by boost::asio.
//       Also, to simplify distribution, we avoid any boost dependencies that require linking to boost libraries. The resulting executable is
//       self-contained and does not require any shared libraries other than libstdc++.

#include "version.hpp"
#include "constants.hpp"
#include "config.hpp"
#include "timespec_math.hpp"
#include "util.hpp"
#include "addrinfo.hpp"
#include "buffer_queue.hpp"
#include "no_block_file_writer.hpp"
#include "datagram_copier_stats.hpp"
#include "file_closer.hpp"
#include "datagram_copier.hpp"
