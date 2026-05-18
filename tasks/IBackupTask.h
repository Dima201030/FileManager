#pragma once

#include "core/BackupTaskContext.h"

namespace backup
{

class IBackupTask
{
public:
    IBackupTask() = default;
    virtual ~IBackupTask() = default;

    virtual std::string getTaskName() const = 0;
    virtual std::string getTaskKey() const = 0;
    virtual std::string getRelativePath() const = 0;
    virtual std::uintmax_t getPayloadSize() const = 0;
    virtual TaskResult process(BackupTaskContext& context) = 0;
};

} // namespace backup
