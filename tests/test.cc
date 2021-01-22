#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <gtest/gtest.h>

#include "../../libzt_playground/libzt/include/ZeroTierSockets.h"

#include "zt_service.h"
#include "zts_ip6_udp_socket.h"
#include "zts_ip6_rudp_socket.h"

constexpr uint64_t other_id = 0x8f738ba0af;
constexpr uint64_t nwid = 0x88503383905880e5;
/* 
constexpr uint16_t local_udp_port = 9000;
constexpr uint16_t remote_udp_port = 9001;
 */
constexpr uint16_t local_rudp_port = 9002;
constexpr uint16_t remote_rudp_port = 9003;
/* 
zts_sockaddr_in6 remote_udp_addr;
 */
zts_sockaddr_in6 remote_rudp_addr;
/* 
std::unique_ptr<standby_network::ZTS_IP6_UDP_Socket> sock;
 */
std::unique_ptr<standby_network::ZTS_IP6_RUDP_Socket> rudp_sock;
/* 
TEST(ByteArrayTests, StringTest)
{
    ByteArray ping("ping");
    ASSERT_EQ(ping.size(), strlen("ping") + 1);
    for(uint64_t i = 0; i < ping.size(); i++)
    {
        ASSERT_EQ(ping.get()[i], "ping"[i]);
    }

    ping = ByteArray("ping", false);
    ASSERT_EQ(ping.size(), strlen("ping"));

    for(uint64_t i = 0; i < ping.size(); i++)
    {
        ASSERT_EQ(ping.get()[i], "ping"[i]);
    }
}

TEST(ByteArrayTests, ArrayTest)
{
    constexpr int len = 10000;
    char array[len];
    strcpy(array, "ping");
    ByteArray ba((uint8_t *)array, (uint64_t)4);

    ASSERT_EQ(ba.size(), 4);

    for(uint64_t i = 0; i < ba.size(); i++)
    {
        ASSERT_EQ(ba.get()[i], array[i]);
    }
}

TEST(TransferTests, DirectLinkTest)
{
    using namespace standby_network;

    try
    {
        ASSERT_TRUE(sock->create_direct_link(nwid, other_id, remote_udp_port));
    }
    catch(const ZTS_Exception &e)
    {
        std::cerr << e.what() << std::endl;
        FAIL();
    }
}

TEST(TransferTests, JustATest)
{
    using namespace standby_network;

    try
    {
        char data[1] = {0};
        ByteArray ba(data, 1);
        sock->sendto(&remote_udp_addr, ba);
        ByteArray msg = sock->recvfrom(nullptr, nullptr);
        int superfluous_pongs = 0;
        while(!strcmp((const char *)msg.get(), "pong"))
        {
            msg = sock->recvfrom(nullptr, nullptr);
            superfluous_pongs++;
        }
        std::cout << "superfluous pongs " << superfluous_pongs << std::endl;
        ASSERT_EQ(msg[0], 1);
    }
    catch(const ZTS_Exception &e)
    {
        std::cerr << e.what() << std::endl;
        FAIL();
    }
}
 */
TEST(RUDPTest, RecvTest)
{
    using namespace standby_network;

    try
    {
        std::optional<ByteArray> msg = std::nullopt;
        while(!msg)
        {
            msg = rudp_sock->recvfrom(remote_rudp_addr);
        }
        std::cout << "msg got: " << msg.value().to_string() << std::endl;
    }
    catch(const ZTS_Exception &e)
    {
        std::cerr << e.what() << std::endl;
        FAIL();
    }
}

auto main() -> int
{
    using namespace standby_network;

    int test_result = 1;

    try
    {
        ZT_Service zt("./zt_runtime");
        zt.join(nwid);
/* 
        sock = std::make_unique<ZTS_IP6_UDP_Socket>();
        sock->bind("::", local_udp_port);
         */
        rudp_sock = std::make_unique<ZTS_IP6_RUDP_Socket>(local_rudp_port);
/* 
        zts_get_rfc4193_addr((zts_sockaddr_storage *)&remote_udp_addr, nwid, other_id);
        remote_udp_addr.sin6_family = ZTS_AF_INET6;
        remote_udp_addr.sin6_port = zts_htons(remote_udp_port);
         */
        zts_get_rfc4193_addr((zts_sockaddr_storage *)&remote_rudp_addr, nwid, other_id);
        remote_rudp_addr.sin6_family = ZTS_AF_INET6;
        remote_rudp_addr.sin6_port = zts_htons(remote_rudp_port);
        ::testing::InitGoogleTest();
        test_result = RUN_ALL_TESTS();
        /* auto rs = rudp_sock.release();
        delete rs;
        auto s = sock.release();
        delete s; */

        zts_delay_ms(5000);
    }
    catch(const ZTS_Exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    return test_result;
}