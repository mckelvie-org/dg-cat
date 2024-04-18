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
        .scan<'u', size_t>()
        .help(std::string(
            "For UDP input, the per-datagram buffer size. Datagrams larger than this are discarded.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_DATAGRAM_SIZE) + "."
            );

    parser.add_argument("-b", "--max-backlog")
        .default_value(DEFAULT_MAX_BACKLOG)
        .scan<'u', size_t>()
        .help(std::string(
            "The maximum number of bytes (including 4-byte per-datagram length prefixes) to buffer\n"
            "before stalling input. For UDP input, stalling input may cause datagrams to be dropped.\n ")
         // + "Default is " + std::to_string(DEFAULT_MAX_BACKLOG) + "."
        );

    parser.add_argument("-p", "--polling-interval")
        .default_value(DEFAULT_POLLING_INTERVAL)
        .scan<'g', double>()
        .help(std::string(
            "For UDP sources, the low-level timeout on recv(), in seconds. This is the maximum latency\n"
            "between a shutdown condition and shutting down.")
         // + " Default is " + std::to_string(DEFAULT_POLLING_INTERVAL) + " seconds."
        );

    parser.add_argument("-t", "--eof-timeout")
        .default_value(DEFAULT_EOF_TIMEOUT_SECS)
        .scan<'g', double>()
        .help(std::string(
            "For UDP sources, a number of seconds with no datagrams received that should be interpreted\n"
            "as an EOF. If <= 0.0, allows unlimited time between datagrams (copying will not terminate\n"
            "until a signal is received).")
         // + " Default is " + std::to_string(DEFAULT_EOF_TIMEOUT_SECS) + "."
        );

    parser.add_argument("--start-timeout")
        .default_value(-1.0)
        .scan<'g', double>()
        .help(std::string(
            "For UDP sources, a number of seconds to wait for the first datagram before ending with an\n"
            "empty stream. If < 0, the value for --eof-timeout will be used. If 0.0, will wait forever\n"
            "for the first datagram. By default, the value for --eof-timeout is used.\n ")
        );

    parser.add_argument("-r", "--max-datagram-rate")
        .default_value(DEFAULT_MAX_DATAGRAM_RATE)
        .scan<'g', double>()
        .help(std::string(
            "For UDP outputs, the maximun datagrams per second to send. If <= 0.0, does not limit\n"
            "datagram send rate (may cause datagrams to be dropped by receiver or enroute).\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_DATAGRAM_RATE) + "."
        );

    parser.add_argument("-n", "--max-datagrams")
        .default_value(DEFAULT_MAX_DATAGRAMS)
        .scan<'u', uint64_t>()
        .help(std::string(
            "Stop after copying the specified number of datagrams. If 0, copy all datagrams.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_DATAGRAMS) + "."
        );

    parser.add_argument("-R", "--max-read-size")
        .default_value(DEFAULT_MAX_READ_SIZE)
        .scan<'u', size_t>()
        .help(std::string(
            "For file inputs, the maximum number of bytes to read in a single system call.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_READ_SIZE) + "."
        );

    parser.add_argument("-w", "--max-write-size")
        .default_value(DEFAULT_MAX_WRITE_SIZE)
        .scan<'u', size_t>()
        .help(std::string(
            "For file outputs, the maximum number of bytes to write in a single system call.\n ")
         // + " Default is " + std::to_string(DEFAULT_MAX_WRITE_SIZE) + "."
        );

    parser.add_argument("--max-iovecs")
        .default_value(DEFAULT_MAX_IOVECS)
        .scan<'u', size_t>()
        .help(std::string(
            "For UDP inputs, the maximum number of datagrams that can be received in a single\n"
            "recvmmsg() call. Regardless of value, will be limited to sysconf(_SC_IOV_MAX).\n"
            "0 means use the maximum possible.")
         // + " Default is " + std::to_string(DEFAULT_MAX_IOVECS) + "."
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
    auto max_read_size = parser.get<size_t>("max-read-size");
    auto max_write_size = parser.get<size_t>("max-write-size");
    auto max_iovecs = parser.get<size_t>("max-iovecs");
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
        max_read_size,
        max_write_size,
        max_iovecs,
        append
    );

    BOOST_LOG_TRIVIAL(debug) <<
        "Starting dg-cat with " << config.to_string() << "\n";
    
    DatagramCopier copier(config, src, dst);

    copier.start();
    copier.wait();

    auto stats = copier.get_stats();

    std::cerr << "\nFinished: " << stats.brief_str() << std::endl;

    return 0;
}
