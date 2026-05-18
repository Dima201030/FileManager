#pragma once

#include "core/BackupCatalog.h"
#include "core/BackupTypes.h"

namespace backup
{

class MetadataRepository final
{
public:
    MetadataRepository() = default;
    ~MetadataRepository() = default;

    bool load(const fs::path& metadataFile,
              AppConfig& config,
              BackupCatalog& catalog,
              std::string& errorMessage) const;

    bool save(const fs::path& metadataFile,
              const AppConfig& config,
              const BackupCatalog& catalog,
              std::string& errorMessage) const;
};

} // namespace backup
