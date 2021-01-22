#include <iostream>

#include "../../../libzt_playground/libzt/include/ZeroTierSockets.h"

#include "../zt_service.h"
#include "../zts_ip6_udp_socket.h"
#include "../zts_ip6_rudp_socket.h"

constexpr uint64_t other_id = 0x953daa4fca;
constexpr uint64_t nwid = 0x88503383905880e5;
/* 
constexpr uint16_t local_udp_port = 9001;
constexpr uint16_t remote_udp_port = 9000;
 */
constexpr uint16_t local_rudp_port = 9003;
constexpr uint16_t remote_rudp_port = 9002;

auto main() -> int
{
    using namespace standby_network;

    try
    {
        ZT_Service zt("./zt_runtime");
        zt.join(nwid);
        /* 
        ZTS_IP6_UDP_Socket sock(ZTS_SOCK_DGRAM, 0);
        sock.bind("::", local_udp_port);

        zts_sockaddr_in6 remote_udp;
        zts_socklen_t remote_udp_len = sizeof(remote_udp);

        ByteArray msg(nullptr, (uint64_t)0);
        int i = 0;
        bool received_non_ping = false;
        for(; i < 1000; i++)
        {
            msg = sock.recvfrom(&remote_udp, &remote_udp_len);
            remote_udp.sin6_family = ZTS_AF_INET6;
            remote_udp.sin6_port = zts_htons(remote_udp_port);
            remote_udp_len = sizeof(remote_udp);
            // create a byte array containing the string with the trailing zero
            ByteArray ping("ping");
            if(msg != ping)
            {
                received_non_ping = true;
                break;
            }
            ByteArray pong("pong");
            sock.sendto(&remote_udp, pong);
        }
        if(!received_non_ping)
        {
            msg = sock.recvfrom(&remote_udp, &remote_udp_len);
            remote_udp.sin6_family = ZTS_AF_INET6;
            remote_udp.sin6_port = zts_htons(remote_udp_port);
        }
        std::cout << "received: " << static_cast<int>(msg[0]) << std::endl;
 */
        uint8_t data[] = {1/* static_cast<uint8_t>(msg[0] + 1) */};
        ByteArray ba(data, 1ull);
        /* 
        sock.sendto(&remote_udp, ba);
 */
        ZTS_IP6_RUDP_Socket rudp_sock(local_rudp_port);
        zts_sockaddr_in6 remote_rudp;
        remote_rudp.sin6_family = ZTS_AF_INET6;
        remote_rudp.sin6_port = zts_htons(remote_rudp_port);
        int err = zts_get_rfc4193_addr((zts_sockaddr_storage *)&remote_rudp, nwid, other_id);
        if(err != ZTS_ERR_OK)
        {
            throw ZTS_Exception("remote address couldn't be computed by zts_get_rfc4193_addr()");
        }
        rudp_sock.sendto(ba, remote_rudp);
    }
    catch(const ZTS_Exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}