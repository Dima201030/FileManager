#pragma once

#include <filesystem>

namespace backup
{
namespace fs = std::filesystem;

bool runAsBackgroundDaemon(const fs::path& logFilePath);

} // namespace backup
