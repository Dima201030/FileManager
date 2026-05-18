#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace backup
{

class IBackupTask;

class TaskQueue final
{
public:
    TaskQueue() = default;
    ~TaskQueue() = default;

    bool push(std::unique_ptr<IBackupTask> task);
    std::unique_ptr<IBackupTask> popNext();
    void markCompleted(const std::string& taskKey);
    void stop();
    std::size_t pendingCount() const;
    bool hasLargePendingTask() const;
    bool isIdle() const;
    bool isStopped() const;

private:
    mutable std::mutex _mutex;
    std::vector<std::unique_ptr<IBackupTask>> _tasks;
    std::vector<std::string> _pendingKeys;
    std::vector<std::string> _activeKeys;
    bool _stopped = false;
};

} // namespace backup
