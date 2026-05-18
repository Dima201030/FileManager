#pragma once

#include "core/BackupTypes.h"

#include <string>
#include <vector>

namespace backup
{

class ConsoleInterface final
{
public:
    ConsoleInterface() = default;
    ~ConsoleInterface() = default;

    void printWelcome() const;
    void printHelp() const;
    std::string readCommandLine();
    std::string promptValue(const std::string& label);
    void printStatus(const AppConfig& config,
                     bool monitoring,
                     std::size_t activeFiles,
                     std::size_t deletedFiles,
                     std::size_t queuedTasks) const;
    void printFiles(const std::vector<FileRecord>& records, bool onlyDeleted) const;
    void printHistory(const FileRecord& record) const;
    void printLogs(const std::vector<std::string>& logs) const;

private:
    std::string readLineWithHistory(const std::string& prompt, bool rememberInHistory);
    void rememberCommand(const std::string& line);
    void redrawInputLine(const std::string& prompt, const std::string& line) const;

    std::vector<std::string> _commandHistory;
    std::size_t _historyPosition = 0;
    std::string _historyDraft;
};

} // namespace backup
