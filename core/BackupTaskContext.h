#pragma once

#include "core/BackupTypes.h"

namespace backup
{

class BackupCatalog;
class MetadataRepository;

struct BackupTaskContext
{
    AppConfig config;
    BackupCatalog& catalog;
    MetadataRepository& metadataRepository;
    fs::path metadataFilePath;
};

} // namespace backup
