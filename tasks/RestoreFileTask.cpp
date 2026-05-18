#include "tasks/RestoreFileTask.h"

#include "core/BackupCatalog.h"
#include "core/BackupUtils.h"
#include "core/MetadataRepository.h"

#include <filesystem>

namespace backup
{

RestoreFileTask::RestoreFileTask(std::string relativePath)
    : _relativePath(std::move(relativePath))
{
}

std::string RestoreFileTask::getTaskName() const
{
    return "restore";
}

std::string RestoreFileTask::getTaskKey() const
{
    return getTaskName() + ":" + _relativePath;
}

std::string RestoreFileTask::getRelativePath() const
{
    return _relativePath;
}

std::uintmax_t RestoreFileTask::getPayloadSize() const
{
    return 0;
}

TaskResult RestoreFileTask::process(BackupTaskContext& context)
{
    FileRecord record;

    if (!context.catalog.getRecord(_relativePath, record))
    {
        return {false, "Файл не найден в каталоге бэкапа: " + _relativePath};
    }

    FileVersion sourceVersion;

    if (!findLatestContentVersion(record, sourceVersion))
    {
        return {false, "Для файла нет доступной версии восстановления: " + _relativePath};
    }

    const fs::path backupFile = context.config.backupDirectory / fs::path(sourceVersion.backupRelativePath);
    const fs::path targetFile = context.config.sourceDirectory / fs::path(_relativePath);

    std::error_code errorCode;
    fs::create_directories(targetFile.parent_path(), errorCode);

    if (errorCode)
    {
        return {false, "Не удалось создать каталог для восстановления: " + errorCode.message()};
    }

    fs::copy_file(backupFile, targetFile, fs::copy_options::overwrite_existing, errorCode);

    if (errorCode)
    {
        return {false, "Не удалось восстановить файл '" + _relativePath + "': " + errorCode.message()};
    }

    const std::string versionId = makeVersionId();
    const fs::path newStoragePath = makeVersionStoragePath(context.config.backupDirectory, _relativePath, versionId);

    fs::create_directories(newStoragePath.parent_path(), errorCode);

    if (errorCode)
    {
        return {false, "Не удалось создать каталог версии после восстановления: " + errorCode.message()};
    }

    fs::copy_file(targetFile, newStoragePath, fs::copy_options::overwrite_existing, errorCode);

    if (errorCode)
    {
        return {false, "Не удалось сохранить восстановленную версию '" + _relativePath + "': " + errorCode.message()};
    }

    record.state = RecordState::Active;
    record.deletionDeadline = 0;
    record.lastKnownSize = fs::file_size(targetFile, errorCode);

    if (errorCode)
    {
        return {false, "Не удалось получить размер восстановленного файла: " + errorCode.message()};
    }

    record.lastWriteTime = fileTimeToNumber(fs::last_write_time(targetFile, errorCode));

    if (errorCode)
    {
        return {false, "Не удалось получить время изменения восстановленного файла: " + errorCode.message()};
    }

    record.versions.push_back({
        versionId,
        normalizeRelativePath(fs::relative(newStoragePath, context.config.backupDirectory)),
        currentTimestamp(),
        VersionEvent::Restored,
        record.lastKnownSize
    });

    context.catalog.saveRecord(record);

    std::string errorMessage;

    if (!context.metadataRepository.save(context.metadataFilePath, context.config, context.catalog, errorMessage))
    {
        return {false, errorMessage};
    }

    return {true, "Файл восстановлен: " + _relativePath};
}

} // namespace backup
