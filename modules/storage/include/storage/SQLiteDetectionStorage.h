#ifndef IVP_STORAGE_SQLITEDETECTIONSTORAGE_H
#define IVP_STORAGE_SQLITEDETECTIONSTORAGE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

#include "common/DetectionResult.h"

struct sqlite3;

namespace ivp
{

struct InspectionSessionSummary
{
    std::int64_t sessionId = 0;
    std::string sourceId;
    std::string inputUrl;
    std::int64_t startedAtMs = 0;
    std::optional<std::int64_t> endedAtMs;
    std::int64_t frameCount = 0;
    std::int64_t objectCount = 0;
};

struct DetectionHistoryQuery
{
    std::optional<std::int64_t> sessionId;
    std::optional<std::string> sourceLike;
    std::optional<std::string> classLike;
    std::optional<std::int64_t> recordedAfterMs;
    std::optional<std::int64_t> recordedBeforeMs;
    std::size_t limit = 200;
};

struct DetectionHistoryRow
{
    std::int64_t recordId = 0;
    std::int64_t sessionId = 0;
    std::string sourceId;
    std::string inputUrl;
    std::int64_t sessionStartedAtMs = 0;
    std::optional<std::int64_t> sessionEndedAtMs;
    std::int64_t frameIndex = 0;
    std::int64_t ptsMs = 0;
    std::int64_t recordedAtMs = 0;
    std::int64_t frameObjectCount = 0;
    int classId = -1;
    std::string className;
    float confidence = 0.0F;
    BoundingBox box;
};

using InspectionSessionSummaries = std::vector<InspectionSessionSummary>;
using DetectionHistoryRows = std::vector<DetectionHistoryRow>;

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

    InspectionSessionSummaries recentSessions(std::size_t maxCount) const;
    DetectionHistoryRows recentHistory(std::size_t maxCount) const;
    DetectionHistoryRows queryHistory(const DetectionHistoryQuery& query) const;

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
