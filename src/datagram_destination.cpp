#include "dg_cat/datagram_destination.hpp"
#include "dg_cat/file_datagram_destination.hpp"
#include "dg_cat/udp_datagram_destination.hpp"

std::unique_ptr<DatagramDestination> DatagramDestination::create(const DgCatConfig& config, const std::string& path)
{
    if (path.compare(0, 6, "udp://") == 0) {
        return UdpDatagramDestination::create(config, path);
    } else {
        return FileDatagramDestination::create(config, path);
    }
}
