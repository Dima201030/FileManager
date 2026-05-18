#include "tasks/ModifyFileTask.h"

#include "core/BackupCatalog.h"
#include "core/BackupUtils.h"
#include "core/MetadataRepository.h"

#include <filesystem>

namespace backup
{

ModifyFileTask::ModifyFileTask(const FileSnapshot& snapshot)
    : _snapshot(snapshot)
{
}

std::string ModifyFileTask::getTaskName() const
{
    return "modify";
}

std::string ModifyFileTask::getTaskKey() const
{
    return getTaskName() + ":" + _snapshot.relativePath;
}

std::string ModifyFileTask::getRelativePath() const
{
    return _snapshot.relativePath;
}

std::uintmax_t ModifyFileTask::getPayloadSize() const
{
    return _snapshot.fileSize;
}

TaskResult ModifyFileTask::process(BackupTaskContext& context)
{
    FileRecord record;

    if (context.catalog.getRecord(_snapshot.relativePath, record))
    {
        pruneSmallerContentVersions(record, _snapshot.fileSize, context.config.backupDirectory);
        context.catalog.saveRecord(record);
    }

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
        return {false, "Не удалось сохранить новую версию файла '" + _snapshot.relativePath + "': " + errorCode.message()};
    }

    if (!context.catalog.getRecord(_snapshot.relativePath, record))
    {
        record = FileRecord{};
    }

    record.relativePath = _snapshot.relativePath;
    record.state = RecordState::Active;
    record.lastKnownSize = _snapshot.fileSize;
    record.lastWriteTime = _snapshot.lastWriteTime;
    record.deletionDeadline = 0;
    record.versions.push_back({
        versionId,
        normalizeRelativePath(fs::relative(storagePath, context.config.backupDirectory)),
        currentTimestamp(),
        VersionEvent::Modified,
        _snapshot.fileSize
    });

    context.catalog.saveRecord(record);

    std::string errorMessage;

    if (!context.metadataRepository.save(context.metadataFilePath, context.config, context.catalog, errorMessage))
    {
        return {false, errorMessage};
    }

    return {true, "Обновлена версия файла: " + _snapshot.relativePath};
}

} // namespace backup
