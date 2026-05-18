#include "workers/FolderMonitor.h"

#include "app/Application.h"

#include <chrono>

namespace backup
{

FolderMonitor::FolderMonitor(Application& application)
    : _application(application)
{
}

FolderMonitor::~FolderMonitor()
{
    stop();
}

bool FolderMonitor::start()
{
    if (_running)
    {
        return false;
    }

    _stopRequested = false;
    _running = true;
    _thread = std::thread(&FolderMonitor::runLoop, this);
    _application.devTraceDetail("MONITOR", "folder monitor started", "scanIntervalSec=" + std::to_string(_application.getScanIntervalSeconds()));
    return true;
}

void FolderMonitor::stop()
{
    _stopRequested = true;

    if (_thread.joinable())
    {
        _thread.join();
    }

    _application.devTrace("MONITOR", "folder monitor stopped");
    _running = false;
}

bool FolderMonitor::isRunning() const
{
    return _running;
}

void FolderMonitor::runLoop()
{
    while (!_stopRequested)
    {
        _application.devTrace("MONITOR", "monitor tick: scan requested");
        _application.scanNow();

        const std::size_t waitIterations = std::max<std::size_t>(1, _application.getScanIntervalSeconds() * 5);

        for (std::size_t index = 0; index < waitIterations && !_stopRequested; ++index)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    _running = false;
}

} // namespace backup
