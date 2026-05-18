#include "tasks/AddFileTask.h"

#include "core/BackupCatalog.h"
#include "core/BackupUtils.h"
#include "core/MetadataRepository.h"

#include <filesystem>

namespace backup
{

AddFileTask::AddFileTask(const FileSnapshot& snapshot)
    : _snapshot(snapshot)
{
}

std::string AddFileTask::getTaskName() const
{
    return "add";
}

std::string AddFileTask::getTaskKey() const
{
    return getTaskName() + ":" + _snapshot.relativePath;
}

std::string AddFileTask::getRelativePath() const
{
    return _snapshot.relativePath;
}

std::uintmax_t AddFileTask::getPayloadSize() const
{
    return _snapshot.fileSize;
}

TaskResult AddFileTask::process(BackupTaskContext& context)
{
    const fs::path sourcePath = context.config.sourceDirectory / fs::path(_snapshot.relativePath);
    const std::string versionId = makeVersionId();
    const fs::path storagePath = makeVersionStoragePath(context.config.backupDirectory, _snapshot.relativePath, versionId);

    std::error_code errorCode;
    fs::create_directories(storagePath.parent_path(), errorCode);

    if (errorCode)
    {
        return {false, "Не удалось создать каталог для версии файла: " + errorCode.message()};
    }

    fs::copy_file(sourcePath, storagePath, fs::copy_options::overwrite_existing, errorCode);

    if (errorCode)
    {
        return {false, "Не удалось скопировать новый файл '" + _snapshot.relativePath + "': " + errorCode.message()};
    }

    FileRecord record;
    record.relativePath = _snapshot.relativePath;
    record.state = RecordState::Active;
    record.lastKnownSize = _snapshot.fileSize;
    record.lastWriteTime = _snapshot.lastWriteTime;
    record.deletionDeadline = 0;
    record.versions.push_back({
        versionId,
        normalizeRelativePath(fs::relative(storagePath, context.config.backupDirectory)),
        currentTimestamp(),
        VersionEvent::Added,
        _snapshot.fileSize
    });

    context.catalog.saveRecord(record);

    std::string errorMessage;

    if (!context.metadataRepository.save(context.metadataFilePath, context.config, context.catalog, errorMessage))
    {
        return {false, errorMessage};
    }

    return {true, "Добавлен в бэкап: " + _snapshot.relativePath};
}

} // namespace backup
