#include "app/ConsoleInterface.h"

#include "core/BackupUtils.h"

#include <algorithm>
#include <iostream>
#include <termios.h>
#include <unistd.h>

namespace backup
{
namespace
{

const char* CommandPrompt = "backup> ";

const std::vector<std::string> ConsoleCommands = {
    "help",
    "status",
    "set-source",
    "set-backup",
    "start",
    "stop",
    "scan",
    "files",
    "deleted",
    "history",
    "restore",
    "cleanup",
    "full-cleanup",
    "logs",
    "dev-trace",
    "exit"
};

std::string getCommandPrefix(const std::string& line)
{
    const std::string trimmedLine = trim(line);
    const std::size_t spacePosition = trimmedLine.find(' ');

    if (spacePosition == std::string::npos)
    {
        return trimmedLine;
    }

    return trimmedLine.substr(0, spacePosition);
}

std::vector<std::string> findCommandMatches(const std::string& prefix)
{
    const std::string lowerPrefix = toLower(prefix);
    std::vector<std::string> matches;

    for (const std::string& command : ConsoleCommands)
    {
        if (command.rfind(lowerPrefix, 0) == 0)
        {
            matches.push_back(command);
        }
    }

    return matches;
}

std::string commonCommandPrefix(const std::vector<std::string>& matches)
{
    if (matches.empty())
    {
        return {};
    }

    std::string prefix = matches.front();

    for (const std::string& command : matches)
    {
        std::size_t index = 0;

        while (index < prefix.size()
               && index < command.size()
               && prefix[index] == command[index])
        {
            ++index;
        }

        prefix.resize(index);
    }

    return prefix;
}

void printCommandMatches(const std::vector<std::string>& matches)
{
    std::cout << '\n';

    for (const std::string& command : matches)
    {
        std::cout << "  " << command << '\n';
    }
}

class TerminalModeGuard final
{
public:
    TerminalModeGuard()
    {
        if (!isatty(STDIN_FILENO))
        {
            return;
        }

        if (tcgetattr(STDIN_FILENO, &_originalMode) != 0)
        {
            return;
        }

        termios rawMode = _originalMode;
        rawMode.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
        rawMode.c_cc[VMIN] = 1;
        rawMode.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &rawMode) == 0)
        {
            _active = true;
        }
    }

    ~TerminalModeGuard()
    {
        if (_active)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &_originalMode);
        }
    }

    bool isActive() const
    {
        return _active;
    }

private:
    termios _originalMode {};
    bool _active = false;
};

int readCharacter()
{
    unsigned char symbol = 0;

    if (read(STDIN_FILENO, &symbol, 1) == 1)
    {
        return static_cast<int>(symbol);
    }

    return -1;
}

bool readArrowKey(int firstSymbol, int& arrowCode)
{
    if (firstSymbol != 27)
    {
        return false;
    }

    const int secondSymbol = readCharacter();

    if (secondSymbol < 0)
    {
        return false;
    }

    if (secondSymbol == '[' || secondSymbol == 'O')
    {
        const int thirdSymbol = readCharacter();

        if (thirdSymbol < 0)
        {
            return false;
        }

        arrowCode = thirdSymbol;
        return true;
    }

    return false;
}

} // namespace

void ConsoleInterface::printWelcome() const
{
    std::cout << "Backup monitor started.\n";
    std::cout << "Type 'help' to see available commands.\n";
    std::cout << "Use Up/Down arrows for history and Tab for command completion.\n";
}

