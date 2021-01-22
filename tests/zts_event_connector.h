#ifndef _ZTS_EVENT_CONNECTOR_H_
#define _ZTS_EVENT_CONNECTOR_H_

#include <functional>
#include <queue>
#include <map>
#include <mutex>
#include <thread>
#include <list>

#include <ZeroTierSockets.h>

namespace standby_network
{

using callback_function_t = std::function<void(const zts_callback_msg *)>;

/**
 * @brief a static class with methods to subscribe to and unsubscribe from ZeroTier events
 * It is used internally by ZT_Service to start the ZeroTier service and attach the necessary
 * event handlers
 */

class ZTS_EventConnector
{
private:
    ZTS_EventConnector() = delete;
    ~ZTS_EventConnector() = delete;

public:
    /**
     * @brief subscribe to event <zts_event> with callback <func>
     * @param zts_event : int - the event to subscribe to like ZTS_EVENT_NETWORK_READY
     * @param id : const std::string & - the id of the callback. It is used when unsubscribing. The callbacks are grouped by events so every id has to be UNIQUE for the given event
     * @param func : const callback_function_t & - the callback to use when event occures
     */
    static auto subscribe(int zts_event, const std::string &id, const callback_function_t &func) -> void;
    /**
     * @brief same as subscribe with 3 arguments, only here a pair is created from the id and the callback
     */
    static auto subscribe(int zts_event, const std::pair<std::string, callback_function_t> &func_pair) -> void;
    /**
     * @brief unsubscribe from event <zts_event> the callback with id <id>
     * @param zts_event : int - the event we subscribed to
     * @param id : const std::string & - the id of the callback to remove the subscription for
     */
    static auto unsubscribe(int zts_event, const std::string &id) -> void;

public:
    /**
     * @brief this is the function that can be supplied to zts_start as the callback
     */
    static auto zts_callback(void *msg) -> void;

private:
    static std::map<int, std::list<std::pair<std::string, callback_function_t>>> callbacks;
};

} // namespace standby_network

#endif // _ZT_EVEN_CONNECTOR_H_
