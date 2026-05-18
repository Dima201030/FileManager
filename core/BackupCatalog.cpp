#include "core/BackupCatalog.h"

#include <algorithm>

namespace backup
{

void BackupCatalog::clear()
{
    std::lock_guard<std::mutex> guard(_mutex);
    _records.clear();
}

void BackupCatalog::replaceAll(const std::vector<FileRecord>& records)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _records = records;
}

void BackupCatalog::saveRecord(const FileRecord& record)
{
    std::lock_guard<std::mutex> guard(_mutex);

    for (FileRecord& currentRecord : _records)
    {
        if (currentRecord.relativePath == record.relativePath)
        {
            currentRecord = record;
            return;
        }
    }

    _records.push_back(record);
}

bool BackupCatalog::erase(const std::string& relativePath)
{
    std::lock_guard<std::mutex> guard(_mutex);

    const auto iterator = std::find_if(_records.begin(), _records.end(), [&relativePath](const FileRecord& record)
    {
        return record.relativePath == relativePath;
    });

    if (iterator == _records.end())
    {
        return false;
    }

    _records.erase(iterator);
    return true;
}

bool BackupCatalog::contains(const std::string& relativePath) const
{
    std::lock_guard<std::mutex> guard(_mutex);

    return std::find_if(_records.begin(), _records.end(), [&relativePath](const FileRecord& record)
    {
        return record.relativePath == relativePath;
    }) != _records.end();
}

bool BackupCatalog::getRecord(const std::string& relativePath, FileRecord& record) const
{
    std::lock_guard<std::mutex> guard(_mutex);

    const auto iterator = std::find_if(_records.begin(), _records.end(), [&relativePath](const FileRecord& currentRecord)
    {
        return currentRecord.relativePath == relativePath;
    });

    if (iterator == _records.end())
    {
        return false;
    }

    record = *iterator;
    return true;
}

std::vector<FileRecord> BackupCatalog::listRecords() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _records;
}

std::size_t BackupCatalog::size() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _records.size();
}

} // namespace backup
