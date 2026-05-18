#include "core/BackupTypes.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace backup
{

std::string toString(RecordState state)
{
    switch (state)
    {
        case RecordState::Active:
            return "active";
        case RecordState::PendingDeletion:
            return "pending_deletion";
    }

    return "unknown";
}

std::string toString(VersionEvent event)
{
    switch (event)
    {
        case VersionEvent::Added:
            return "added";
        case VersionEvent::Modified:
            return "modified";
        case VersionEvent::Deleted:
            return "deleted";
        case VersionEvent::Restored:
            return "restored";
    }

    return "unknown";
}

long long currentTimestamp()
{
    return static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

long long daysToSeconds(const std::size_t days)
{
    return static_cast<long long>(days) * 24LL * 60LL * 60LL;
}

std::string formatTimestamp(const long long timestamp)
{
    if (timestamp <= 0)
    {
        return "-";
    }

    const std::time_t timeValue = static_cast<std::time_t>(timestamp);
    const std::tm* localTime = std::localtime(&timeValue);

    if (localTime == nullptr)
    {
        return "invalid_time";
    }

    std::ostringstream stream;
    stream << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string makeVersionId()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::ostringstream stream;
    stream << milliseconds;
    return stream.str();
}

bool isContentEvent(const VersionEvent event)
{
    return event == VersionEvent::Added
        || event == VersionEvent::Modified
        || event == VersionEvent::Restored;
}

} // namespace backup
