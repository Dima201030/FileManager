#pragma once

#include "tasks/IBackupTask.h"

namespace backup
{

class DeleteFileTask final : public IBackupTask
{
public:
    DeleteFileTask(std::string relativePath, long long eventTimestamp, long long deletionDeadline);
    ~DeleteFileTask() override = default;

    std::string getTaskName() const override;
    std::string getTaskKey() const override;
    std::string getRelativePath() const override;
    std::uintmax_t getPayloadSize() const override;
    TaskResult process(BackupTaskContext& context) override;

private:
    std::string _relativePath;
    long long _eventTimestamp = 0;
    long long _deletionDeadline = 0;
};

} // namespace backup