void ConsoleInterface::printHelp() const
{
    std::cout << "\nCommands:\n";
    std::cout << "  help                - show commands\n";
    std::cout << "  status              - show current configuration and counters\n";
    std::cout << "  set-source [path]   - set source directory\n";
    std::cout << "  set-backup [path]   - set backup directory\n";
    std::cout << "  start               - start background monitoring\n";
    std::cout << "  stop                - stop background monitoring\n";
    std::cout << "  scan                - run one scan immediately\n";
    std::cout << "  files               - show active files\n";
    std::cout << "  deleted             - show files pending deletion\n";
    std::cout << "  history <path>      - show version history of a file\n";
    std::cout << "  restore <path>      - restore file from backup\n";
    std::cout << "  cleanup             - remove expired deleted files from backup\n";
    std::cout << "  full-cleanup confirm - fully clear backup storage and metadata\n";
    std::cout << "  logs                - show recent log entries\n";
    std::cout << "  dev-trace status    - show developer trace state\n";
    std::cout << "  dev-trace on        - enable trace (path must be set)\n";
    std::cout << "  dev-trace off       - disable trace\n";
    std::cout << "  dev-trace <path>    - set folder and enable developer trace\n";
    std::cout << "  exit                - stop threads and close the program\n\n";
}

void ConsoleInterface::redrawInputLine(const std::string& prompt, const std::string& line) const
{
    std::cout << "\033[2K\r" << prompt << line << std::flush;
}

void ConsoleInterface::rememberCommand(const std::string& line)
{
    const std::string trimmedLine = trim(line);

    if (trimmedLine.empty())
    {
        return;
    }

    if (!_commandHistory.empty() && _commandHistory.back() == trimmedLine)
    {
        return;
    }

    _commandHistory.push_back(trimmedLine);
}

std::string ConsoleInterface::readLineWithHistory(const std::string& prompt, const bool rememberInHistory)
{
    if (!isatty(STDIN_FILENO))
    {
        std::cout << prompt << std::flush;
        std::string line;
        std::getline(std::cin, line);

        if (rememberInHistory)
        {
            rememberCommand(line);
            _historyPosition = _commandHistory.size();
            _historyDraft.clear();
        }

        return line;
    }

    TerminalModeGuard terminalGuard;

    if (!terminalGuard.isActive())
    {
        std::cout << prompt << std::flush;
        std::string line;
        std::getline(std::cin, line);

        if (rememberInHistory)
        {
            rememberCommand(line);
            _historyPosition = _commandHistory.size();
            _historyDraft.clear();
        }

        return line;
    }

    std::string line;
    _historyPosition = _commandHistory.size();
    _historyDraft.clear();
    redrawInputLine(prompt, line);

    while (true)
    {
        const int symbol = readCharacter();

        if (symbol < 0)
        {
            std::cout << '\n';
            return line;
        }

        if (symbol == '\n' || symbol == '\r')
        {
            std::cout << '\n';

            if (rememberInHistory)
            {
                rememberCommand(line);
                _historyPosition = _commandHistory.size();
                _historyDraft.clear();
            }

            return line;
        }

        if (symbol == 127 || symbol == 8)
        {
            if (!line.empty())
            {
                line.pop_back();
                redrawInputLine(prompt, line);
            }

            continue;
        }

        if (symbol == '\t')
        {
            const std::string commandPrefix = getCommandPrefix(line);
            const std::vector<std::string> matches = findCommandMatches(commandPrefix);

            if (matches.size() == 1)
            {
                line = matches.front();
                redrawInputLine(prompt, line);
            }
            else if (matches.size() > 1)
            {
                const std::string sharedPrefix = commonCommandPrefix(matches);

                if (sharedPrefix.size() > commandPrefix.size())
                {
                    line = sharedPrefix;
                    redrawInputLine(prompt, line);
                }
                else
                {
                    printCommandMatches(matches);
                    redrawInputLine(prompt, line);
                }
            }

            continue;
        }

        int arrowCode = 0;

        if (readArrowKey(symbol, arrowCode))
        {
            if (arrowCode == 'A')
            {
                if (_commandHistory.empty())
                {
                    continue;
                }

                if (_historyPosition == _commandHistory.size())
                {
                    _historyDraft = line;
                }

                if (_historyPosition > 0)
                {
                    --_historyPosition;
                    line = _commandHistory[_historyPosition];
                    redrawInputLine(prompt, line);
                }

                continue;
            }

            if (arrowCode == 'B')
            {
                if (_commandHistory.empty())
                {
                    continue;
                }

                if (_historyPosition < _commandHistory.size() - 1)
                {
                    ++_historyPosition;
                    line = _commandHistory[_historyPosition];
                    redrawInputLine(prompt, line);
                }
                else if (_historyPosition == _commandHistory.size() - 1)
                {
                    _historyPosition = _commandHistory.size();
                    line = _historyDraft;
                    redrawInputLine(prompt, line);
                }

                continue;
            }

            continue;
        }

        if (symbol >= 32 && symbol <= 126)
        {
            line.push_back(static_cast<char>(symbol));
            redrawInputLine(prompt, line);
        }
    }
}

