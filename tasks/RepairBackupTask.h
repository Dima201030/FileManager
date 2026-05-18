#pragma once

#include "tasks/IBackupTask.h"

namespace backup
{

class RepairBackupTask final : public IBackupTask
{
public:
    explicit RepairBackupTask(std::string relativePath);
    ~RepairBackupTask() override = default;

    std::string getTaskName() const override;
    std::string getTaskKey() const override;
    std::string getRelativePath() const override;
    std::uintmax_t getPayloadSize() const override;
    TaskResult process(BackupTaskContext& context) override;

private:
    std::string _relativePath;
};

} // namespace backup
