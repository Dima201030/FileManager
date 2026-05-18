#include "core/MetadataRepository.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace backup
{
namespace
{
constexpr const char* MetadataHeader = "BACKUP_METADATA_V1";

std::vector<std::string> splitMetadataTokens(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string currentToken;
    bool insideQuotes = false;

    for (const char symbol : line)
    {
        if (symbol == '"')
        {
            insideQuotes = !insideQuotes;
            currentToken.push_back(symbol);
            continue;
        }

        if (symbol == ' ' && !insideQuotes)
        {
            if (!currentToken.empty())
            {
                tokens.push_back(currentToken);
                currentToken.clear();
            }

            continue;
        }

        currentToken.push_back(symbol);
    }

    if (!currentToken.empty())
    {
        tokens.push_back(currentToken);
    }

    return tokens;
}

std::string unquoteToken(const std::string& token)
{
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
    {
        return token.substr(1, token.size() - 2);
    }

    return token;
}

std::size_t parsePositiveValue(const std::string& token, const std::string& fieldName)
{
    try
    {
        const std::size_t value = static_cast<std::size_t>(std::stoull(token));

        if (value == 0)
        {
            throw std::invalid_argument(fieldName);
        }

        return value;
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument("Некорректное значение поля " + fieldName + ": " + token);
    }
}

long long parseSignedValue(const std::string& token, const std::string& fieldName)
{
    try
    {
        return std::stoll(token);
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument("Некорректное значение поля " + fieldName + ": " + token);
    }
}

std::uintmax_t parseSizeValue(const std::string& token, const std::string& fieldName)
{
    try
    {
        return static_cast<std::uintmax_t>(std::stoull(token));
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument("Некорректное значение поля " + fieldName + ": " + token);
    }
}

} // namespace

bool MetadataRepository::load(const fs::path& metadataFile,
                              AppConfig& config,
                              BackupCatalog& catalog,
                              std::string& errorMessage) const
{
    if (!fs::exists(metadataFile))
    {
        catalog.clear();
        return true;
    }

    std::ifstream input(metadataFile);

    if (!input.is_open())
    {
        errorMessage = "Не удалось открыть файл метаданных: " + metadataFile.string();
        return false;
    }

    std::string header;
    std::getline(input, header);

    if (header != MetadataHeader)
    {
        errorMessage = "Файл метаданных имеет неверный формат";
        return false;
    }

    AppConfig loadedConfig = config;
    std::vector<FileRecord> loadedRecords;
    std::string line;
    bool insideRecord = false;
    FileRecord currentRecord;

    while (std::getline(input, line))
    {
        if (line.empty())
        {
            continue;
        }

        try
        {
            const std::vector<std::string> tokens = splitMetadataTokens(line);

            if (tokens.empty())
            {
                continue;
            }

            const std::string& section = tokens[0];

            if (section == "CONFIG")
            {
                if (tokens.size() < 4)
                {
                    throw std::invalid_argument("CONFIG должен содержать retention, scan-interval и source");
                }

                loadedConfig.retentionDays = parsePositiveValue(tokens[1], "retention-days");
                loadedConfig.scanIntervalSeconds = parsePositiveValue(tokens[2], "scan-interval");
                loadedConfig.sourceDirectory = unquoteToken(tokens[3]);
                continue;
            }

            if (section == "RECORD")
            {
                if (insideRecord)
                {
                    throw std::invalid_argument("RECORD внутри RECORD");
                }

                if (tokens.size() < 6)
                {
                    throw std::invalid_argument("RECORD содержит недостаточно полей");
                }

                currentRecord = FileRecord{};
                currentRecord.relativePath = unquoteToken(tokens[1]);
                currentRecord.state = static_cast<RecordState>(parseSignedValue(tokens[2], "state"));
                currentRecord.lastWriteTime = parseSignedValue(tokens[3], "last-write-time");
                currentRecord.lastKnownSize = parseSizeValue(tokens[4], "last-known-size");
                currentRecord.deletionDeadline = parseSignedValue(tokens[5], "deletion-deadline");
                currentRecord.versions.clear();
                insideRecord = true;
                continue;
            }

            if (section == "VERSION")
            {
                if (!insideRecord)
                {
                    throw std::invalid_argument("VERSION встретился вне RECORD");
                }

                if (tokens.size() < 6)
                {
                    throw std::invalid_argument("VERSION содержит недостаточно полей");
                }

                FileVersion version;
                version.versionId = unquoteToken(tokens[1]);
                version.backupRelativePath = unquoteToken(tokens[2]);
                version.timestamp = parseSignedValue(tokens[3], "timestamp");
                version.eventType = static_cast<VersionEvent>(parseSignedValue(tokens[4], "event-type"));
                version.fileSize = parseSizeValue(tokens[5], "file-size");
                currentRecord.versions.push_back(version);
                continue;
            }

            if (section == "ENDRECORD")
            {
                if (!insideRecord)
                {
                    throw std::invalid_argument("ENDRECORD встретился без RECORD");
                }

                loadedRecords.push_back(currentRecord);
                currentRecord = FileRecord{};
                insideRecord = false;
                continue;
            }

            throw std::invalid_argument("Неизвестная секция: " + section);
        }
        catch (const std::exception& exception)
        {
            errorMessage = exception.what();
            return false;
        }
    }

    if (insideRecord)
    {
        errorMessage = "Файл метаданных оборван внутри RECORD";
        return false;
    }

    config = loadedConfig;
    catalog.replaceAll(loadedRecords);
    return true;
}

bool MetadataRepository::save(const fs::path& metadataFile,
                              const AppConfig& config,
                              const BackupCatalog& catalog,
                              std::string& errorMessage) const
{
    std::error_code errorCode;
    fs::create_directories(metadataFile.parent_path(), errorCode);

    if (errorCode)
    {
        errorMessage = "Не удалось создать каталог метаданных: " + errorCode.message();
        return false;
    }

    std::ofstream output(metadataFile, std::ios::trunc);

    if (!output.is_open())
    {
        errorMessage = "Не удалось открыть файл метаданных на запись: " + metadataFile.string();
        return false;
    }

    output << MetadataHeader << '\n';
    output << "CONFIG "
           << config.retentionDays << ' '
           << config.scanIntervalSeconds << ' '
           << std::quoted(config.sourceDirectory.string()) << '\n';

    std::vector<FileRecord> records = catalog.listRecords();
    std::sort(records.begin(), records.end(), [](const FileRecord& left, const FileRecord& right)
    {
        return left.relativePath < right.relativePath;
    });

    for (const FileRecord& record : records)
    {
        output << "RECORD "
               << std::quoted(record.relativePath) << ' '
               << static_cast<int>(record.state) << ' '
               << record.lastWriteTime << ' '
               << record.lastKnownSize << ' '
               << record.deletionDeadline << '\n';

        for (const FileVersion& version : record.versions)
        {
            output << "VERSION "
                   << std::quoted(version.versionId) << ' '
                   << std::quoted(version.backupRelativePath) << ' '
                   << version.timestamp << ' '
                   << static_cast<int>(version.eventType) << ' '
                   << version.fileSize << '\n';
        }

        output << "ENDRECORD\n";
    }

    return true;
}

} // namespace backup
