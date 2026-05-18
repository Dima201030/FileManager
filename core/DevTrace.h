#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace backup
{
namespace fs = std::filesystem;

class DevTrace final
{
public:
    DevTrace() = default;
    ~DevTrace() = default;

    bool setDirectory(const fs::path& directory);
    void enable();
    void disable();
    bool isEnabled() const;
    bool isConfigured() const;
    fs::path getDirectory() const;
    fs::path getSessionDirectory() const;

    void write(const std::string& category, const std::string& message) const;
    void writeDetail(const std::string& category,
                     const std::string& message,
                     const std::string& details) const;

private:
    void openSession();
    void writeToFile(const std::string& fileName,
                     const std::string& category,
                     const std::string& message,
                     const std::string& details) const;
    std::string makeTimestamp() const;
    std::string makeThreadLabel() const;

    mutable std::mutex _mutex;
    bool _enabled = false;
    fs::path _directory;
    fs::path _sessionDirectory;
};

} // namespace backup
