#include "app/Application.h"

#include "core/BackupTaskContext.h"
#include "core/BackupUtils.h"
#include "tasks/AddFileTask.h"
#include "tasks/DeleteFileTask.h"
#include "tasks/IBackupTask.h"
#include "tasks/ModifyFileTask.h"
#include "tasks/RepairBackupTask.h"
#include "tasks/RestoreFileTask.h"
#include "workers/BackupWorker.h"
#include "workers/FolderMonitor.h"
#include "app/ConsoleInterface.h"
#include "core/DaemonUtils.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace backup
{
namespace
{
struct CommandLineOptions
{
    bool background = false;
    bool showHelp = false;
    bool hasSource = false;
    bool hasBackup = false;
    bool hasRetention = false;
    bool hasScanInterval = false;
    fs::path sourceDirectory;
    fs::path backupDirectory;
    std::size_t retentionDays = 30;
    std::size_t scanIntervalSeconds = 5;
};

std::pair<std::string, std::string> splitCommand(const std::string& line)
{
    const std::string trimmedLine = trim(line);

    if (trimmedLine.empty())
    {
        return {};
    }

    const std::size_t firstSpace = trimmedLine.find(' ');

    if (firstSpace == std::string::npos)
    {
        return {trimmedLine, {}};
    }

    return {
        trimmedLine.substr(0, firstSpace),
        trim(trimmedLine.substr(firstSpace + 1))
    };
}

std::string stripQuotes(std::string value)
{
    value = trim(value);

    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    {
        return value.substr(1, value.size() - 2);
    }

    return value;
}

bool parsePositiveSize(const std::string& value, std::size_t& result)
{
    if (value.empty())
    {
        return false;
    }

    std::stringstream stream(value);
    unsigned long long parsedValue = 0;
    char extraCharacter = '\0';

    if (!(stream >> parsedValue))
    {
        return false;
    }

    if (stream >> extraCharacter)
    {
        return false;
    }

    if (parsedValue == 0)
    {
        return false;
    }

    result = static_cast<std::size_t>(parsedValue);
    return true;
}

bool parseCommandLineArguments(const std::vector<std::string>& arguments,
                               CommandLineOptions& parsed,
                               std::string& errorMessage)
{
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string& argument = arguments[index];

        if (argument == "--help" || argument == "-h")
        {
            parsed.showHelp = true;
            continue;
        }

        if (argument == "--background")
        {
            parsed.background = true;
            continue;
        }

        if (argument == "--source")
        {
            if (index + 1 >= arguments.size())
            {
                errorMessage = "После --source нужно указать путь";
                return false;
            }

            parsed.hasSource = true;
            parsed.sourceDirectory = arguments[++index];
            continue;
        }

        if (argument == "--backup")
        {
            if (index + 1 >= arguments.size())
            {
                errorMessage = "После --backup нужно указать путь";
                return false;
            }

            parsed.hasBackup = true;
            parsed.backupDirectory = arguments[++index];
            continue;
        }

        if (argument == "--retention-days")
        {
            if (index + 1 >= arguments.size())
            {
                errorMessage = "После --retention-days нужно указать число";
                return false;
            }

            parsed.hasRetention = true;

            if (!parsePositiveSize(arguments[++index], parsed.retentionDays))
            {
                errorMessage = "Значение --retention-days должно быть положительным числом";
                return false;
            }

            continue;
        }

        if (argument == "--scan-interval")
        {
            if (index + 1 >= arguments.size())
            {
                errorMessage = "После --scan-interval нужно указать число";
                return false;
            }

            parsed.hasScanInterval = true;

            if (!parsePositiveSize(arguments[++index], parsed.scanIntervalSeconds))
            {
                errorMessage = "Значение --scan-interval должно быть положительным числом";
                return false;
            }

            continue;
        }

        errorMessage = "Неизвестный аргумент командной строки: " + argument;
        return false;
    }

    return true;
}

bool snapshotsEqual(const FileSnapshot& left, const FileSnapshot& right)
{
    return left.relativePath == right.relativePath
        && left.fileSize == right.fileSize
        && left.lastWriteTime == right.lastWriteTime;
}

bool differsFromBackedUpRecord(const FileRecord& record, const FileSnapshot& snapshot)
{
    return record.lastWriteTime != snapshot.lastWriteTime
        || record.lastKnownSize != snapshot.fileSize;
}

} // namespace

Application::Application()
    : _console(std::make_unique<ConsoleInterface>())
    , _worker(std::make_unique<BackupWorker>(_taskQueue, *this))
    , _monitor(std::make_unique<FolderMonitor>(*this))
{
}

Application::~Application()
{
    shutdown();
}

int Application::run()
{
    return runInteractive();
}

