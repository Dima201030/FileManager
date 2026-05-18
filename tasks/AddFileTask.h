#pragma once

#include "core/BackupTypes.h"
#include "tasks/IBackupTask.h"

namespace backup
{

class AddFileTask final : public IBackupTask
{
public:
    explicit AddFileTask(const FileSnapshot& snapshot);
    ~AddFileTask() override = default;

    std::string getTaskName() const override;
    std::string getTaskKey() const override;
    std::string getRelativePath() const override;
    std::uintmax_t getPayloadSize() const override;
    TaskResult process(BackupTaskContext& context) override;

private:
    FileSnapshot _snapshot;
};

} // namespace backup
