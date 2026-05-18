#pragma once

#include "core/BackupTypes.h"
#include "tasks/IBackupTask.h"

namespace backup
{

class ModifyFileTask final : public IBackupTask
{
public:
    explicit ModifyFileTask(const FileSnapshot& snapshot);
    ~ModifyFileTask() override = default;

    std::string getTaskName() const override;
    std::string getTaskKey() const override;
    std::string getRelativePath() const override;
    std::uintmax_t getPayloadSize() const override;
    TaskResult process(BackupTaskContext& context) override;

private:
    FileSnapshot _snapshot;
};

} // namespace backup
