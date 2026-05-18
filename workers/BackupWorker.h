#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace backup
{

class Application;
class TaskQueue;

class BackupWorker final
{
public:
    BackupWorker(TaskQueue& queue, Application& application);
    ~BackupWorker();

    bool start();
    void stop();
    bool isRunning() const;
    std::size_t getWorkerCount() const;
    void updateLoad(std::size_t queueSize, bool hasLargeFileTask);

private:
    void runLoop();
    void ensureWorkerCount(std::size_t desiredWorkers);

    TaskQueue& _queue;
    Application& _application;
    std::vector<std::thread> _threads;
    mutable std::mutex _threadsMutex;
    std::atomic_bool _stopRequested = false;
    std::atomic_bool _running = false;
    std::size_t _targetWorkers = 1;
};

} // namespace backup
