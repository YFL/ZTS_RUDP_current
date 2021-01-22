#ifndef _ZTS_EXCEPTION_H_
#define _ZTS_EXCEPTION_H_

#include <exception>
#include <string>

namespace standby_network
{

/**
 * @brief a bare exception class based on std::exception containing only a message
 */

class ZTS_Exception : public std::exception
{
public:
    ZTS_Exception(const char *msg);
    ZTS_Exception(const std::string &msg);
    ZTS_Exception(const ZTS_Exception &copy);
    ZTS_Exception(ZTS_Exception &&move);
    virtual ~ZTS_Exception() noexcept override;

public:
    auto operator=(const ZTS_Exception &copy) -> ZTS_Exception &;
    auto operator=(ZTS_Exception &&move) -> ZTS_Exception &;

public:
    auto what() const noexcept -> const char * override;

private:
    const char *message;
};

} // namespace standby_network

#endif // _ZTS_EXCEPTION_H_