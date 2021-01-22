#include "zts_ip6_udp_socket.h"

#include <cstring>
#include <iostream>

#include "zts_event_connector.h"
#include "zts_exception.h"

namespace standby_network
{

auto str_to_ip6(const char *addr, uint32_t port = (uint32_t)-1) -> zts_sockaddr_in6
{
    zts_sockaddr_in6 a;
    a.sin6_family = ZTS_AF_INET6;
    if(port != (uint32_t)-1)
    {
        a.sin6_port = zts_htons(port);
    }
    int err = zts_inet_pton(ZTS_AF_INET6, addr, &a.sin6_addr);
    if(err < 0)
    {
        throw ZTS_Exception(std::string("Couldn't convert string: ") + addr + " to sockaddr: err: " + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }

    return a;
}

ZTS_IP6_UDP_Socket::ZTS_IP6_UDP_Socket()
{
    int *sock_p = new int;
    *sock_p = zts_socket(ZTS_AF_INET6, ZTS_SOCK_DGRAM, 0);
    if(*sock_p < 0)
    {
        int err = *sock_p;
        delete sock_p;
        throw ZTS_Exception(std::string("Couldn't create socket: err: ") + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }
    socket = std::shared_ptr<int>(sock_p, [](int *sock_p)
    {
        zts_close(*sock_p);
        delete sock_p;
    });
}

ZTS_IP6_UDP_Socket::ZTS_IP6_UDP_Socket(const ZTS_IP6_UDP_Socket &copy) noexcept :
    socket(copy.socket)
{

}

ZTS_IP6_UDP_Socket::ZTS_IP6_UDP_Socket(ZTS_IP6_UDP_Socket &&move) noexcept
{
    std::swap(socket, move.socket);
}

auto ZTS_IP6_UDP_Socket::operator=(const ZTS_IP6_UDP_Socket &copy) noexcept -> ZTS_IP6_UDP_Socket &
{
    socket = copy.socket;

    return *this;
}

auto ZTS_IP6_UDP_Socket::operator=(ZTS_IP6_UDP_Socket &&move) noexcept -> ZTS_IP6_UDP_Socket &
{
    std::swap(socket, move.socket);

    return *this;
}

auto ZTS_IP6_UDP_Socket::set_non_block() const -> void
{
    int err = zts_fcntl(*socket, ZTS_F_SETFL, ZTS_O_NONBLOCK);
    if(err < 0)
    {
        throw ZTS_Exception(std::string("Couldn't set socket to be non-blockin: err: ") + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }
}

auto ZTS_IP6_UDP_Socket::set_block() const -> void
{
    int flags = zts_fcntl(*socket, ZTS_F_GETFL, 0);
    int err = zts_fcntl(*socket, ZTS_F_SETFL, flags & (~ZTS_O_NONBLOCK));
    if(err < 0)
    {
        throw ZTS_Exception(std::string("Couldn't set socket to be blocking: err: ") + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }
}

auto ZTS_IP6_UDP_Socket::bind(const char *addr, const uint16_t port) const -> void
{
    zts_sockaddr_in6 sockaddr;
    sockaddr = str_to_ip6(addr, port);
    int err = zts_bind(*socket, (zts_sockaddr *)&sockaddr, sizeof(zts_sockaddr_in6));
    if(err < 0)
    {
        throw ZTS_Exception(std::string("Couldn't bind socket to address: ") + addr + ": err: " + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }
}

auto ZTS_IP6_UDP_Socket::sendto(const char *addr, const uint16_t port, const ByteArray &data) const -> const int
{
    zts_sockaddr_in6 sockaddr;
    sockaddr = str_to_ip6(addr, port);
    
    return sendto(&sockaddr, data);
}

auto ZTS_IP6_UDP_Socket::sendto(const zts_sockaddr_in6 *addr, const ByteArray &data) const -> const int
{
    int err = zts_sendto(*socket, const_cast<ByteArray *>(&data)->get(), data.size(), 0, (zts_sockaddr *)addr, sizeof(zts_sockaddr_in6));
    if(err < 0)
    {
        throw ZTS_Exception(std::string("Couldn't send: err: ") + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }

    return err;
}

auto ZTS_IP6_UDP_Socket::recvfrom(zts_sockaddr_in6 *remote_addr, zts_socklen_t *remote_addr_len) const -> const ByteArray
{
    constexpr int buf_len = 10000;
    char buf[buf_len];
    int err = zts_recvfrom(*socket, buf, buf_len, 0, (zts_sockaddr *)remote_addr, remote_addr_len);
    if(err < 0)
    {
        throw ZTS_Exception(std::string("Couldn't receive: err: ") + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }

    return ByteArray((uint8_t *)buf, (uint64_t)err);
}

auto ZTS_IP6_UDP_Socket::poll(const uint16_t directions, const int32_t timeout) const -> const uint16_t
{
    zts_pollfd fds[1];
    fds[0].fd = *socket;
    fds[0].events = directions;
    int err = zts_poll(fds, 1, timeout);
    if(err < 0)
    {
        throw ZTS_Exception(std::string("zts_poll returned an error: ") + std::to_string(err) + " zts_errno: " + std::to_string(zts_errno));
    }
    if(!err)
    {
        return err;
    }
    uint16_t ret_val = 0;
    if(fds[0].revents & static_cast<uint16_t>(PollDirection::SEND))
    {
        ret_val |= static_cast<uint16_t>(PollDirection::SEND);
    }
    if(fds[0].revents & static_cast<uint16_t>(PollDirection::RECV))
    {
        ret_val |= static_cast<uint16_t>(PollDirection::RECV);
    }

    return ret_val;
}

auto ZTS_IP6_UDP_Socket::create_direct_link(const uint64_t nwid, const uint64_t node_id, const uint16_t port) const -> const bool
{
    zts_sockaddr_in6 remote_addr;
    if(zts_get_rfc4193_addr((zts_sockaddr_storage *)&remote_addr, nwid, node_id) != ZTS_ERR_OK)
    {
        throw ZTS_Exception("Couldn't create address from nwid and node_id");
    }
    remote_addr.sin6_family = ZTS_AF_INET6;
    remote_addr.sin6_port = zts_htons(port);
    char ip[ZTS_INET6_ADDRSTRLEN];
    zts_inet_ntop(ZTS_AF_INET6, &remote_addr.sin6_addr, ip, ZTS_INET6_ADDRSTRLEN);
    std::cout << "ip " << ip << std::endl;
    bool direct_connection_established = false;

    const char *callback_name = "zts_ip6_sock_create_direct_link";
    ZTS_EventConnector::subscribe(ZTS_EVENT_PEER_DIRECT, std::make_pair(callback_name, [&direct_connection_established, &node_id](const zts_callback_msg *msg) -> void
    {
        if(msg->peer->address == node_id)
        {
            direct_connection_established = true;
        }
    }));

    int acked = 0;

    int i = 0;
    for(; !direct_connection_established && i < 1000; i++)
    {
        // Create an array of bytes containing the string with the trailing zero
        ByteArray ping("ping");
        int sent = sendto(&remote_addr, ping);
        zts_sockaddr_in6 r;
        zts_socklen_t socklen = sizeof(r);
        zts_pollfd fds[1];
        fds[0].fd = *socket;
        fds[0].events = ZTS_POLLIN;
        // using poll here because we need the timeout. This way
        // we don't have to tinker with the sockets timeout or blocking behavior
        int err = zts_poll(fds, 1, 200);
        if(err == 1)
        {
            ByteArray msg = recvfrom(&r, &socklen);
            int match = memcmp(&r.sin6_addr, &remote_addr.sin6_addr, sizeof(r.sin6_addr));
            ByteArray pong("pong");
            if(!match && msg == pong)
            {
                acked++;
            }
        }
    }
    
    ZTS_EventConnector::unsubscribe(ZTS_EVENT_PEER_DIRECT, callback_name);

    if(!direct_connection_established && acked >= 850)
    {
        direct_connection_established = true;
    }

    std::cout << "number of pings " << i << " acked " << acked << std::endl;

    return direct_connection_established;
}

} // namespace standby_network