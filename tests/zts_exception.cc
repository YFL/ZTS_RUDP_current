#include "zts_exception.h"

#include <cstring>

namespace standby_network
{

ZTS_Exception::ZTS_Exception(const char *msg) :
    std::exception(),
    message(new char[strlen(msg)])
{
    strcpy(const_cast<char *>(message), msg);
}

ZTS_Exception::ZTS_Exception(const std::string &msg) :
    ZTS_Exception(msg.c_str())
{
    
}

ZTS_Exception::ZTS_Exception(const ZTS_Exception &copy) :
    std::exception(),
    message(new char[strlen(copy.message)])
{
    strcpy(const_cast<char *>(message), copy.message);
}

ZTS_Exception::ZTS_Exception(ZTS_Exception &&move) :
    std::exception(),
    message(move.message)
{
    move.message = nullptr;
}

ZTS_Exception::~ZTS_Exception() noexcept
{
    if(message)
    {
        delete[] message;
    }
}

auto ZTS_Exception::operator=(const ZTS_Exception &copy) -> ZTS_Exception &
{
    if(message)
    {
        delete[] message;
    }

    message = new char[strlen(copy.message)];
    strcpy(const_cast<char *>(message), copy.message);

    return *this;
}

auto ZTS_Exception::operator=(ZTS_Exception &&move) -> ZTS_Exception &
{
    std::swap(message, move.message);

    return *this;
}

auto ZTS_Exception::what() const noexcept -> const char *
{
    return message;
}

} // namepsace standby_network