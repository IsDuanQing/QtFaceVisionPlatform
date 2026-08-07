#ifndef IVP_STORAGE_SQLITEDETECTIONSTORAGE_H
#define IVP_STORAGE_SQLITEDETECTIONSTORAGE_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "common/DetectionResult.h"

struct sqlite3;

namespace ivp
{

class SQLiteDetectionStorage final
{
public:
    SQLiteDetectionStorage();
    ~SQLiteDetectionStorage();

    SQLiteDetectionStorage(const SQLiteDetectionStorage&) = delete;
    SQLiteDetectionStorage& operator=(const SQLiteDetectionStorage&) = delete;

    bool open(const std::string& databasePath);
    void close();

    bool isOpen() const;
    std::string databasePath() const;
    std::string lastError() const;

    std::int64_t startSession(const std::string& sourceId, const std::string& inputUrl);
    bool finishSession(std::int64_t sessionId);

    bool saveFrameResults(
        std::int64_t sessionId,
        const std::string& sourceId,
        std::int64_t frameIndex,
        std::int64_t ptsMs,
        const DetectionResults& results);

    DetectionResults recentResults(std::size_t maxCount) const;
    DetectionResults resultsForFrame(
        std::int64_t sessionId,
        std::int64_t frameIndex) const;

private:
    bool createSchemaLocked();
    bool executeLocked(const char* sql);
    bool insertFrameLocked(
        std::int64_t sessionId,
        const std::string& sourceId,
        std::int64_t frameIndex,
        std::int64_t ptsMs,
        std::size_t objectCount,
        std::int64_t recordedAtMs);
    bool insertDetectionLocked(
        std::int64_t sessionId,
        const std::string& fallbackSourceId,
        std::int64_t fallbackFrameIndex,
        std::int64_t fallbackPtsMs,
        const DetectionResult& result,
        std::int64_t recordedAtMs);
    DetectionResults readResultsLocked(
        const char* sql,
        std::int64_t firstParameter,
        std::int64_t secondParameter,
        bool bindSecondParameter) const;
    void setLastErrorLocked(const std::string& context) const;
    static std::int64_t currentTimeMs();

    sqlite3* database_;
    mutable std::mutex mutex_;
    std::string databasePath_;
    mutable std::string lastError_;
};

} // namespace ivp

#endif // IVP_STORAGE_SQLITEDETECTIONSTORAGE_H
