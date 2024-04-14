#include "dg_cat/dg_cat.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <argparse/argparse.hpp>
#include <algorithm>
#include <cctype>
#include <string>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/expressions.hpp>
#include <boost/algorithm/string.hpp>

namespace logging = boost::log;

logging::trivial::severity_level str_to_severity(const std::string& str) {
    auto lc_str = boost::algorithm::to_lower_copy(str);

    logging::trivial::severity_level severity;
    if (!logging::trivial::from_string(lc_str.c_str(), lc_str.size(), severity)) {
        throw std::runtime_error(std::string("Invalid log level: ") + str);
    }
    return severity;
}

void init_logging(const std::string& log_level) {
    // logging::register_simple_formatter_factory<logging::trivial::severity_level, char>("Severity");

    auto severity = str_to_severity(log_level);

   logging::core::get()->set_filter(        
      logging::trivial::severity >= severity     
   ); 

    /*
    logging::add_console_log(
        std::cerr,
        logging::keywords::format = "[%TimeStamp%] [%Severity%] %Message%",
        logging::keywords::auto_flush = true,
        logging::keywords::severity = severity
    );
    */

    // logging::add_common_attributes();
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser parser("dg-cat", "0.1");

    parser.add_description(
        "Copy between datagram streams while preserving message lengths.\n\n"
        "A simple command-line utility that reads datagrams from a random generator, UDP socket,\n"
        "file, or pipe, and forwards them to another UDP socket, file, or pipe. For files and\n"
        "pipes, each datagram is prefixed with a 4-byte length field in network byte order\n"
        "(big-endian)."
    );

    parser.add_argument("-d", "--max-datagram-size")
        .default_value(DEFAULT_MAX_DATAGRAM_SIZE)
        .help(std::string(
            "For UDP input, the per-datagram buffer size. Datagrams larger than this are discarded.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_DATAGRAM_SIZE) + "."
            );

    parser.add_argument("-b", "--max-backlog")
        .default_value(DEFAULT_MAX_BACKLOG)
        .help(std::string(
            "The maximum number of bytes (including 4-byte per-datagram length prefixes) to buffer\n"
            "before stalling input. For UDP input, stalling input may cause datagrams to be dropped.\n ")
         // + "Default is " + std::to_string(DEFAULT_MAX_BACKLOG) + "."
        );

    parser.add_argument("-p", "--polling-interval")
        .default_value(DEFAULT_POLLING_INTERVAL)
        .help(std::string(
            "For UDP sources, the low-level timeout on recv(), in seconds. This is the maximum latency\n"
            "between a shutdown condition and shutting down.")
         // + " Default is " + std::to_string(DEFAULT_POLLING_INTERVAL) + " seconds."
        );

    parser.add_argument("-t", "--eof-timeout")
        .default_value(DEFAULT_EOF_TIMEOUT_SECS)
        .help(std::string(
            "For UDP sources, a number of seconds with no datagrams received that should be interpreted\n"
            "as an EOF. If <= 0.0, allows unlimited time between datagrams (copying will not terminate\n"
            "until a signal is received).")
         // + " Default is " + std::to_string(DEFAULT_EOF_TIMEOUT_SECS) + "."
        );

    parser.add_argument("--start-timeout")
        .default_value(-1.0)
        .help(std::string(
            "For UDP sources, a number of seconds to wait for the first datagram before ending with an\n"
            "empty stream. If < 0, the value for --eof-timeout will be used. If 0.0, will wait forever\n"
            "for the first datagram. By default, the value for --eof-timeout is used.\n ")
        );

    parser.add_argument("-r", "--max-datagram-rate")
        .default_value(DEFAULT_MAX_DATAGRAM_RATE)
        .help(std::string(
            "For UDP outputs, the maximun datagrams per second to send. If <= 0.0, does not limit\n"
            "datagram send rate (may cause datagrams to be dropped by receiver or enroute).\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_DATAGRAM_RATE) + "."
        );

    parser.add_argument("-n", "--max-datagrams")
        .default_value(DEFAULT_MAX_DATAGRAMS)
        .help(std::string(
            "Stop after copying the specified number of datagrams. If <= 0, copy all datagrams.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_DATAGRAMS) + "."
        );

    parser.add_argument("-w", "--max-write-size")
        .default_value(DEFAULT_MAX_WRITE_SIZE)
        .help(std::string(
            "For file outputs, the maximum number of bytes to write in a single system call.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_WRITE_SIZE) + "."
        );

    parser.add_argument("-a", "--append")
        .flag()
        .help(std::string(
            "For file outputs, append to the file instead of truncating it.")
        );

    parser.add_argument("-l", "--log-level")
        .default_value(std::string("warning"))
        .choices("DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL", "debug", "info", "warning", "error", "critical")
        .help(std::string(
              "Set the logging level. Choices are ('debug', 'info', 'warning', 'error',\n"
              "or 'critical').")
           // + " Default is 'warning'."
        );

    parser.add_argument("src")
        .default_value(std::string("stdin"))
        .help("The source of datagrams. Can be one of: \n"
                "    \"<filename>\"\n"
                "    \"file://<filename>\"\n"
                "    \"udp://<local-port\"\n"
                "    \"udp://<local-bind-addr>:<local-port>\"\n"
                "    \"random://[n=<num-datagrams>][:min=<min-bytes>][:max=<max-bytes>][:seed=<seed>]\"\n"
                "    \"stdin\"\n"
                "    \"-\"        (alias for stdin)\n"
                "If omitted, \"stdin\" is used.");

    parser.add_argument("dst")
        .default_value(std::string("stdout"))
        .help("The destination of datagrams. Can be one of: \n"
              "    \"<filename>\"\n"
              "    \"file://<filename>\"\n"
              "    \"udp://<remote-addr>:<remote-port>\"\n"
              "    \"stdout\"\n"
              "    \"-\"       (alias for stdout)\n"
              "If omitted, stdout is used.");

    parser.add_epilog(
            "Examples:\n"
            "    dg-cat udp://9876\n"
            "        Listen on UDP port 9876 and copy datagrams to stdout.\n"
        );

    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    auto log_level_s = parser.get<std::string>("log-level");
    init_logging(log_level_s);

    auto bufsize = parser.get<size_t>("max-datagram-size");
    auto max_backlog = parser.get<size_t>("max-backlog");
    auto polling_interval = parser.get<double>("polling-interval");
    auto eof_timeout = parser.get<double>("eof-timeout");
    auto start_timeout = parser.get<double>("start-timeout");
    if (start_timeout < 0.0) {
        start_timeout = eof_timeout;
    }
    auto max_datagram_rate = parser.get<double>("max-datagram-rate");
    auto max_datagrams = parser.get<uint64_t>("max-datagrams");
    auto max_write_size = parser.get<size_t>("max-write-size");
    auto append = parser.get<bool>("append");
    auto src = parser.get<std::string>("src");
    auto dst = parser.get<std::string>("dst");

    DgCatConfig config(
        bufsize,
        max_backlog,
        polling_interval,
        eof_timeout,
        start_timeout,
        max_datagram_rate,
        max_datagrams,
        max_write_size,
        append
    );

    BOOST_LOG_TRIVIAL(debug) <<
        "Starting dg-cat with " << config.to_string() << "\n";
    

# if 0
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

#endif

    return 0;
}
