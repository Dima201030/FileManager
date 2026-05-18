#include "core/DevTrace.h"

#include "core/BackupUtils.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace backup
{
namespace
{

std::string formatSessionFolderName()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timeValue = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&timeValue);

    if (localTime == nullptr)
    {
        return "session_unknown";
    }

    std::ostringstream stream;
    stream << "session_" << std::put_time(localTime, "%Y-%m-%d_%H-%M-%S");
    return stream.str();
}

} // namespace

bool DevTrace::setDirectory(const fs::path& directory)
{
    std::lock_guard<std::mutex> guard(_mutex);

    if (directory.empty())
    {
        return false;
    }

    std::error_code errorCode;
    fs::create_directories(directory, errorCode);

    if (errorCode)
    {
        return false;
    }

    _directory = fs::weakly_canonical(directory, errorCode);

    if (errorCode)
    {
        _directory = directory;
    }

    return true;
}

void DevTrace::enable()
{
    std::lock_guard<std::mutex> guard(_mutex);

    if (_directory.empty())
    {
        return;
    }

    if (!_enabled)
    {
        openSession();
    }

    _enabled = true;
}

void DevTrace::disable()
{
    std::lock_guard<std::mutex> guard(_mutex);

    if (_enabled)
    {
        writeToFile("main.log", "TRACE", "developer trace disabled", {});
    }

    _enabled = false;
}

bool DevTrace::isEnabled() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _enabled;
}

bool DevTrace::isConfigured() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return !_directory.empty();
}

fs::path DevTrace::getDirectory() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _directory;
}

fs::path DevTrace::getSessionDirectory() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _sessionDirectory;
}

void DevTrace::write(const std::string& category, const std::string& message) const
{
    writeDetail(category, message, {});
}

void DevTrace::writeDetail(const std::string& category,
                           const std::string& message,
                           const std::string& details) const
{
    std::lock_guard<std::mutex> guard(_mutex);

    if (!_enabled || _sessionDirectory.empty())
    {
        return;
    }

    writeToFile("main.log", category, message, details);

    if (category == "THREAD" || category == "WORKER")
    {
        writeToFile("threads.log", category, message, details);
    }
    else if (category == "SCAN" || category == "MONITOR")
    {
        writeToFile("scans.log", category, message, details);
    }
    else if (category == "TASK" || category == "QUEUE")
    {
        writeToFile("tasks.log", category, message, details);
    }
    else if (category == "STATE" || category == "CONFIG")
    {
        writeToFile("state.log", category, message, details);
    }
}

void DevTrace::openSession()
{
    _sessionDirectory = _directory / formatSessionFolderName();
    std::error_code errorCode;
    fs::create_directories(_sessionDirectory, errorCode);

    std::ofstream readme(_sessionDirectory / "README.txt", std::ios::trunc);

    if (readme.is_open())
    {
        readme << "fileMenager developer trace session\n";
        readme << "main.log      - all events in time order\n";
        readme << "threads.log   - worker and monitor threads\n";
        readme << "scans.log     - folder scans and monitor loop\n";
        readme << "tasks.log     - backup tasks and queue\n";
        readme << "state.log     - configuration and runtime state\n";
    }

    writeToFile("main.log", "TRACE", "developer trace enabled", "session=" + _sessionDirectory.string());
}

void DevTrace::writeToFile(const std::string& fileName,
                           const std::string& category,
                           const std::string& message,
                           const std::string& details) const
{
    std::ofstream output(_sessionDirectory / fileName, std::ios::app);

    if (!output.is_open())
    {
        return;
    }

    output << makeTimestamp()
           << " | thread=" << makeThreadLabel()
           << " | " << category
           << " | " << message;

    if (!details.empty())
    {
        output << " | " << details;
    }

    output << '\n';
    output.flush();
}

std::string DevTrace::makeTimestamp() const
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timeValue = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    const std::tm* localTime = std::localtime(&timeValue);

    if (localTime == nullptr)
    {
        return "invalid_time";
    }

    std::ostringstream stream;
    stream << std::put_time(localTime, "%Y-%m-%d %H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << milliseconds;
    return stream.str();
}

std::string DevTrace::makeThreadLabel() const
{
    std::ostringstream stream;
    stream << std::this_thread::get_id();
    return stream.str();
}

} // namespace backup