int Application::run(int argc, char* argv[])
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));

    for (int index = 1; index < argc; ++index)
    {
        arguments.push_back(argv[index]);
    }

    if (arguments.empty())
    {
        return runInteractive();
    }

    CommandLineOptions parsedArguments;
    std::string errorMessage;

    if (!parseCommandLineArguments(arguments, parsedArguments, errorMessage))
    {
        std::cout << errorMessage << '\n';
        printCommandLineHelp();
        return 1;
    }

    if (parsedArguments.showHelp)
    {
        printCommandLineHelp();
        return 0;
    }

    if (!parsedArguments.background)
    {
        std::cout << "Для параметров командной строки нужен режим --background.\n";
        printCommandLineHelp();
        return 1;
    }

    return runBackground(arguments);
}

int Application::runInteractive()
{
    _worker->start();
    _console->printWelcome();
    _console->printHelp();

    while (std::cin.good())
    {
        const std::string commandLine = _console->readCommandLine();

        if (!handleCommand(commandLine))
        {
            break;
        }
    }

    shutdown();
    return 0;
}

int Application::runBackground(const std::vector<std::string>& arguments)
{
    CommandLineOptions parsedArguments;
    std::string errorMessage;

    if (!parseCommandLineArguments(arguments, parsedArguments, errorMessage))
    {
        std::cout << errorMessage << '\n';
        return 1;
    }

    std::error_code errorCode;
    fs::create_directories(parsedArguments.backupDirectory, errorCode);

    if (errorCode)
    {
        std::cout << "Не удалось создать папку бэкапа: " << errorCode.message() << '\n';
        return 1;
    }

    const fs::path daemonLogPath = parsedArguments.backupDirectory / daemonLogFileName();

    if (!runAsBackgroundDaemon(daemonLogPath))
    {
        std::cout << "Не удалось запустить фоновый демон\n";
        return 1;
    }

    _worker->start();

    if (!writeDaemonPidFile(parsedArguments.backupDirectory))
    {
        log("Не удалось записать pid-файл демона");
    }

    log("Запуск в background-режиме, pid=" + std::to_string(getpid()));

    if (parsedArguments.hasSource
        && !setSourceDirectory(parsedArguments.sourceDirectory))
    {
        shutdown();
        return 1;
    }

    if (!parsedArguments.hasBackup)
    {
        log("Для background-режима обязательно укажи --backup");
        shutdown();
        return 1;
    }

    if (!setBackupDirectory(parsedArguments.backupDirectory))
    {
        shutdown();
        return 1;
    }

    if (!updateConfigSettings(parsedArguments.retentionDays,
                              parsedArguments.scanIntervalSeconds,
                              parsedArguments.hasRetention,
                              parsedArguments.hasScanInterval))
    {
        shutdown();
        return 1;
    }

    if (!parsedArguments.hasSource)
    {
        const AppConfig config = getConfig();

        if (config.sourceDirectory.empty())
        {
            log("В metadata нет source. Для первого background-запуска укажи --source");
            shutdown();
            return 1;
        }
    }

    if (!startMonitoring())
    {
        shutdown();
        return 1;
    }

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

void Application::shutdown()
{
    devTrace("STATE", "application shutdown started");
    stopMonitoring();
    _taskQueue.stop();
    _worker->stop();
    writeDevTraceStateSnapshot("shutdown");
    devTrace("STATE", "application shutdown finished");
}

bool Application::setSourceDirectory(const fs::path& sourceDirectory)
{
    if (isMonitoring())
    {
        log("Сначала останови мониторинг, потом меняй source");
        return false;
    }

    if (pendingTasks() > 0)
    {
        log("Сначала дождись завершения текущих задач");
        return false;
    }

    std::string errorMessage;

    if (!validateSourceDirectory(sourceDirectory, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    const AppConfig currentConfig = getConfig();
    AppConfig nextConfig = currentConfig;
    nextConfig.sourceDirectory = fs::weakly_canonical(sourceDirectory);

    if (!nextConfig.backupDirectory.empty()
        && !validateDirectoryPair(nextConfig.sourceDirectory, nextConfig.backupDirectory, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    const fs::path metadataPath = nextConfig.backupDirectory / metadataFileName();

    if (!nextConfig.backupDirectory.empty()
        && fs::exists(metadataPath)
        && !currentConfig.sourceDirectory.empty()
        && currentConfig.sourceDirectory != nextConfig.sourceDirectory
        && _catalog.size() > 0)
    {
        log("Нельзя сменить source для уже существующего каталога бэкапа");
        return false;
    }

    {
        std::lock_guard<std::mutex> guard(_configMutex);
        _config = nextConfig;
    }

    log("Исходная папка установлена: " + nextConfig.sourceDirectory.string());

    if (!nextConfig.backupDirectory.empty())
    {
        saveMetadata();
    }

    return true;
}

bool Application::setBackupDirectory(const fs::path& backupDirectory)
{
    if (isMonitoring())
    {
        log("Сначала останови мониторинг, потом меняй backup");
        return false;
    }

    if (pendingTasks() > 0)
    {
        log("Сначала дождись завершения текущих задач");
        return false;
    }

    std::string errorMessage;

    if (!validateBackupDirectory(backupDirectory, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    AppConfig nextConfig = getConfig();
    nextConfig.backupDirectory = fs::weakly_canonical(backupDirectory);

    if (!nextConfig.sourceDirectory.empty()
        && !validateDirectoryPair(nextConfig.sourceDirectory, nextConfig.backupDirectory, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    if (!fs::exists(nextConfig.backupDirectory))
    {
        fs::create_directories(nextConfig.backupDirectory);
    }

    if (!loadMetadataFromBackupDirectory(nextConfig))
    {
        return false;
    }

    clearPendingSnapshots();

    {
        std::lock_guard<std::mutex> guard(_configMutex);
        _config = nextConfig;
    }

    std::error_code errorCode;
    fs::create_directories(nextConfig.backupDirectory / storageDirectoryName(), errorCode);

    if (errorCode)
    {
        log("Не удалось создать служебную папку бэкапа: " + errorCode.message());
        return false;
    }

    saveMetadata();
    log("Папка бэкапа установлена: " + nextConfig.backupDirectory.string());
    return true;
}

bool Application::startMonitoring()
{
    std::string errorMessage;
    const AppConfig config = getConfig();

    if (!validateConfig(config, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    if (_monitor->isRunning())
    {
        log("Мониторинг уже запущен");
        return false;
    }

    scanNow();
    _monitor->start();
    log("Мониторинг папки запущен");
    return true;
}

void Application::stopMonitoring()
{
    if (_monitor->isRunning())
    {
        _monitor->stop();
        log("Мониторинг папки остановлен");
    }
}

bool Application::scanNow()
{
    const auto startedAt = std::chrono::steady_clock::now();
    const AppConfig config = getConfig();
    std::string errorMessage;

    devTrace("SCAN", "scan started");

    if (!validateConfig(config, errorMessage))
    {
        devTraceDetail("SCAN", "scan rejected", "reason=" + errorMessage);
        log(errorMessage);
        return false;
    }

    const std::vector<FileSnapshot> snapshot = buildSnapshot(config);
    devTraceDetail(
        "SCAN",
        "snapshot built",
        "filesInSource=" + std::to_string(snapshot.size())
            + " pendingSnapshots=" + std::to_string(_pendingSnapshots.size()));

    addTasksForChanges(config, snapshot);
    purgeUnrecoverableRecords(config);
    verifyBackupIntegrity(config);
    cleanupExpiredFiles();
    updateWorkerLoad();

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();

    devTraceDetail(
        "SCAN",
        "scan finished",
        "durationMs=" + std::to_string(elapsedMs)
            + " queuedTasks=" + std::to_string(pendingTasks())
            + " catalogRecords=" + std::to_string(_catalog.size()));

    writeDevTraceStateSnapshot("after_scan");
    return true;
}

bool Application::restoreFile(const std::string& relativePath)
{
    if (relativePath.empty())
    {
        log("Для восстановления нужно указать относительный путь файла");
        return false;
    }

    if (!_taskQueue.push(std::make_unique<RestoreFileTask>(relativePath)))
    {
        log("Не удалось поставить задачу на восстановление: " + relativePath);
        return false;
    }

    log("Задача восстановления поставлена в очередь: " + relativePath);
    return true;
}

bool Application::cleanupExpiredFiles()
{
    const AppConfig config = getConfig();

    if (config.backupDirectory.empty())
    {
        return false;
    }

    const long long now = currentTimestamp();
    std::vector<FileRecord> records = listRecords(true);
    bool anyRemoved = false;

    for (const FileRecord& record : records)
    {
        if (record.deletionDeadline == 0 || record.deletionDeadline > now)
        {
            continue;
        }

        for (const FileVersion& version : record.versions)
        {
            if (version.backupRelativePath.empty())
            {
                continue;
            }

            const fs::path versionPath = config.backupDirectory / fs::path(version.backupRelativePath);
            const fs::path storageRoot = config.backupDirectory / storageDirectoryName();
            std::error_code errorCode;
            fs::remove(versionPath, errorCode);

            if (errorCode)
            {
                log("Не удалось удалить файл версии из бэкапа: " + errorCode.message());
            }
            else
            {
                removeEmptyParentDirectories(storageRoot, versionPath);
            }
        }

        _catalog.erase(record.relativePath);
        anyRemoved = true;
        log("Файл окончательно удалён из бэкапа: " + record.relativePath);
    }

    if (anyRemoved)
    {
        saveMetadata();
    }

    return anyRemoved;
}

bool Application::fullCleanup()
{
    if (isMonitoring())
    {
        log("Сначала останови мониторинг, потом запускай полную очистку");
        return false;
    }

    if (pendingTasks() > 0)
    {
        log("Сначала дождись завершения текущих задач");
        return false;
    }

    const AppConfig config = getConfig();

    if (config.backupDirectory.empty())
    {
        log("Сначала укажи папку бэкапа");
        return false;
    }

    const fs::path storagePath = config.backupDirectory / storageDirectoryName();
    const fs::path metadataPath = metadataFilePath(config);
    std::error_code errorCode;

    if (fs::exists(storagePath))
    {
        fs::remove_all(storagePath, errorCode);

        if (errorCode)
        {
            log("Не удалось удалить папку хранения бэкапа: " + errorCode.message());
            return false;
        }
    }

    errorCode.clear();

    if (fs::exists(metadataPath))
    {
        fs::remove(metadataPath, errorCode);

        if (errorCode)
        {
            log("Не удалось удалить metadata: " + errorCode.message());
            return false;
        }
    }

    errorCode.clear();
    fs::create_directories(storagePath, errorCode);

    if (errorCode)
    {
        log("Не удалось заново создать папку хранения бэкапа: " + errorCode.message());
        return false;
    }

    _catalog.clear();
    clearPendingSnapshots();

    if (!saveMetadata())
    {
        return false;
    }

    log("Бэкап полностью очищен. Папка бэкапа сохранена, каталог начинается заново");
    return true;
}

AppConfig Application::getConfig() const
{
    std::lock_guard<std::mutex> guard(_configMutex);
    return _config;
}

std::size_t Application::getScanIntervalSeconds() const
{
    std::lock_guard<std::mutex> guard(_configMutex);
    return _config.scanIntervalSeconds;
}

bool Application::isMonitoring() const
{
    return _monitor->isRunning();
}

std::size_t Application::pendingTasks() const
{
    return _taskQueue.pendingCount();
}

std::vector<FileRecord> Application::listRecords(const bool onlyDeleted) const
{
    std::vector<FileRecord> records = _catalog.listRecords();

    records.erase(std::remove_if(records.begin(), records.end(), [onlyDeleted](const FileRecord& record)
    {
        if (onlyDeleted)
        {
            return record.state != RecordState::PendingDeletion;
        }

        return record.state != RecordState::Active;
    }), records.end());

    return records;
}

bool Application::getRecord(const std::string& relativePath, FileRecord& record) const
{
    return _catalog.getRecord(relativePath, record);
}

std::vector<std::string> Application::getLogs() const
{
    std::lock_guard<std::mutex> guard(_logMutex);
    return _logs;
}

void Application::processTask(IBackupTask& task)
{
    const auto startedAt = std::chrono::steady_clock::now();
    const AppConfig config = getConfig();

    devTraceDetail(
        "TASK",
        "task execution started",
        "name=" + task.getTaskName()
            + " key=" + task.getTaskKey()
            + " path=" + task.getRelativePath()
            + " payloadBytes=" + std::to_string(task.getPayloadSize()));

    BackupTaskContext context {
        config,
        _catalog,
        _metadataRepository,
        metadataFilePath(config)
    };

    const TaskResult result = task.process(context);

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();

    devTraceDetail(
        "TASK",
        "task execution finished",
        "name=" + task.getTaskName()
            + " success=" + (result.success ? "yes" : "no")
            + " durationMs=" + std::to_string(elapsedMs)
            + " message=" + result.message);

    log("[" + task.getTaskName() + "] " + result.message);
}

void Application::log(const std::string& message)
{
    const std::string fullMessage = formatTimestamp(currentTimestamp()) + " | " + message;

    {
        std::lock_guard<std::mutex> guard(_logMutex);
        _logs.push_back(fullMessage);

        if (_logs.size() > 100)
        {
            _logs.erase(_logs.begin());
        }
    }

    std::cout << fullMessage << '\n';
}

bool Application::handleCommand(const std::string& line)
{
    const std::pair<std::string, std::string> commandParts = splitCommand(line);
    const std::string& commandValue = commandParts.first;
    const std::string& argumentsValue = commandParts.second;
    const std::string command = toLower(commandValue);
    std::string arguments = stripQuotes(argumentsValue);

    if (command.empty())
    {
        return true;
    }

    if (command == "help")
    {
        _console->printHelp();
        return true;
    }

    if (command == "status")
    {
        _console->printStatus(getConfig(),
                              isMonitoring(),
                              listRecords(false).size(),
                              listRecords(true).size(),
                              pendingTasks());
        return true;
    }

    if (command == "set-source")
    {
        if (arguments.empty())
        {
            arguments = stripQuotes(_console->promptValue("Enter source directory"));
        }

        setSourceDirectory(arguments);
        return true;
    }

    if (command == "set-backup")
    {
        if (arguments.empty())
        {
            arguments = stripQuotes(_console->promptValue("Enter backup directory"));
        }

        setBackupDirectory(arguments);
        return true;
    }

    if (command == "start")
    {
        startMonitoring();
        return true;
    }

    if (command == "stop")
    {
        stopMonitoring();
        return true;
    }

    if (command == "scan")
    {
        if (scanNow() && !waitForTaskQueueToDrain(10000))
        {
            log("Не дождался завершения задач после scan");
        }
        return true;
    }

    if (command == "files")
    {
        _console->printFiles(listRecords(false), false);
        return true;
    }

    if (command == "deleted")
    {
        _console->printFiles(listRecords(true), true);
        return true;
    }

    if (command == "history")
    {
        if (arguments.empty())
        {
            log("Для history нужно указать относительный путь файла");
            return true;
        }

        FileRecord record;

        if (!getRecord(arguments, record))
        {
            log("Файл не найден в каталоге: " + arguments);
            return true;
        }

        _console->printHistory(record);
        return true;
    }

    if (command == "restore")
    {
        if (restoreFile(arguments) && !waitForTaskQueueToDrain(10000))
        {
            log("Не дождался завершения восстановления");
        }
        return true;
    }

    if (command == "cleanup")
    {
        cleanupExpiredFiles();
        return true;
    }

    if (command == "full-cleanup")
    {
        if (arguments != "confirm")
        {
            log("Для полной очистки используй: full-cleanup confirm");
            return true;
        }

        fullCleanup();
        return true;
    }

    if (command == "logs")
    {
        _console->printLogs(getLogs());
        return true;
    }

    if (command == "dev-trace")
    {
        const std::string option = toLower(trim(arguments));

        if (option.empty() || option == "status")
        {
            log("Dev trace: " + std::string(isDevTraceEnabled() ? "on" : "off"));

            if (_devTrace.isConfigured())
            {
                log("Dev trace directory: " + _devTrace.getDirectory().string());
            }
            else
            {
                log("Dev trace directory: not set");
            }

            if (!_devTrace.getSessionDirectory().empty())
            {
                log("Dev trace session: " + _devTrace.getSessionDirectory().string());
            }

            return true;
        }

        if (option == "on")
        {
            if (!_devTrace.isConfigured())
            {
                log("Сначала укажи папку: dev-trace /path/to/dev/logs");
                return true;
            }

            enableDevTrace();
            log("Developer trace включён");
            return true;
        }

        if (option == "off")
        {
            disableDevTrace();
            log("Developer trace выключен");
            return true;
        }

        if (setDevTraceDirectory(stripQuotes(arguments)))
        {
            enableDevTrace();
            log("Developer trace включён, папка: " + _devTrace.getDirectory().string());
        }

        return true;
    }

    if (command == "exit")
    {
        return false;
    }

    log("Неизвестная команда: " + command);
    return true;
}

bool Application::validateConfig(const AppConfig& config, std::string& errorMessage) const
{
    if (config.sourceDirectory.empty())
    {
        errorMessage = "Сначала укажи исходную папку";
        return false;
    }

    if (config.backupDirectory.empty())
    {
        errorMessage = "Сначала укажи папку бэкапа";
        return false;
    }

    return validateDirectoryPair(config.sourceDirectory, config.backupDirectory, errorMessage);
}

bool Application::validateSourceDirectory(const fs::path& sourceDirectory, std::string& errorMessage) const
{
    if (sourceDirectory.empty())
    {
        errorMessage = "Путь к исходной папке пустой";
        return false;
    }

    if (!fs::exists(sourceDirectory))
    {
        errorMessage = "Исходная папка не существует: " + sourceDirectory.string();
        return false;
    }

    if (!fs::is_directory(sourceDirectory))
    {
        errorMessage = "Указанный source не является папкой: " + sourceDirectory.string();
        return false;
    }

    return true;
}

bool Application::validateBackupDirectory(const fs::path& backupDirectory, std::string& errorMessage) const
{
    if (backupDirectory.empty())
    {
        errorMessage = "Путь к папке бэкапа пустой";
        return false;
    }

    std::error_code errorCode;

    if (!fs::exists(backupDirectory))
    {
        fs::create_directories(backupDirectory, errorCode);

        if (errorCode)
        {
            errorMessage = "Не удалось создать папку бэкапа: " + errorCode.message();
            return false;
        }
    }

    if (!fs::is_directory(backupDirectory))
    {
        errorMessage = "Указанный backup не является папкой: " + backupDirectory.string();
        return false;
    }

    return true;
}

bool Application::validateDirectoryPair(const fs::path& sourceDirectory,
                                        const fs::path& backupDirectory,
                                        std::string& errorMessage) const
{
    if (sourceDirectory.empty() || backupDirectory.empty())
    {
        return true;
    }

    if (fs::equivalent(sourceDirectory, backupDirectory))
    {
        errorMessage = "Source и backup не могут быть одной и той же папкой";
        return false;
    }

    if (isSubPath(backupDirectory, sourceDirectory))
    {
        errorMessage = "Папка бэкапа не может лежать внутри source";
        return false;
    }

    if (isSubPath(sourceDirectory, backupDirectory))
    {
        errorMessage = "Source не может лежать внутри папки бэкапа";
        return false;
    }

    return true;
}

bool Application::loadMetadataFromBackupDirectory(AppConfig& configToLoad)
{
    std::string errorMessage;
    AppConfig loadedConfig = configToLoad;
    loadedConfig.backupDirectory = configToLoad.backupDirectory;

    if (!_metadataRepository.load(metadataFilePath(loadedConfig), loadedConfig, _catalog, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    if (!loadedConfig.sourceDirectory.empty())
    {
        configToLoad.sourceDirectory = loadedConfig.sourceDirectory;
    }

    configToLoad.retentionDays = loadedConfig.retentionDays;
    configToLoad.scanIntervalSeconds = loadedConfig.scanIntervalSeconds;
    return true;
}

bool Application::saveMetadata()
{
    const AppConfig config = getConfig();

    if (config.backupDirectory.empty())
    {
        return false;
    }

    std::string errorMessage;

    if (!_metadataRepository.save(metadataFilePath(config), config, _catalog, errorMessage))
    {
        log(errorMessage);
        return false;
    }

    return true;
}

fs::path Application::metadataFilePath(const AppConfig& config) const
{
    return config.backupDirectory / metadataFileName();
}

std::vector<FileSnapshot> Application::buildSnapshot(const AppConfig& config) const
{
    std::vector<FileSnapshot> result;
    std::error_code errorCode;

    fs::recursive_directory_iterator iterator(config.sourceDirectory,
                                              fs::directory_options::skip_permission_denied,
                                              errorCode);

    fs::recursive_directory_iterator end;

    while (iterator != end)
    {
        if (errorCode)
        {
            errorCode.clear();
            iterator.increment(errorCode);
            continue;
        }

        const fs::directory_entry& entry = *iterator;

        if (entry.is_regular_file(errorCode))
        {
            const fs::path relativePath = fs::relative(entry.path(), config.sourceDirectory, errorCode);

            if (!errorCode)
            {
                const std::string relativePathString = normalizeRelativePath(relativePath);

                if (isIgnoredSourcePath(relativePathString))
                {
                    errorCode.clear();
                    iterator.increment(errorCode);
                    continue;
                }

                FileSnapshot snapshot;
                snapshot.relativePath = relativePathString;
                snapshot.fileSize = entry.file_size(errorCode);

                if (!errorCode)
                {
                    snapshot.lastWriteTime = fileTimeToNumber(entry.last_write_time(errorCode));
                }

                if (!errorCode)
                {
                    bool replaced = false;

                    for (FileSnapshot& currentSnapshot : result)
                    {
                        if (currentSnapshot.relativePath == snapshot.relativePath)
                        {
                            currentSnapshot = snapshot;
                            replaced = true;
                            break;
                        }
                    }

                    if (!replaced)
                    {
                        result.push_back(snapshot);
                    }
                }
            }
        }

        errorCode.clear();
        iterator.increment(errorCode);
    }

    return result;
}

bool Application::isSnapshotStable(const FileSnapshot& snapshot) const
{
    for (const FileSnapshot& pendingSnapshot : _pendingSnapshots)
    {
        if (pendingSnapshot.relativePath == snapshot.relativePath)
        {
            return snapshotsEqual(pendingSnapshot, snapshot);
        }
    }

    return false;
}

void Application::rememberPendingSnapshot(const FileSnapshot& snapshot)
{
    for (FileSnapshot& pendingSnapshot : _pendingSnapshots)
    {
        if (pendingSnapshot.relativePath == snapshot.relativePath)
        {
            pendingSnapshot = snapshot;
            return;
        }
    }

    _pendingSnapshots.push_back(snapshot);
}

void Application::forgetPendingSnapshot(const std::string& relativePath)
{
    _pendingSnapshots.erase(std::remove_if(_pendingSnapshots.begin(), _pendingSnapshots.end(),
        [&relativePath](const FileSnapshot& pendingSnapshot)
    {
        return pendingSnapshot.relativePath == relativePath;
    }), _pendingSnapshots.end());
}

void Application::clearPendingSnapshots()
{
    _pendingSnapshots.clear();
}

void Application::purgeUnrecoverableRecords(const AppConfig& config)
{
    if (config.backupDirectory.empty())
    {
        return;
    }

    bool anyRemoved = false;
    const std::vector<FileRecord> records = _catalog.listRecords();

    for (const FileRecord& record : records)
    {
        if (canRepairRecord(record, config))
        {
            continue;
        }

        const fs::path sourcePath = config.sourceDirectory / fs::path(record.relativePath);
        std::error_code errorCode;
        const bool sourceExists = fs::exists(sourcePath, errorCode) && !errorCode;

        if (sourceExists)
        {
            continue;
        }

        _catalog.erase(record.relativePath);
        anyRemoved = true;
        devTraceDetail("STATE", "purged unrecoverable record", "path=" + record.relativePath);
        log("Запись удалена из каталога, восстановление невозможно: " + record.relativePath);
    }

    if (anyRemoved)
    {
        saveMetadata();
    }
}

void Application::verifyBackupIntegrity(const AppConfig& config)
{
    if (config.backupDirectory.empty())
    {
        return;
    }

    const std::vector<FileRecord> records = _catalog.listRecords();

    for (const FileRecord& record : records)
    {
        if (record.state != RecordState::Active && record.state != RecordState::PendingDeletion)
        {
            continue;
        }

        bool missingBackupFile = false;

        for (const FileVersion& version : record.versions)
        {
            if (!isContentEvent(version.eventType))
            {
                continue;
            }

            if (!backupVersionExists(config.backupDirectory, version))
            {
                missingBackupFile = true;
                break;
            }
        }

        if (!missingBackupFile)
        {
            continue;
        }

        if (!canRepairRecord(record, config))
        {
            continue;
        }

        if (_taskQueue.push(std::make_unique<RepairBackupTask>(record.relativePath)))
        {
            devTraceDetail("QUEUE", "queued repair task", "path=" + record.relativePath);
        }
        else
        {
            log("Не удалось поставить задачу на восстановление бэкапа: " + record.relativePath);
        }
    }
}

void Application::updateWorkerLoad()
{
    if (_worker->isRunning())
    {
        _worker->updateLoad(_taskQueue.pendingCount(), _taskQueue.hasLargePendingTask());
    }
}

void Application::addTasksForChanges(const AppConfig& config,
                                       const std::vector<FileSnapshot>& snapshot)
{
    const std::vector<FileRecord> records = _catalog.listRecords();
    std::size_t queuedAdd = 0;
    std::size_t queuedModify = 0;
    std::size_t queuedDelete = 0;
    std::size_t waitingStability = 0;

    for (const FileSnapshot& fileSnapshot : snapshot)
    {
        FileRecord record;
        bool recordFound = false;

        for (const FileRecord& currentRecord : records)
        {
            if (currentRecord.relativePath == fileSnapshot.relativePath)
            {
                record = currentRecord;
                recordFound = true;
                break;
            }
        }

        if (!recordFound)
        {
            if (!isSnapshotStable(fileSnapshot))
            {
                rememberPendingSnapshot(fileSnapshot);
                ++waitingStability;
                devTraceDetail("QUEUE", "waiting stable snapshot for new file", "path=" + fileSnapshot.relativePath);
                continue;
            }

            forgetPendingSnapshot(fileSnapshot.relativePath);

            if (_taskQueue.push(std::make_unique<AddFileTask>(fileSnapshot)))
            {
                ++queuedAdd;
                devTraceDetail("QUEUE", "queued add task", "path=" + fileSnapshot.relativePath + " bytes=" + std::to_string(fileSnapshot.fileSize));
            }

            continue;
        }

        if (record.state == RecordState::PendingDeletion)
        {
            forgetPendingSnapshot(fileSnapshot.relativePath);

            if (_taskQueue.push(std::make_unique<ModifyFileTask>(fileSnapshot)))
            {
                ++queuedModify;
                devTraceDetail("QUEUE", "queued restore-after-delete modify", "path=" + fileSnapshot.relativePath);
            }

            continue;
        }

        if (!differsFromBackedUpRecord(record, fileSnapshot))
        {
            forgetPendingSnapshot(fileSnapshot.relativePath);
            continue;
        }

        if (!isSnapshotStable(fileSnapshot))
        {
            rememberPendingSnapshot(fileSnapshot);
            ++waitingStability;
            devTraceDetail("QUEUE", "waiting stable snapshot for modify", "path=" + fileSnapshot.relativePath);
            continue;
        }

        forgetPendingSnapshot(fileSnapshot.relativePath);

        if (_taskQueue.push(std::make_unique<ModifyFileTask>(fileSnapshot)))
        {
            ++queuedModify;
            devTraceDetail("QUEUE", "queued modify task", "path=" + fileSnapshot.relativePath + " bytes=" + std::to_string(fileSnapshot.fileSize));
        }
    }

    const long long eventTimestamp = currentTimestamp();
    const long long deletionDeadline = eventTimestamp + daysToSeconds(config.retentionDays);

    for (const FileRecord& record : records)
    {
        if (record.state != RecordState::Active)
        {
            continue;
        }

        bool snapshotFound = false;

        for (const FileSnapshot& currentSnapshot : snapshot)
        {
            if (currentSnapshot.relativePath == record.relativePath)
            {
                snapshotFound = true;
                break;
            }
        }

        if (!snapshotFound)
        {
            if (_taskQueue.push(std::make_unique<DeleteFileTask>(record.relativePath, eventTimestamp, deletionDeadline)))
            {
                ++queuedDelete;
                devTraceDetail("QUEUE", "queued delete task", "path=" + record.relativePath);
            }
        }
    }

    devTraceDetail(
        "QUEUE",
        "scan task planning finished",
        "add=" + std::to_string(queuedAdd)
            + " modify=" + std::to_string(queuedModify)
            + " delete=" + std::to_string(queuedDelete)
            + " waitingStability=" + std::to_string(waitingStability));
}

bool Application::waitForTaskQueueToDrain(const std::size_t timeoutMilliseconds) const
{
    const auto startedAt = std::chrono::steady_clock::now();

    while (!_taskQueue.isIdle())
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count();

        if (elapsed >= static_cast<long long>(timeoutMilliseconds))
        {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return true;
}

bool Application::updateConfigSettings(const std::size_t retentionDays,
                                       const std::size_t scanIntervalSeconds,
                                       const bool updateRetention,
                                       const bool updateScanInterval)
{
    if (!updateRetention && !updateScanInterval)
    {
        return true;
    }

    AppConfig nextConfig = getConfig();

    if (updateRetention)
    {
        nextConfig.retentionDays = retentionDays;
    }

    if (updateScanInterval)
    {
        nextConfig.scanIntervalSeconds = scanIntervalSeconds;
    }

    {
        std::lock_guard<std::mutex> guard(_configMutex);
        _config = nextConfig;
    }

    if (!nextConfig.backupDirectory.empty())
    {
        return saveMetadata();
    }

    return true;
}

bool Application::setDevTraceDirectory(const fs::path& directory)
{
    if (!_devTrace.setDirectory(directory))
    {
        log("Не удалось установить папку developer trace");
        return false;
    }

    devTraceDetail("CONFIG", "developer trace directory set", "path=" + directory.string());
    return true;
}

bool Application::enableDevTrace()
{
    if (!_devTrace.isConfigured())
    {
        return false;
    }

    _devTrace.enable();
    writeDevTraceStateSnapshot("dev_trace_enabled");
    return true;
}

bool Application::disableDevTrace()
{
    writeDevTraceStateSnapshot("dev_trace_disabled");
    _devTrace.disable();
    return true;
}

bool Application::isDevTraceEnabled() const
{
    return _devTrace.isEnabled();
}

void Application::devTrace(const std::string& category, const std::string& message) const
{
    _devTrace.write(category, message);
}

void Application::devTraceDetail(const std::string& category,
                                 const std::string& message,
                                 const std::string& details) const
{
    _devTrace.writeDetail(category, message, details);
}

void Application::writeDevTraceStateSnapshot(const std::string& reason) const
{
    if (!_devTrace.isEnabled())
    {
        return;
    }

    const AppConfig config = getConfig();

    devTraceDetail(
        "STATE",
        "runtime snapshot",
        "reason=" + reason
            + " monitoring=" + (isMonitoring() ? "on" : "off")
            + " workers=" + std::to_string(_worker->getWorkerCount())
            + " queuedTasks=" + std::to_string(pendingTasks())
            + " catalogRecords=" + std::to_string(_catalog.size())
            + " activeFiles=" + std::to_string(listRecords(false).size())
            + " pendingDeletion=" + std::to_string(listRecords(true).size())
            + " pendingSnapshots=" + std::to_string(_pendingSnapshots.size())
            + " source=" + config.sourceDirectory.string()
            + " backup=" + config.backupDirectory.string()
            + " retentionDays=" + std::to_string(config.retentionDays)
            + " scanIntervalSec=" + std::to_string(config.scanIntervalSeconds));
}

void Application::printCommandLineHelp() const
{
    std::cout << "Usage:\n";
    std::cout << "  ./build/fileMenager\n";
    std::cout << "  ./build/fileMenager --background --backup <path> [--source <path>] [--scan-interval <seconds>] [--retention-days <days>]\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --background            run without interactive console and keep monitoring in background\n";
    std::cout << "  --backup <path>       backup directory; required for background mode\n";
    std::cout << "  --source <path>       source directory; required on first background start if metadata is empty\n";
    std::cout << "  --scan-interval <n>   scan interval in seconds\n";
    std::cout << "  --retention-days <n>  deleted file retention in days\n";
    std::cout << "  --help, -h            show this help\n";
}

} // namespace backup
