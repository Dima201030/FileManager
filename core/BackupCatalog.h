#pragma once

#include "core/BackupTypes.h"

#include <mutex>
#include <vector>

namespace backup
{

class BackupCatalog final
{
public:
    BackupCatalog() = default;
    ~BackupCatalog() = default;

    void clear();
    void replaceAll(const std::vector<FileRecord>& records);
    void saveRecord(const FileRecord& record);
    bool erase(const std::string& relativePath);
    bool contains(const std::string& relativePath) const;
    bool getRecord(const std::string& relativePath, FileRecord& record) const;
    std::vector<FileRecord> listRecords() const;
    std::size_t size() const;

private:
    mutable std::mutex _mutex;
    std::vector<FileRecord> _records;
};

} // namespace backup
