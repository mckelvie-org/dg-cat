#include "dg_cat/datagram_destination.hpp"
#include "dg_cat/file_datagram_destination.hpp"

std::unique_ptr<DatagramDestination> DatagramDestination::create(const DgCatConfig& config, const std::string& path)
{
    if (path.compare(0, 7, "file://") == 0) {
        return FileDatagramDestination::create(config, path);
    } else {
        throw std::runtime_error("Unsupported protocol in path: " + path);
    }
}
