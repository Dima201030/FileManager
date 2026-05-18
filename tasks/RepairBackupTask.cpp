#include "tasks/RepairBackupTask.h"

#include "core/BackupCatalog.h"
#include "core/BackupUtils.h"
#include "core/MetadataRepository.h"

#include <filesystem>

namespace backup
{
namespace
{

bool copyBackupVersion(const fs::path& sourceFile,
                       const fs::path& targetFile,
                       std::string& errorMessage)
{
    std::error_code errorCode;
    fs::create_directories(targetFile.parent_path(), errorCode);

    if (errorCode)
    {
        errorMessage = "Не удалось создать каталог для восстановления бэкапа: " + errorCode.message();
        return false;
    }

    fs::copy_file(sourceFile, targetFile, fs::copy_options::overwrite_existing, errorCode);

    if (errorCode)
    {
        errorMessage = "Не удалось восстановить файл в бэкапе: " + errorCode.message();
        return false;
    }

    return true;
}

} // namespace

RepairBackupTask::RepairBackupTask(std::string relativePath)
    : _relativePath(std::move(relativePath))
{
}

std::string RepairBackupTask::getTaskName() const
{
    return "repair";
}

std::string RepairBackupTask::getTaskKey() const
{
    return getTaskName() + ":" + _relativePath;
}

std::string RepairBackupTask::getRelativePath() const
{
    return _relativePath;
}

std::uintmax_t RepairBackupTask::getPayloadSize() const
{
    return 0;
}

TaskResult RepairBackupTask::process(BackupTaskContext& context)
{
    std::string errorMessage;
    FileRecord record;

    if (!context.catalog.getRecord(_relativePath, record))
    {
        return {false, "Запись для восстановления бэкапа не найдена: " + _relativePath};
    }

    const fs::path sourcePath = context.config.sourceDirectory / fs::path(_relativePath);
    std::error_code errorCode;
    const bool sourceExists = fs::exists(sourcePath, errorCode) && !errorCode;

    FileVersion donorVersion;
    const bool hasDonorOnDisk = findExistingContentVersion(record, context.config.backupDirectory, donorVersion);

    std::size_t repairedCount = 0;

    for (FileVersion& version : record.versions)
    {
        if (!isContentEvent(version.eventType) || version.backupRelativePath.empty())
        {
            continue;
        }

        const fs::path backupPath = context.config.backupDirectory / fs::path(version.backupRelativePath);

        if (fs::exists(backupPath, errorCode) && !errorCode)
        {
            continue;
        }

        errorCode.clear();

        if (sourceExists && record.state == RecordState::Active)
        {
            if (!copyBackupVersion(sourcePath, backupPath, errorMessage))
            {
                return {false, errorMessage};
            }

            ++repairedCount;
            continue;
        }

        if (!hasDonorOnDisk)
        {
            return {false, "Нет источника и других версий для восстановления бэкапа: " + _relativePath};
        }

        const fs::path donorPath = context.config.backupDirectory / fs::path(donorVersion.backupRelativePath);

        if (!copyBackupVersion(donorPath, backupPath, errorMessage))
        {
            return {false, errorMessage};
        }

        ++repairedCount;
    }

    if (repairedCount == 0)
    {
        return {true, "Файлы бэкапа на месте: " + _relativePath};
    }

    if (sourceExists && record.state == RecordState::Active)
    {
        record.lastKnownSize = fs::file_size(sourcePath, errorCode);

        if (errorCode)
        {
            return {false, "Не удалось получить размер source после восстановления бэкапа: " + errorCode.message()};
        }

        record.lastWriteTime = fileTimeToNumber(fs::last_write_time(sourcePath, errorCode));

        if (errorCode)
        {
            return {false, "Не удалось получить время source после восстановления бэкапа: " + errorCode.message()};
        }
    }

    context.catalog.saveRecord(record);

    if (!context.metadataRepository.save(context.metadataFilePath, context.config, context.catalog, errorMessage))
    {
        return {false, errorMessage};
    }

    return {true, "Восстановлено файлов в бэкапе: " + std::to_string(repairedCount) + " (" + _relativePath + ")"};
}

} // namespace backup