std::string ConsoleInterface::readCommandLine()
{
    return readLineWithHistory(CommandPrompt, true);
}

std::string ConsoleInterface::promptValue(const std::string& label)
{
    return readLineWithHistory(label + ": ", false);
}

void ConsoleInterface::printStatus(const AppConfig& config,
                                   const bool monitoring,
                                   const std::size_t activeFiles,
                                   const std::size_t deletedFiles,
                                   const std::size_t queuedTasks) const
{
    std::cout << "\nStatus:\n";
    std::cout << "  source: " << (config.sourceDirectory.empty() ? "-" : config.sourceDirectory.string()) << '\n';
    std::cout << "  backup: " << (config.backupDirectory.empty() ? "-" : config.backupDirectory.string()) << '\n';
    std::cout << "  monitoring: " << (monitoring ? "on" : "off") << '\n';
    std::cout << "  scan interval: " << config.scanIntervalSeconds << " sec\n";
    std::cout << "  retention: " << config.retentionDays << " days\n";
    std::cout << "  active files: " << activeFiles << '\n';
    std::cout << "  pending deletion: " << deletedFiles << '\n';
    std::cout << "  queued tasks: " << queuedTasks << '\n';
}

void ConsoleInterface::printFiles(const std::vector<FileRecord>& records, const bool onlyDeleted) const
{
    if (records.empty())
    {
        std::cout << (onlyDeleted ? "No deleted files.\n" : "No active files.\n");
        return;
    }

    std::vector<FileRecord> sortedRecords = records;
    std::sort(sortedRecords.begin(), sortedRecords.end(), [](const FileRecord& left, const FileRecord& right)
    {
        return left.relativePath < right.relativePath;
    });

    std::cout << '\n';

    for (const FileRecord& record : sortedRecords)
    {
        std::cout << "  " << record.relativePath
                  << " | state: " << toString(record.state)
                  << " | size: " << formatFileSize(record.lastKnownSize)
                  << " | versions: " << record.versions.size();

        if (onlyDeleted)
        {
            std::cout << " | delete after: " << formatTimestamp(record.deletionDeadline);
        }

        std::cout << '\n';
    }
}

void ConsoleInterface::printHistory(const FileRecord& record) const
{
    std::cout << "\nHistory for: " << record.relativePath << '\n';

    if (record.versions.empty())
    {
        std::cout << "  No versions found.\n";
        return;
    }

    for (const FileVersion& version : record.versions)
    {
        std::cout << "  " << formatTimestamp(version.timestamp)
                  << " | " << toString(version.eventType)
                  << " | size: " << formatFileSize(version.fileSize);

        if (!version.backupRelativePath.empty())
        {
            std::cout << " | backup: " << version.backupRelativePath;
        }

        std::cout << '\n';
    }
}

void ConsoleInterface::printLogs(const std::vector<std::string>& logs) const
{
    if (logs.empty())
    {
        std::cout << "No logs yet.\n";
        return;
    }

    std::cout << '\n';

    for (const std::string& logLine : logs)
    {
        std::cout << "  " << logLine << '\n';
    }
}

} // namespace backup
