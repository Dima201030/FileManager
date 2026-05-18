#include "core/TaskQueue.h"

#include "core/BackupUtils.h"
#include "tasks/IBackupTask.h"

#include <algorithm>

namespace backup
{

bool TaskQueue::push(std::unique_ptr<IBackupTask> task)
{
    if (task == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(_mutex);

    if (_stopped)
    {
        return false;
    }

    const std::string taskKey = task->getTaskKey();

    if (std::find(_pendingKeys.begin(), _pendingKeys.end(), taskKey) != _pendingKeys.end()
        || std::find(_activeKeys.begin(), _activeKeys.end(), taskKey) != _activeKeys.end())
    {
        return false;
    }

    _pendingKeys.push_back(taskKey);
    _tasks.push_back(std::move(task));
    return true;
}

std::unique_ptr<IBackupTask> TaskQueue::popNext()
{
    std::lock_guard<std::mutex> guard(_mutex);

    if (_tasks.empty())
    {
        return nullptr;
    }

    std::unique_ptr<IBackupTask> task = std::move(_tasks.front());
    _tasks.erase(_tasks.begin());

    const std::string taskKey = task->getTaskKey();
    const auto pendingIterator = std::find(_pendingKeys.begin(), _pendingKeys.end(), taskKey);

    if (pendingIterator != _pendingKeys.end())
    {
        _pendingKeys.erase(pendingIterator);
    }

    _activeKeys.push_back(taskKey);

    return task;
}

void TaskQueue::markCompleted(const std::string& taskKey)
{
    std::lock_guard<std::mutex> guard(_mutex);
    const auto iterator = std::find(_activeKeys.begin(), _activeKeys.end(), taskKey);

    if (iterator != _activeKeys.end())
    {
        _activeKeys.erase(iterator);
    }
}

void TaskQueue::stop()
{
    std::lock_guard<std::mutex> guard(_mutex);
    _stopped = true;
}

std::size_t TaskQueue::pendingCount() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _tasks.size() + _activeKeys.size();
}

bool TaskQueue::hasLargePendingTask() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    const std::uintmax_t threshold = largeFileThresholdBytes();

    for (const std::unique_ptr<IBackupTask>& task : _tasks)
    {
        if (task != nullptr && task->getPayloadSize() >= threshold)
        {
            return true;
        }
    }

    return false;
}

bool TaskQueue::isIdle() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _tasks.empty() && _activeKeys.empty();
}

bool TaskQueue::isStopped() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _stopped;
}

} // namespace backup
