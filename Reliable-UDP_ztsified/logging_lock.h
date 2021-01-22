#ifndef _LOGGING_LOCK_H_
#define _LOGGING_LOCK_H_

#include<fstream>
#include <mutex>
#include <string>

class LoggingLock
{
public:
    LoggingLock(std::mutex &mutex, const std::string &mutex_name, const std::string &file_path);
    ~LoggingLock();

public:
    auto lock() -> void;
    auto unlock() -> void;

private:
    std::unique_lock<std::mutex> _lock;
    std::string _mutex_name;
    std::mutex _file_mut;
    std::ofstream _file;
};

#endif // _LOGGING_LOCK_GUARD_H_