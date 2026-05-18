#include "core/DaemonUtils.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace backup
{

bool runAsBackgroundDaemon(const fs::path& logFilePath)
{
    std::error_code errorCode;
    fs::create_directories(logFilePath.parent_path(), errorCode);

    pid_t firstFork = fork();

    if (firstFork < 0)
    {
        return false;
    }

    if (firstFork > 0)
    {
        _exit(0);
    }

    if (setsid() < 0)
    {
        return false;
    }

    pid_t secondFork = fork();

    if (secondFork < 0)
    {
        return false;
    }

    if (secondFork > 0)
    {
        _exit(0);
    }

    const int logDescriptor = open(logFilePath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (logDescriptor < 0)
    {
        return false;
    }

    dup2(logDescriptor, STDOUT_FILENO);
    dup2(logDescriptor, STDERR_FILENO);

    if (logDescriptor > STDERR_FILENO)
    {
        close(logDescriptor);
    }

    const int nullDescriptor = open("/dev/null", O_RDONLY);

    if (nullDescriptor >= 0)
    {
        dup2(nullDescriptor, STDIN_FILENO);
        close(nullDescriptor);
    }

    return true;
}

} // namespace backup
