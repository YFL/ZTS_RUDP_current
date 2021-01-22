#include "zts_ip6_rudp_socket.h"

#include "../Reliable-UDP_ztsified/event.h"

#include "zts_exception.h"

namespace standby_network
{

static std::packaged_task<void()> empty_task([](){});

std::mutex ZTS_IP6_RUDP_Socket::data_queue_mut;
std::map<rudp_socket_t, std::map<std::string, std::queue<ByteArray>>> ZTS_IP6_RUDP_Socket::data_queue;
std::future<void> ZTS_IP6_RUDP_Socket::is_eventloop_done = empty_task.get_future();
std::thread ZTS_IP6_RUDP_Socket::eventloop_thread = std::thread(std::move(empty_task));
std::mutex ZTS_IP6_RUDP_Socket::socket_count_mut;
uint32_t ZTS_IP6_RUDP_Socket::socket_count = 0;

auto try_addr_to_str(const zts_sockaddr_in6 &addr) -> std::string
{
    char dst_buf[ZTS_INET6_ADDRSTRLEN];
    const char *err = zts_inet_ntop(ZTS_AF_INET6, &addr.sin6_addr, dst_buf, ZTS_INET6_ADDRSTRLEN);
    if(!err)
    {
        throw ZTS_Exception("Wrong address supplied: conversion to string failed");
    }

    return std::string(dst_buf);
}

ZTS_IP6_RUDP_Socket::ZTS_IP6_RUDP_Socket(uint16_t port) :
    socket(rudp_socket(port)),
    port(port)
{
    if(socket == (rudp_socket_t)-1)
    {
        throw ZTS_Exception("Couldn't create RUDP socket");
    }
    rudp_recvfrom_handler(socket, recv_callback);
    rudp_event_handler(socket, event_callback);

    std::lock_guard socket_count_lg(socket_count_mut);
    socket_count++;

    std::unique_lock data_q_ul(data_queue_mut);
    data_queue[socket] = std::map<std::string, std::queue<ByteArray>>();
    data_q_ul.unlock();

    /*
    Now here comes the problematic part. So we have an eventloop() in the underlying library which works something like this:
    it runs until there are subscribed event handlers on the underlying file descriptors (unix sockets) or timer-event-handlers.
    That means if only one sockets existed and it has been destructed, the eventloop stops. So we have to restart it.
    We do this by using std::future and std::packaged_task because the eventloop has to be run on a separate thread for our
    usecase. Why? The underlying RUDP library is event based and written in C. We want our library to be consistent with UDP thus we need non-blocking
    on demand API and RAII. Maybe it would have been faster to implement it in an eventbased fashion without doing any C++ RAII wrapping,
    but than again, lua bindings for this should be done differently then for the UDP and that way we created even more inconsistency.
    My goal was to eliminate that.
    */
    auto status = is_eventloop_done.wait_for(std::chrono::milliseconds(0));
    if(status == std::future_status::ready)
    {
        std::packaged_task<void()> task([]() { eventloop(); return; });
        is_eventloop_done = task.get_future();
        eventloop_thread.join();
        eventloop_thread = std::thread(std::move(task));
    }
}

ZTS_IP6_RUDP_Socket::ZTS_IP6_RUDP_Socket(ZTS_IP6_RUDP_Socket &&move) :
    socket(move.socket),
    port(move.port)
{
    move.socket = nullptr;
    move.port = 0;
}

ZTS_IP6_RUDP_Socket::~ZTS_IP6_RUDP_Socket()
{
    /*
    Here we request a close on the underlying socket and wait for
    the eventloop to finish if there's no other sockets left open,
    just to make sure that the application is not terminating sooner
    then the eventloop
    */

    std::lock_guard socket_count_lg(socket_count_mut);
    rudp_close(socket);
    socket_count--;
    if(!socket_count)
    {
        eventloop_thread.join();
    }
}

auto ZTS_IP6_RUDP_Socket::operator=(ZTS_IP6_RUDP_Socket &&move) -> ZTS_IP6_RUDP_Socket &
{
    std::swap(socket, move.socket);
    std::swap(port, move.port);

    return *this;
}

auto ZTS_IP6_RUDP_Socket::sendto(const ByteArray &data, const zts_sockaddr_in6 &to) const -> int
{
    return rudp_sendto(socket, (void *)const_cast<ByteArray *>(&data)->get(), data.size(), (zts_sockaddr_in6 *)&to);
}

auto ZTS_IP6_RUDP_Socket::sendto(const ByteArray &data, const char *to, const uint16_t port) const -> int
{
    zts_sockaddr_in6 addr;
    addr.sin6_family = ZTS_AF_INET6;
    addr.sin6_port = zts_htons(port);
    int err = zts_inet_pton(ZTS_AF_INET6, to, &addr.sin6_addr);
    if(err <= 0)
    {
        throw ZTS_Exception("Couldn't convert address from string to zts_sockaddr_in6");
    }

    return sendto(data, addr);
}

auto ZTS_IP6_RUDP_Socket::recvfrom(const zts_sockaddr_in6 &from) const -> const std::optional<ByteArray>
{
    std::lock_guard data_q_lg(data_queue_mut);
    try
    {
        std::string key = try_addr_to_str(from);
        auto addr_q_map = data_queue.at(socket);
        auto q = addr_q_map.at(key);
        auto msg = std::move(q.front());
        q.pop();

        return msg;
    }
    catch(const std::out_of_range &)
    {
        return std::nullopt;
    }
}

auto ZTS_IP6_RUDP_Socket::recvfrom(const char *from) const -> const std::optional<ByteArray>
{
    zts_sockaddr_in6 addr;
    addr.sin6_family = ZTS_AF_INET6;
    addr.sin6_port = 0;
    int err = zts_inet_pton(ZTS_AF_INET6, from, &addr.sin6_addr);
    if(err <= 0)
    {
        throw ZTS_Exception("Couldn't convert address from string to zts_sockaddr_in6");
    }

    return recvfrom(addr);
}

auto ZTS_IP6_RUDP_Socket::any_socket_open() -> bool
{
    std::lock_guard sockets_lg(socket_count_mut);

    return socket_count > 0;
}

auto ZTS_IP6_RUDP_Socket::recv_callback(rudp_socket_t socket, zts_sockaddr_in6 *from, char *data, int len) -> int
{
    std::lock_guard data_q_lg(data_queue_mut);
    zts_sockaddr_in6 key = *from;
    key.sin6_port = 0;
    data_queue[socket][try_addr_to_str(*from)].push(ByteArray((uint8_t *)data, (uint64_t)len));

    return 0;
}

auto ZTS_IP6_RUDP_Socket::event_callback(rudp_socket_t socket, rudp_event_t event_type, zts_sockaddr_in6 *to) -> int
{
    if(event_type == RUDP_EVENT_TIMEOUT)
    {
        // dunno
    }
    else
    {
        // dunno
    }

    return 0;
}

} // namespace standby_network