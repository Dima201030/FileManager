#pragma once

#include "core/BackupCatalog.h"
#include "core/BackupTypes.h"
#include "core/DevTrace.h"
#include "core/MetadataRepository.h"
#include "core/TaskQueue.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace backup
{

class BackupWorker;
class ConsoleInterface;
class FolderMonitor;
class IBackupTask;

class Application final
{
public:
    Application();
    ~Application();

    int run();
    int run(int argc, char* argv[]);

    bool setSourceDirectory(const fs::path& sourceDirectory);
    bool setBackupDirectory(const fs::path& backupDirectory);
    bool startMonitoring();
    void stopMonitoring();
    bool scanNow();
    bool restoreFile(const std::string& relativePath);
    bool cleanupExpiredFiles();
    bool fullCleanup();

    AppConfig getConfig() const;
    std::size_t getScanIntervalSeconds() const;
    bool isMonitoring() const;
    std::size_t pendingTasks() const;
    std::vector<FileRecord> listRecords(bool onlyDeleted) const;
    bool getRecord(const std::string& relativePath, FileRecord& record) const;
    std::vector<std::string> getLogs() const;

    void processTask(IBackupTask& task);
    void log(const std::string& message);

    bool setDevTraceDirectory(const fs::path& directory);
    bool enableDevTrace();
    bool disableDevTrace();
    bool isDevTraceEnabled() const;
    void devTrace(const std::string& category, const std::string& message) const;
    void devTraceDetail(const std::string& category,
                        const std::string& message,
                        const std::string& details) const;

private:
    int runInteractive();
    int runBackground(const std::vector<std::string>& arguments);
    void shutdown();
    bool handleCommand(const std::string& line);
    bool validateConfig(const AppConfig& config, std::string& errorMessage) const;
    bool validateSourceDirectory(const fs::path& sourceDirectory, std::string& errorMessage) const;
    bool validateBackupDirectory(const fs::path& backupDirectory, std::string& errorMessage) const;
    bool validateDirectoryPair(const fs::path& sourceDirectory,
                               const fs::path& backupDirectory,
                               std::string& errorMessage) const;
    bool loadMetadataFromBackupDirectory(AppConfig& configToLoad);
    bool saveMetadata();
    fs::path metadataFilePath(const AppConfig& config) const;
    std::vector<FileSnapshot> buildSnapshot(const AppConfig& config) const;
    void addTasksForChanges(const AppConfig& config,
                              const std::vector<FileSnapshot>& snapshot);
    bool isSnapshotStable(const FileSnapshot& snapshot) const;
    void rememberPendingSnapshot(const FileSnapshot& snapshot);
    void forgetPendingSnapshot(const std::string& relativePath);
    void clearPendingSnapshots();
    void verifyBackupIntegrity(const AppConfig& config);
    void purgeUnrecoverableRecords(const AppConfig& config);
    void updateWorkerLoad();
    bool waitForTaskQueueToDrain(std::size_t timeoutMilliseconds) const;
    bool updateConfigSettings(std::size_t retentionDays,
                              std::size_t scanIntervalSeconds,
                              bool updateRetention,
                              bool updateScanInterval);
    void printCommandLineHelp() const;
    void writeDevTraceStateSnapshot(const std::string& reason) const;

    DevTrace _devTrace;
    mutable std::mutex _configMutex;
    AppConfig _config;

    mutable std::mutex _logMutex;
    std::vector<std::string> _logs;

    BackupCatalog _catalog;
    MetadataRepository _metadataRepository;
    TaskQueue _taskQueue;
    std::unique_ptr<ConsoleInterface> _console;
    std::unique_ptr<BackupWorker> _worker;
    std::unique_ptr<FolderMonitor> _monitor;
    std::vector<FileSnapshot> _pendingSnapshots;
};

} // namespace backup
