#include "udp_capture/udp_capture.hpp"

#include <unistd.h>
#include <fcntl.h>

// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netdb.h>

// //#include <boost/asio.hpp>
// #include <boost/endian/conversion.hpp>
// #include <fstream>
// #include <iostream>
// #include <vector>
// #include <array>
// #include <thread>
// #include <mutex>
// #include <condition_variable>
// #include <exception>
// #include <time.h>
// #include <csignal>
// #include <memory>
// #include <chrono>
// #include <sys/uio.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <cassert>
// #include <cmath>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " [<local-bind-addr>:]<port> <output_file>\n";
        return 1;
    }

    size_t max_dg_size = 65535;
    size_t max_backlog = DEFAULT_MAX_BACKLOG;
    size_t max_write_size = DEFAULT_MAX_WRITE_SIZE;
    double first_datagram_timeout_secs = 60.0;
    double datagram_timeout_secs = 10.0;

    std::string addr_and_port(argv[1]);
    std::string output_file(argv[2]);

    size_t colon_pos = addr_and_port.rfind(':');
    std::string addr_str;
    uint16_t  port;
    if (colon_pos == std::string::npos) {
        addr_str = std::string("0.0.0.0");
        port = std::stoi(addr_and_port);
    } else {
        addr_str = addr_and_port.substr(0, colon_pos);
        port = std::stoi(addr_and_port.substr(colon_pos + 1));
    }


    AddrInfoList addrinfo_list(
        addr_str.c_str(),
        std::to_string(port).c_str(),
        AI_PASSIVE,
        AF_UNSPEC,
        SOCK_DGRAM
      );

    if (addrinfo_list.size() == 0) {
        throw std::runtime_error("No addresses found for " + addr_str + ":" + std::to_string(port));
    }

    for(auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
        auto& entry = *ai;
        std::cerr << "Addr=" << entry.addr_string() << " (" << entry->ai_addr << ") Family=" << entry->ai_family << " SockType=" << entry->ai_socktype << " Protocol=" << entry->ai_protocol << "\n";
    }
        

    int s = -1;
    AddrInfoList::Entry matching_entry;
    for(auto ai = addrinfo_list.begin(); ai != addrinfo_list.end(); ++ai) {
        auto& entry = *ai;
        s = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (s == -1) {
            continue;
        }

        if (bind(s, entry->ai_addr, entry->ai_addrlen) == 0) {
            matching_entry = entry;
            break;
        }

        close(s);
    }

    if (s == -1) {
        throw std::runtime_error("Could not bind socket to any addresses");
    }

    std::cerr << "Bound to " << matching_entry.addr_string() << ":" << port << "\n";

    {
        FileCloser file(::open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660));
        if (file.fd < 0) {
            throw std::system_error(errno, std::system_category(), "open() failed");
        }
        DatagramCopier copier(
            s,
            file.fd,
            max_dg_size,
            max_backlog,
            max_write_size,
            datagram_timeout_secs,
            first_datagram_timeout_secs
        );

        copier.copy();
        file.close();

        auto stats = copier.get_stats();

        std::cerr << "\nFinished copying datagrams\n";

        std::cerr << "Received " << stats.n_datagrams << " datagrams totaling " << stats.n_datagram_bytes << " datagram bytes (not including length prefixes)\n";
        std::cerr << "Discarded " << stats.n_datagrams_discarded << " datagrams\n";
        std::cerr << "Max clump size: " << stats.max_clump_size << " datagrams\n";
        std::cerr << "Min datagram size: " << stats.min_datagram_size << " bytes\n";
        std::cerr << "Max datagram size: " << stats.max_datagram_size << " bytes\n";
        std::cerr << "Mean datagram size: " << stats.mean_datagram_size() << " bytes\n";
        std::cerr << "Elapsed time: " << stats.elapsed_secs()  << " seconds\n";
        std::cerr << "Max backlog: " << stats.max_backlog_bytes << " bytes\n";
        std::cerr << "Throughput: " << stats.throughput_datagrams_per_sec() << " datagrams/second (" << stats.throughput_bytes_per_sec() << " bytes/second)\n";
    }

    return 0;
}
