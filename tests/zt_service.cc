#include "zt_service.h"

#include "zts_event_connector.h"
#include "zts_exception.h"

namespace standby_network
{

ZT_Service::ZT_Service(const char *path, uint16_t port) :
    node_r(false),
    network_r(false)
{
    ZTS_EventConnector::subscribe(ZTS_EVENT_NODE_ONLINE, node_ready_callback_id, [this](const zts_callback_msg *msg) -> void
    {
        node_r = true;
    });
    ZTS_EventConnector::subscribe(ZTS_EVENT_NETWORK_READY_IP6, network_ready_callback_id, [this](const zts_callback_msg *msg) -> void
    {
        network_r = true;
    });

    if(zts_start(path, ZTS_EventConnector::zts_callback, port) != ZTS_ERR_OK)
    {
        throw ZTS_Exception("Couldn't start ZeroTier service");
    }

    while(!node_r) { zts_delay_ms(50); }
}

ZT_Service::~ZT_Service() noexcept
{
    zts_stop();
}

auto ZT_Service::join(uint64_t nwid) const -> void
{
    if(zts_join(nwid) != ZTS_ERR_OK)
    {
        throw ZTS_Exception("Couldn't join network");
    }

    while(!network_r) { zts_delay_ms(50); }
}

auto ZT_Service::leave(uint64_t nwid) const -> void
{
    if(zts_leave(nwid) != ZTS_ERR_OK)
    {
        throw ZTS_Exception("Couldn't leave network");
    }
}

} // namespace standby_network