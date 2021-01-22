#ifndef _ZT_SERVICE_H_
#define _ZT_SERVICE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

#include <ZeroTierSockets.h>

namespace standby_network
{

/**
 * @brief a C++ wrapper around the ZeroTier service
 * It's very minimal, very unfashioned two purposes only:
 * reduce boilerplate code and RAII
 */

class ZT_Service
{
public:
    /**
     * @brief start the ZeroTier service with the specified arguments
     * @param path : const char * - the path to the files where the ZeroTier service stores it's runtime information
     * @param port : uint16_t - the port for the ZeroTier service to communicate on
     */
    ZT_Service(const char *path, uint16_t port = 9003);
    ZT_Service(const ZT_Service &) = delete;
    ZT_Service(ZT_Service &&) = delete;
    ~ZT_Service() noexcept;

public:
    auto operator=(const ZT_Service &) -> ZT_Service & = delete;
    auto operator=(ZT_Service &&) -> ZT_Service & = delete;

public:
    /**
     * @brief join a ZeroTier network
     * blocks until the join is done
     * @param nwid : uint64_t - the id of the ZeroTier network
     * @throw ZTS_Exception - if can't join the network (no details)
     */
    auto join(uint64_t nwid) const -> void;
    /**
     * @brief leave a ZeroTier newtork
     * @param nwid : uint64_t - the id of the ZeroTier network
     * @throw ZTS_Exception - if can't leave the network (no details)
     */
    auto leave(uint64_t nwid) const -> void;

private:
    std::atomic_bool node_r;
    std::atomic_bool network_r;
    const char *node_ready_callback_id = "zt_service_node_r";
    const char *network_ready_callback_id = "zt_service_network_r";
};

} // namespace standby_network

#endif // _ZT_SERVICE_H_