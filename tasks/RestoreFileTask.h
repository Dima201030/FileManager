#pragma once

#include "tasks/IBackupTask.h"

namespace backup
{

class RestoreFileTask final : public IBackupTask
{
public:
    explicit RestoreFileTask(std::string relativePath);
    ~RestoreFileTask() override = default;

    std::string getTaskName() const override;
    std::string getTaskKey() const override;
    std::string getRelativePath() const override;
    std::uintmax_t getPayloadSize() const override;
    TaskResult process(BackupTaskContext& context) override;

private:
    std::string _relativePath;
};

} // namespace backup
