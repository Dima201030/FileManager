#pragma once

#include "core/BackupTypes.h"

namespace backup
{

long long fileTimeToNumber(const fs::file_time_type& fileTime);
bool isSubPath(const fs::path& candidate, const fs::path& parent);
std::string normalizeRelativePath(const fs::path& path);
std::string trim(const std::string& value);
std::string toLower(std::string value);
std::string formatFileSize(std::uintmax_t size);
fs::path metadataFileName();
fs::path storageDirectoryName();
fs::path makeVersionStoragePath(const fs::path& backupDirectory,
                                const std::string& relativePath,
                                const std::string& versionId);
bool findLatestContentVersion(const FileRecord& record, FileVersion& version);
bool findExistingContentVersion(const FileRecord& record,
                                const fs::path& backupDirectory,
                                FileVersion& version);
bool backupVersionExists(const fs::path& backupDirectory, const FileVersion& version);
void removeEmptyParentDirectories(const fs::path& storageRoot, const fs::path& filePath);
bool isIgnoredSourcePath(const std::string& relativePath);
bool canRepairRecord(const FileRecord& record, const AppConfig& config);
void pruneSmallerContentVersions(FileRecord& record,
                                 std::uintmax_t newFileSize,
                                 const fs::path& backupDirectory);
std::uintmax_t largeFileThresholdBytes();
fs::path daemonLogFileName();
fs::path daemonPidFileName();
bool writeDaemonPidFile(const fs::path& backupDirectory);

} // namespace backup
