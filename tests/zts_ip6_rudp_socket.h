#ifndef _ZTS_IP6_RUDP_SOCKET_H_
#define _ZTS_IP6_RUDP_SOCKET_H_

#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

#include <stdint.h>

#include "../Reliable-UDP_ztsified/rudp_api.h"

#include "byte_array.h"

namespace standby_network
{

constexpr uint16_t RUDP_DEFAULT_PORT = 9001;

/**
 * @brief a C++ wrapper for a ZeroTier IPv6 socket for RUDP communication
 * It's API is simple sendto and recvfrom like with Berkeley sockets.
 * It uses the Reliable-UDP_ztsified library as RUDP protocol implementation.
 * It's not fully functional/buggy.
 * 
 * A socket of this type can not be copied but, moving it then returning it
 * solves the problems that arise.
 * 
 * It's not fully tested either.
 * 
 * The user should only be concerned about creating it and sending and/or receiving
 * through it, so the API is pretty straightforward, hence no documentation for the
 * individual methodes.
 */

class ZTS_IP6_RUDP_Socket
{
public:
    /**
     * @throw ZTS_Exception if can't create the socket
     */
    ZTS_IP6_RUDP_Socket(uint16_t port = RUDP_DEFAULT_PORT);
    ZTS_IP6_RUDP_Socket(const ZTS_IP6_RUDP_Socket &copy) = delete;
    ZTS_IP6_RUDP_Socket(ZTS_IP6_RUDP_Socket &&move);
    /**
     * @brief if there are no other sockets open when this is called, it blocks until this socket is closed properly
     * This means if there is any pending operation on this socket, that operation has to finish first.
     */
    ~ZTS_IP6_RUDP_Socket();

public:
    auto operator=(const ZTS_IP6_RUDP_Socket &copy) -> ZTS_IP6_RUDP_Socket & = delete;
    auto operator=(ZTS_IP6_RUDP_Socket &&move) -> ZTS_IP6_RUDP_Socket &;

public:
    auto sendto(const ByteArray &data, const zts_sockaddr_in6 &to) const -> int;
    /**
     * @throw ZTS_Exception if can't convert address and port to zts_sockaddr_in6
     */
    auto sendto(const ByteArray &data, const char *to, const uint16_t remote_port) const -> int;
    auto recvfrom(const zts_sockaddr_in6 &from) const -> const std::optional<ByteArray>;
    /**
     * @throw ZTS_Exception if can't convert address to zts_sockaddr_in6
     */
    auto recvfrom(const char *from) const -> const std::optional<ByteArray>;

public:
    /**
     * @brief checks whether is any RUDP socket open (not destructed) or not
     * The sockets are stored in global memory and are administrated by the
     * underlying library. This functions "queries the library" for the 
     * information.
     */
    static auto any_socket_open() -> bool;

private:
    rudp_socket_t socket;
    uint16_t port;

private:
    /**
     * @brief this is used as the callback function supplied to the underlying library for handling received messages
     * It's called in the constructor and it stores the data received in the data_queue which is really a map with
     * rudp sockets as keys and maps of string keys and ByteArray queue values as values. So the messages are stored
     * grouped by sockets for the sockets to be able to retrieve the messages that belong to them. Furthermore they are
     * grouped by senders for the sockets to be able to distinguish between the senders of the messages and stored in
     * queues so that they can be get in FIFO order
     */
    static auto recv_callback(rudp_socket_t socket, zts_sockaddr_in6 *from, char *data, int len) -> int;
    /**
     * @brief this is used as the calbback function for the timeout and close events of the underlying RUDP library
     * It does nothing for now. Didn't have the time to get there.
     */
    static auto event_callback(rudp_socket_t socket, rudp_event_t event_type, zts_sockaddr_in6 *to) -> int;

private:
    static std::mutex data_queue_mut;
    static std::map<rudp_socket_t, std::map<std::string, std::queue<ByteArray>>> data_queue;
    static std::future<void> is_eventloop_done;
    static std::thread eventloop_thread;

private:
    /**
     * This counter makes sure to join the eventloop thread
     * before application exit like so:
     * Every (non-move, non-copy) constructor call increases it
     * Every destructor call decreases it
     * If the decreased count == 0, then we join the thread
     */
    static std::mutex socket_count_mut;
    static uint32_t socket_count;
};

} // namespace standby_network

#endif // _ZTS_IP6_RUDP_SOCKET_H_