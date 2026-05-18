#include "core/BackupUtils.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace backup
{

long long fileTimeToNumber(const fs::file_time_type& fileTime)
{
    return static_cast<long long>(fileTime.time_since_epoch().count());
}

bool isSubPath(const fs::path& candidate, const fs::path& parent)
{
    const fs::path normalizedCandidate = fs::weakly_canonical(candidate);
    const fs::path normalizedParent = fs::weakly_canonical(parent);

    auto candidateIterator = normalizedCandidate.begin();
    auto parentIterator = normalizedParent.begin();

    for (; parentIterator != normalizedParent.end(); ++parentIterator, ++candidateIterator)
    {
        if (candidateIterator == normalizedCandidate.end() || *candidateIterator != *parentIterator)
        {
            return false;
        }
    }

    return true;
}

std::string normalizeRelativePath(const fs::path& path)
{
    return path.generic_string();
}

std::string trim(const std::string& value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](const unsigned char symbol)
    {
        return std::isspace(symbol) != 0;
    });

    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char symbol)
    {
        return std::isspace(symbol) != 0;
    }).base();

    if (begin >= end)
    {
        return {};
    }

    return std::string(begin, end);
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char symbol)
    {
        return static_cast<char>(std::tolower(symbol));
    });

    return value;
}

std::string formatFileSize(const std::uintmax_t size)
{
    constexpr double bytesInKilobyte = 1024.0;
    constexpr double bytesInMegabyte = 1024.0 * 1024.0;

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2);

    if (size >= static_cast<std::uintmax_t>(bytesInMegabyte))
    {
        stream << static_cast<double>(size) / bytesInMegabyte << " MB";
        return stream.str();
    }

    if (size >= static_cast<std::uintmax_t>(bytesInKilobyte))
    {
        stream << static_cast<double>(size) / bytesInKilobyte << " KB";
        return stream.str();
    }

    stream.str({});
    stream.clear();
    stream << size << " B";
    return stream.str();
}

fs::path metadataFileName()
{
    return ".backup_metadata.txt";
}

fs::path storageDirectoryName()
{
    return ".backup_storage";
}

fs::path makeVersionStoragePath(const fs::path& backupDirectory,
                                const std::string& relativePath,
                                const std::string& versionId)
{
    const fs::path relative(relativePath);
    const fs::path parentDirectory = relative.parent_path();
    const std::string fileName = relative.filename().string() + "__" + versionId + ".bak";

    return backupDirectory / storageDirectoryName() / parentDirectory / fileName;
}

bool findLatestContentVersion(const FileRecord& record, FileVersion& version)
{
    for (auto iterator = record.versions.rbegin(); iterator != record.versions.rend(); ++iterator)
    {
        if (isContentEvent(iterator->eventType) && !iterator->backupRelativePath.empty())
        {
            version = *iterator;
            return true;
        }
    }

    return false;
}

bool findExistingContentVersion(const FileRecord& record,
                                const fs::path& backupDirectory,
                                FileVersion& version)
{
    for (auto iterator = record.versions.rbegin(); iterator != record.versions.rend(); ++iterator)
    {
        if (!isContentEvent(iterator->eventType) || iterator->backupRelativePath.empty())
        {
            continue;
        }

        if (backupVersionExists(backupDirectory, *iterator))
        {
            version = *iterator;
            return true;
        }
    }

    return false;
}

bool backupVersionExists(const fs::path& backupDirectory, const FileVersion& version)
{
    if (version.backupRelativePath.empty())
    {
        return false;
    }

    std::error_code errorCode;
    return fs::exists(backupDirectory / fs::path(version.backupRelativePath), errorCode) && !errorCode;
}

bool isIgnoredSourcePath(const std::string& relativePath)
{
    if (relativePath.empty())
    {
        return true;
    }

    if (relativePath == metadataFileName().string()
        || relativePath == daemonLogFileName().string()
        || relativePath == daemonPidFileName().string())
    {
        return true;
    }

    if (relativePath.rfind(".backup_", 0) == 0)
    {
        return true;
    }

    const std::string storageMarker = std::string(storageDirectoryName().generic_string()) + "/";
    const std::string storageMarkerWindows = std::string(storageDirectoryName().generic_string()) + "\\";

    if (relativePath.rfind(storageMarker, 0) == 0
        || relativePath.rfind(storageMarkerWindows, 0) == 0)
    {
        return true;
    }

    return false;
}

bool canRepairRecord(const FileRecord& record, const AppConfig& config)
{
    if (config.sourceDirectory.empty() || config.backupDirectory.empty())
    {
        return false;
    }

    const fs::path sourcePath = config.sourceDirectory / fs::path(record.relativePath);
    std::error_code errorCode;

    if (record.state == RecordState::Active && fs::exists(sourcePath, errorCode) && !errorCode)
    {
        return true;
    }

    FileVersion donorVersion;
    return findExistingContentVersion(record, config.backupDirectory, donorVersion);
}

void pruneSmallerContentVersions(FileRecord& record,
                                 const std::uintmax_t newFileSize,
                                 const fs::path& backupDirectory)
{
    std::vector<FileVersion> keptVersions;

    for (const FileVersion& version : record.versions)
    {
        if (isContentEvent(version.eventType)
            && !version.backupRelativePath.empty()
            && version.fileSize > 0
            && version.fileSize <= newFileSize)
        {
            const fs::path versionPath = backupDirectory / fs::path(version.backupRelativePath);
            const fs::path storageRoot = backupDirectory / storageDirectoryName();
            std::error_code errorCode;
            fs::remove(versionPath, errorCode);
            removeEmptyParentDirectories(storageRoot, versionPath);
            continue;
        }

        keptVersions.push_back(version);
    }

    record.versions = keptVersions;
}

std::uintmax_t largeFileThresholdBytes()
{
    return 100ULL * 1024ULL * 1024ULL;
}

fs::path daemonLogFileName()
{
    return ".backup_daemon.log";
}

fs::path daemonPidFileName()
{
    return ".backup_daemon.pid";
}

bool writeDaemonPidFile(const fs::path& backupDirectory)
{
    const fs::path pidFilePath = backupDirectory / daemonPidFileName();
    std::ofstream output(pidFilePath, std::ios::trunc);

    if (!output.is_open())
    {
        return false;
    }

    output << getpid();
    return true;
}

void removeEmptyParentDirectories(const fs::path& storageRoot, const fs::path& filePath)
{
    std::error_code errorCode;
    const fs::path normalizedStorageRoot = fs::weakly_canonical(storageRoot, errorCode);

    if (errorCode)
    {
        return;
    }

    fs::path directory = filePath.parent_path();

    while (!directory.empty())
    {
        errorCode.clear();
        const fs::path normalizedDirectory = fs::weakly_canonical(directory, errorCode);

        if (errorCode || normalizedDirectory == normalizedStorageRoot)
        {
            break;
        }

        if (!isSubPath(normalizedDirectory, normalizedStorageRoot))
        {
            break;
        }

        if (!fs::exists(normalizedDirectory, errorCode) || !fs::is_directory(normalizedDirectory, errorCode))
        {
            break;
        }

        bool isEmpty = true;

        const fs::directory_iterator endIterator;
        fs::directory_iterator directoryIterator(normalizedDirectory, errorCode);

        if (errorCode)
        {
            isEmpty = false;
        }
        else if (directoryIterator != endIterator)
        {
            isEmpty = false;
        }

        if (!isEmpty)
        {
            break;
        }

        fs::remove(normalizedDirectory, errorCode);

        if (errorCode)
        {
            break;
        }

        directory = normalizedDirectory.parent_path();
    }
}

} // namespace backup
