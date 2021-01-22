#include <logging_lock.h>

#include <zts_exception.h>

LoggingLock::LoggingLock(std::mutex &mutex, const std::string &mutex_name, const std::string &file_path) :
    _lock(mutex),
    _mutex_name(mutex_name),
    _file(file_path)
{
    if(!_file.good())
    {
        throw standby_network::ZTS_Exception("Couldn't open " + file_path);
    }

    std::unique_lock ul(_file_mut);
    _file << "locked mutex " << _mutex_name << std::endl;
}

auto LoggingLock::lock() -> void
{
    _lock.lock();
    std::unique_lock ul(_file_mut);
    _file << "locked mutex " << _mutex_name << std::endl;
}

auto LoggingLock::unlock() -> void
{
    _lock.unlock();
    std::unique_lock ul(_file_mut);
    _file << "unlocked mutex " << _mutex_name << std::endl;
}

LoggingLock::~LoggingLock()
{
    std::unique_lock ul(_file_mut);
    _file << "destroying lock of mutex " << _mutex_name << std::endl;
}