#include "tasks/DeleteFileTask.h"

#include "core/BackupCatalog.h"
#include "core/BackupTypes.h"
#include "core/MetadataRepository.h"

#include <utility>

namespace backup
{

DeleteFileTask::DeleteFileTask(std::string relativePath, const long long eventTimestamp, const long long deletionDeadline)
    : _relativePath(std::move(relativePath))
    , _eventTimestamp(eventTimestamp)
    , _deletionDeadline(deletionDeadline)
{
}

std::string DeleteFileTask::getTaskName() const
{
    return "delete";
}

std::string DeleteFileTask::getTaskKey() const
{
    return getTaskName() + ":" + _relativePath;
}

std::string DeleteFileTask::getRelativePath() const
{
    return _relativePath;
}

std::uintmax_t DeleteFileTask::getPayloadSize() const
{
    return 0;
}

TaskResult DeleteFileTask::process(BackupTaskContext& context)
{
    FileRecord record;

    if (!context.catalog.getRecord(_relativePath, record))
    {
        return {false, "Запись для удаления не найдена: " + _relativePath};
    }

    if (record.state == RecordState::PendingDeletion)
    {
        return {true, "Файл уже помечен на удаление: " + _relativePath};
    }

    record.state = RecordState::PendingDeletion;
    record.deletionDeadline = _deletionDeadline;
    record.versions.push_back({
        {},
        {},
        _eventTimestamp,
        VersionEvent::Deleted,
        record.lastKnownSize
    });

    context.catalog.saveRecord(record);

    std::string errorMessage;

    if (!context.metadataRepository.save(context.metadataFilePath, context.config, context.catalog, errorMessage))
    {
        return {false, errorMessage};
    }

    return {true, "Файл помечен на удаление через "
        + std::to_string(context.config.retentionDays)
        + " дней: "
        + _relativePath};
}

} // namespace backup
