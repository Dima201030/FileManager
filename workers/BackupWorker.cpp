#include "workers/BackupWorker.h"

#include "app/Application.h"
#include "core/BackupUtils.h"
#include "core/TaskQueue.h"
#include "tasks/IBackupTask.h"

#include <chrono>
#include <thread>

namespace backup
{
namespace
{
constexpr std::size_t MaxWorkerCount = 4;
}

BackupWorker::BackupWorker(TaskQueue& queue, Application& application)
    : _queue(queue)
    , _application(application)
{
}

BackupWorker::~BackupWorker()
{
    stop();
}

bool BackupWorker::start()
{
    if (_running)
    {
        return false;
    }

    _stopRequested = false;
    _running = true;
    ensureWorkerCount(1);
    return true;
}

void BackupWorker::stop()
{
    _stopRequested = true;

    std::lock_guard<std::mutex> guard(_threadsMutex);

    for (std::thread& workerThread : _threads)
    {
        if (workerThread.joinable())
        {
            workerThread.join();
        }
    }

    _threads.clear();
    _running = false;
}

bool BackupWorker::isRunning() const
{
    return _running;
}

std::size_t BackupWorker::getWorkerCount() const
{
    std::lock_guard<std::mutex> guard(_threadsMutex);
    return _threads.size();
}

void BackupWorker::updateLoad(const std::size_t queueSize, const bool hasLargeFileTask)
{
    std::size_t desiredWorkers = 1;

    if (queueSize >= 2)
    {
        desiredWorkers = 2;
    }

    if (queueSize >= 4 || hasLargeFileTask)
    {
        desiredWorkers = 3;
    }

    if (queueSize >= 8 || (hasLargeFileTask && queueSize >= 2))
    {
        desiredWorkers = MaxWorkerCount;
    }

    ensureWorkerCount(desiredWorkers);

    _application.devTraceDetail(
        "WORKER",
        "load recalculated",
        "queueSize=" + std::to_string(queueSize)
            + " largeFileTask=" + (hasLargeFileTask ? "yes" : "no")
            + " targetWorkers=" + std::to_string(std::min(desiredWorkers, MaxWorkerCount))
            + " activeWorkers=" + std::to_string(getWorkerCount()));
}

void BackupWorker::ensureWorkerCount(const std::size_t desiredWorkers)
{
    const std::size_t workerCount = std::min(desiredWorkers, MaxWorkerCount);
    _targetWorkers = workerCount;

    std::lock_guard<std::mutex> guard(_threadsMutex);

    while (_threads.size() < workerCount)
    {
        _threads.emplace_back(&BackupWorker::runLoop, this);

        _application.devTraceDetail(
            "THREAD",
            "backup worker thread created",
            "workerIndex=" + std::to_string(_threads.size())
                + " targetWorkers=" + std::to_string(workerCount));
    }
}

void BackupWorker::runLoop()
{
    while (true)
    {
        std::unique_ptr<IBackupTask> task = _queue.popNext();

        if (task != nullptr)
        {
            _application.devTraceDetail(
                "QUEUE",
                "task picked by worker",
                "taskKey=" + task->getTaskKey()
                    + " payloadBytes=" + std::to_string(task->getPayloadSize()));

            _application.processTask(*task);
            _queue.markCompleted(task->getTaskKey());
            continue;
        }

        if (_stopRequested && (_queue.isStopped() || _queue.isIdle()))
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace backup
