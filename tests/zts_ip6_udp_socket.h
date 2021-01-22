#ifndef _ZTS_IP6_UDP_SOCKET_H_
#define _ZTS_IP6_UDP_SOCKET_H_

#include <memory>

#include <ZeroTierSockets.h>

#include "byte_array.h"
#include "zts_exception.h"

namespace standby_network
{

enum class PollDirection : uint16_t
{
    RECV = ZTS_POLLIN,
    SEND = ZTS_POLLOUT
};

/**
 * @brief a C++ RAII wrapper for the ZeroTier IPv6 UDP socket
 * The API is the standard Berkeley socket API hence the lack of documentation
 * for the individual functions.
 */

class ZTS_IP6_UDP_Socket
{
public:
    ZTS_IP6_UDP_Socket();
    ZTS_IP6_UDP_Socket(const ZTS_IP6_UDP_Socket &copy) noexcept;
    ZTS_IP6_UDP_Socket(ZTS_IP6_UDP_Socket &&move) noexcept;
    ~ZTS_IP6_UDP_Socket() noexcept = default;

public:
    auto operator=(const ZTS_IP6_UDP_Socket &copy) noexcept -> ZTS_IP6_UDP_Socket &;
    auto operator=(ZTS_IP6_UDP_Socket &&move) noexcept -> ZTS_IP6_UDP_Socket &;

public:
    /**
     * @throw ZTS_Exception - if can't set socket to non-blocking
     */
    auto set_non_block() const -> void;
    /**
     * @throw ZTS_Exception - if can't set socket to blocking
     */
    auto set_block() const -> void;
    /**
     * @throw ZTS_Exception - if can't bind socket
     */
    auto bind(const char *addr, const uint16_t port) const -> void;
    /**
     * @throw ZTS_Exception - if can't send
     */
    auto sendto(const char *addr, const uint16_t port, const ByteArray &data) const -> const int;
    /**
     * @throw ZTS_Exception - if can't send
     */
    auto sendto(const zts_sockaddr_in6 *addr, const ByteArray &data) const -> const int;
    /**
     * @throw ZTS_Exception - if can't receive
     */
    auto recvfrom(zts_sockaddr_in6 *remote_addr, zts_socklen_t *remote_addr_len) const -> const ByteArray;
    /**
     * returns a uint8_t which is a bitflag variable;
     *  if directions & SEND -> ret_val & SEND means we can send via the socket
     *  if directions & SEND -> !(ret_val & SEND) means we can't send via the socket
     *  the same goes fot RECV
     *  SEND and RECV are PollDirection::SEND and PollDirection::RECV
     * 
     * @throw 
     */
    auto poll(const uint16_t directions, const int32_t timeout) const -> const uint16_t;

public:
    /**
     * @brief create a direct link with the other node for a stable communication
     * It sends a ByteArray("ping") to the other node and awaits a ByteArray("pong") back,
     * so the other side should be prepared for this action. The connection is considered direct if:
     *      we get a ZTS_EVENT_PEED_DIRECT event from the ZeroTier service
     * OR
     *      when 85 percent of the pings echoes back with a pong
     * @throw ZTS_Exception - if the address can't be created from the node_id and nwid
     */
    auto create_direct_link(const uint64_t nwid, const uint64_t node_id, const uint16_t port) const -> const bool;

private:
    std::shared_ptr<int> socket;
};

} // namespace standby_network

#endif // _ZTS_IP6_UDP_SOCKET_H_