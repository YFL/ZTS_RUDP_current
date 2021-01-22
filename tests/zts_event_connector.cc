#include "zts_event_connector.h"

#include <cstring>

namespace standby_network
{

std::map<int, std::list<std::pair<std::string, callback_function_t>>> ZTS_EventConnector::callbacks;

auto ZTS_EventConnector::subscribe(int zts_event, const std::string &id, const callback_function_t &func) -> void
{
    ZTS_EventConnector::subscribe(zts_event, std::make_pair(id, func));
}

auto ZTS_EventConnector::subscribe(int zts_event, const std::pair<std::string, callback_function_t> &func_pair) -> void
{
    try
    {
        auto list = ZTS_EventConnector::callbacks.at(zts_event);
        list.push_back(func_pair);
    }
    catch(const std::out_of_range &)
    {
        ZTS_EventConnector::callbacks[zts_event] = std::list<std::pair<std::string, callback_function_t>>();
        ZTS_EventConnector::callbacks[zts_event].push_back(func_pair);
    }
}

auto ZTS_EventConnector::unsubscribe(int zts_event, const std::string &id) -> void
{
    try
    {
        auto list = ZTS_EventConnector::callbacks.at(zts_event);
        std::remove_if(list.begin(), list.end(), [&id](auto &p) -> bool
        {
            if(p.first == id)
            {
                return true;
            }

            return false;
        });
    }
    catch(const std::out_of_range&)
    {
        return;
    }
}

auto ZTS_EventConnector::zts_callback(void *msg) -> void
{
    zts_callback_msg *m = static_cast<zts_callback_msg *>(msg);
    for(auto &pair : ZTS_EventConnector::callbacks[m->eventCode])
    {
        pair.second(m);
    }
}

} // namespace standby_network