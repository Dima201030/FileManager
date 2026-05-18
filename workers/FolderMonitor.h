#pragma once

#include <atomic>
#include <thread>

namespace backup
{

class Application;

class FolderMonitor final
{
public:
    explicit FolderMonitor(Application& application);
    ~FolderMonitor();

    bool start();
    void stop();
    bool isRunning() const;

private:
    void runLoop();

    Application& _application;
    std::thread _thread;
    std::atomic_bool _stopRequested = false;
    std::atomic_bool _running = false;
};

} // namespace backup
