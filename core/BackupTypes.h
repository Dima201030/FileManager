#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace backup
{
namespace fs = std::filesystem;

enum class RecordState
{
    Active = 0,
    PendingDeletion = 1
};

enum class VersionEvent
{
    Added = 0,
    Modified = 1,
    Deleted = 2,
    Restored = 3
};

struct AppConfig
{
    fs::path sourceDirectory;
    fs::path backupDirectory;
    std::size_t retentionDays = 30;
    std::size_t scanIntervalSeconds = 5;
};

struct FileSnapshot
{
    std::string relativePath;
    std::uintmax_t fileSize = 0;
    long long lastWriteTime = 0;
};

struct FileVersion
{
    std::string versionId;
    std::string backupRelativePath;
    long long timestamp = 0;
    VersionEvent eventType = VersionEvent::Added;
    std::uintmax_t fileSize = 0;
};

struct FileRecord
{
    std::string relativePath;
    RecordState state = RecordState::Active;
    std::uintmax_t lastKnownSize = 0;
    long long lastWriteTime = 0;
    long long deletionDeadline = 0;
    std::vector<FileVersion> versions;
};

struct TaskResult
{
    bool success = false;
    std::string message;
};

std::string toString(RecordState state);
std::string toString(VersionEvent event);
long long currentTimestamp();
long long daysToSeconds(std::size_t days);
std::string formatTimestamp(long long timestamp);
std::string makeVersionId();
bool isContentEvent(VersionEvent event);

} // namespace backup
