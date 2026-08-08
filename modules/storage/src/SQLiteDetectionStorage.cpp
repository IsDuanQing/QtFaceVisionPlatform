#include "storage/SQLiteDetectionStorage.h"

#include <chrono>
#include <limits>
#include <utility>

#include <sqlite3.h>

namespace
{

constexpr const char* kCreateSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS inspection_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id TEXT NOT NULL,
    input_url TEXT NOT NULL,
    started_at_ms INTEGER NOT NULL,
    ended_at_ms INTEGER
);

CREATE TABLE IF NOT EXISTS detection_frames (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    source_id TEXT NOT NULL,
    frame_index INTEGER NOT NULL,
    pts_ms INTEGER NOT NULL,
    object_count INTEGER NOT NULL,
    recorded_at_ms INTEGER NOT NULL,
    FOREIGN KEY(session_id) REFERENCES inspection_sessions(id)
);

CREATE TABLE IF NOT EXISTS detection_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    source_id TEXT NOT NULL,
    frame_index INTEGER NOT NULL,
    pts_ms INTEGER NOT NULL,
    class_id INTEGER NOT NULL,
    class_name TEXT NOT NULL,
    confidence REAL NOT NULL,
    box_x REAL NOT NULL,
    box_y REAL NOT NULL,
    box_width REAL NOT NULL,
    box_height REAL NOT NULL,
    recorded_at_ms INTEGER NOT NULL,
    FOREIGN KEY(session_id) REFERENCES inspection_sessions(id)
);

CREATE INDEX IF NOT EXISTS idx_detection_records_session_frame
ON detection_records(session_id, frame_index);

CREATE INDEX IF NOT EXISTS idx_detection_records_recorded_at
ON detection_records(recorded_at_ms);
)SQL";

constexpr const char* kInsertFrameSql =
    "INSERT INTO detection_frames ("
    "session_id, source_id, frame_index, pts_ms, object_count, recorded_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?);";

constexpr const char* kInsertDetectionSql =
    "INSERT INTO detection_records ("
    "session_id, source_id, frame_index, pts_ms, class_id, class_name, confidence, "
    "box_x, box_y, box_width, box_height, recorded_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kRecentResultsSql =
    "SELECT source_id, frame_index, pts_ms, class_id, class_name, confidence, "
    "box_x, box_y, box_width, box_height "
    "FROM detection_records ORDER BY id DESC LIMIT ?;";

constexpr const char* kFrameResultsSql =
    "SELECT source_id, frame_index, pts_ms, class_id, class_name, confidence, "
    "box_x, box_y, box_width, box_height "
    "FROM detection_records "
    "WHERE session_id = ? AND frame_index = ? ORDER BY id ASC;";

constexpr const char* kRecentSessionsSql = R"SQL(
SELECT
    s.id,
    s.source_id,
    s.input_url,
    s.started_at_ms,
    s.ended_at_ms,
    COALESCE((
        SELECT COUNT(*)
        FROM detection_frames f
        WHERE f.session_id = s.id
    ), 0) AS frame_count,
    COALESCE((
        SELECT COUNT(*)
        FROM detection_records r
        WHERE r.session_id = s.id
    ), 0) AS object_count
FROM inspection_sessions s
ORDER BY s.started_at_ms DESC, s.id DESC
LIMIT ?;
)SQL";

std::string textColumn(sqlite3_stmt* statement, int column)
{
    const unsigned char* text = sqlite3_column_text(statement, column);
    return text == nullptr ? std::string() : reinterpret_cast<const char*>(text);
}

std::optional<std::int64_t> optionalInt64Column(sqlite3_stmt* statement, int column)
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL)
    {
        return std::nullopt;
    }

    return sqlite3_column_int64(statement, column);
}

std::int64_t boundedLimit(std::size_t maxCount)
{
    return maxCount > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(maxCount);
}

std::string likePattern(const std::string& text)
{
    return "%" + text + "%";
}

} // namespace

namespace ivp
{

SQLiteDetectionStorage::SQLiteDetectionStorage()
    : database_(nullptr),
      mutex_(),
      databasePath_(),
      lastError_()
{
}

SQLiteDetectionStorage::~SQLiteDetectionStorage()
{
    close();
}

bool SQLiteDetectionStorage::open(const std::string& databasePath)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (database_ != nullptr)
    {
        sqlite3_close(database_);
        database_ = nullptr;
    }

    databasePath_.clear();
    lastError_.clear();
    if (databasePath.empty())
    {
        lastError_ = "SQLite database path is empty.";
        return false;
    }

    sqlite3* database = nullptr;
    const int openResult = sqlite3_open_v2(
        databasePath.c_str(),
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (openResult != SQLITE_OK)
    {
        lastError_ = database == nullptr
            ? "Could not open SQLite database."
            : sqlite3_errmsg(database);
        if (database != nullptr)
        {
            sqlite3_close(database);
        }
        return false;
    }

    database_ = database;
    databasePath_ = databasePath;

    if (!executeLocked("PRAGMA foreign_keys = ON;")
        || !executeLocked("PRAGMA journal_mode = WAL;")
        || !executeLocked("PRAGMA synchronous = NORMAL;")
        || !createSchemaLocked())
    {
        sqlite3_close(database_);
        database_ = nullptr;
        databasePath_.clear();
        return false;
    }

    lastError_.clear();
    return true;
}

void SQLiteDetectionStorage::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ != nullptr)
    {
        sqlite3_close(database_);
        database_ = nullptr;
    }

    databasePath_.clear();
}

bool SQLiteDetectionStorage::isOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return database_ != nullptr;
}

std::string SQLiteDetectionStorage::databasePath() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return databasePath_;
}

std::string SQLiteDetectionStorage::lastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

std::int64_t SQLiteDetectionStorage::startSession(
    const std::string& sourceId,
    const std::string& inputUrl)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return 0;
    }

    constexpr const char* sql =
        "INSERT INTO inspection_sessions (source_id, input_url, started_at_ms) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare inspection session insert");
        return 0;
    }

    const std::int64_t startedAtMs = currentTimeMs();
    const bool bound =
        sqlite3_bind_text(statement, 1, sourceId.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_text(statement, 2, inputUrl.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, startedAtMs) == SQLITE_OK;
    if (!bound || sqlite3_step(statement) != SQLITE_DONE)
    {
        setLastErrorLocked("Could not insert inspection session");
        sqlite3_finalize(statement);
        return 0;
    }

    sqlite3_finalize(statement);
    lastError_.clear();
    return sqlite3_last_insert_rowid(database_);
}

bool SQLiteDetectionStorage::finishSession(std::int64_t sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || sessionId <= 0)
    {
        lastError_ = "SQLite database is not open or session id is invalid.";
        return false;
    }

    constexpr const char* sql =
        "UPDATE inspection_sessions SET ended_at_ms = ? WHERE id = ?;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare inspection session update");
        return false;
    }

    const bool bound =
        sqlite3_bind_int64(statement, 1, currentTimeMs()) == SQLITE_OK
        && sqlite3_bind_int64(statement, 2, sessionId) == SQLITE_OK;
    const bool completed =
        bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not finish inspection session");
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return completed;
}

bool SQLiteDetectionStorage::saveFrameResults(
    std::int64_t sessionId,
    const std::string& sourceId,
    std::int64_t frameIndex,
    std::int64_t ptsMs,
    const DetectionResults& results)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || sessionId <= 0)
    {
        lastError_ = "SQLite database is not open or session id is invalid.";
        return false;
    }

    // The frame row and all detections must be committed atomically.
    if (!executeLocked("BEGIN IMMEDIATE TRANSACTION;"))
    {
        return false;
    }

    const std::int64_t recordedAtMs = currentTimeMs();
    bool saved = insertFrameLocked(
        sessionId,
        sourceId,
        frameIndex,
        ptsMs,
        results.size(),
        recordedAtMs);

    for (const DetectionResult& result : results)
    {
        if (!saved)
        {
            break;
        }

        saved = insertDetectionLocked(
            sessionId,
            sourceId,
            frameIndex,
            ptsMs,
            result,
            recordedAtMs);
    }

    if (!saved)
    {
        executeLocked("ROLLBACK;");
        return false;
    }

    if (!executeLocked("COMMIT;"))
    {
        executeLocked("ROLLBACK;");
        return false;
    }

    lastError_.clear();
    return true;
}

DetectionResults SQLiteDetectionStorage::recentResults(std::size_t maxCount) const
{
    if (maxCount == 0)
    {
        return {};
    }

    const std::int64_t limit = boundedLimit(maxCount);
    return readResultsLocked(kRecentResultsSql, limit, 0, false);
}

DetectionResults SQLiteDetectionStorage::resultsForFrame(
    std::int64_t sessionId,
    std::int64_t frameIndex) const
{
    return readResultsLocked(kFrameResultsSql, sessionId, frameIndex, true);
}

InspectionSessionSummaries SQLiteDetectionStorage::recentSessions(std::size_t maxCount) const
{
    if (maxCount == 0)
    {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return {};
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kRecentSessionsSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare inspection session query");
        return {};
    }

    if (sqlite3_bind_int64(statement, 1, boundedLimit(maxCount)) != SQLITE_OK)
    {
        setLastErrorLocked("Could not bind inspection session query");
        sqlite3_finalize(statement);
        return {};
    }

    InspectionSessionSummaries sessions;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        InspectionSessionSummary session;
        session.sessionId = sqlite3_column_int64(statement, 0);
        session.sourceId = textColumn(statement, 1);
        session.inputUrl = textColumn(statement, 2);
        session.startedAtMs = sqlite3_column_int64(statement, 3);
        session.endedAtMs = optionalInt64Column(statement, 4);
        session.frameCount = sqlite3_column_int64(statement, 5);
        session.objectCount = sqlite3_column_int64(statement, 6);
        sessions.push_back(std::move(session));
    }

    if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not read inspection sessions");
        sessions.clear();
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return sessions;
}

DetectionHistoryRows SQLiteDetectionStorage::recentHistory(std::size_t maxCount) const
{
    DetectionHistoryQuery query;
    query.limit = maxCount;
    return queryHistory(query);
}

DetectionHistoryRows SQLiteDetectionStorage::queryHistory(
    const DetectionHistoryQuery& query) const
{
    if (query.limit == 0)
    {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return {};
    }

    std::string sql = R"SQL(
SELECT
    r.id,
    r.session_id,
    r.source_id,
    s.input_url,
    s.started_at_ms,
    s.ended_at_ms,
    r.frame_index,
    r.pts_ms,
    r.recorded_at_ms,
    COALESCE((
        SELECT f.object_count
        FROM detection_frames f
        WHERE f.session_id = r.session_id AND f.frame_index = r.frame_index
        ORDER BY f.id DESC
        LIMIT 1
    ), 0),
    r.class_id,
    r.class_name,
    r.confidence,
    r.box_x,
    r.box_y,
    r.box_width,
    r.box_height
FROM detection_records r
INNER JOIN inspection_sessions s ON s.id = r.session_id
WHERE 1 = 1
)SQL";

    if (query.sessionId.has_value())
    {
        sql += " AND r.session_id = ?";
    }
    if (query.sourceLike.has_value() && !query.sourceLike->empty())
    {
        sql += " AND (r.source_id LIKE ? COLLATE NOCASE"
               " OR s.source_id LIKE ? COLLATE NOCASE"
               " OR s.input_url LIKE ? COLLATE NOCASE)";
    }
    if (query.classLike.has_value() && !query.classLike->empty())
    {
        sql += " AND r.class_name LIKE ? COLLATE NOCASE";
    }
    if (query.recordedAfterMs.has_value())
    {
        sql += " AND r.recorded_at_ms >= ?";
    }
    if (query.recordedBeforeMs.has_value())
    {
        sql += " AND r.recorded_at_ms <= ?";
    }
    sql += " ORDER BY r.recorded_at_ms DESC, r.id DESC LIMIT ?;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare detection history query");
        return {};
    }

    int parameterIndex = 1;
    bool bound = true;
    auto bindInt64 = [&](std::int64_t value) {
        bound = bound
            && sqlite3_bind_int64(statement, parameterIndex++, value) == SQLITE_OK;
    };
    auto bindText = [&](const std::string& value) {
        bound = bound
            && sqlite3_bind_text(statement, parameterIndex++, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    };

    if (query.sessionId.has_value())
    {
        bindInt64(*query.sessionId);
    }
    if (query.sourceLike.has_value() && !query.sourceLike->empty())
    {
        const std::string pattern = likePattern(*query.sourceLike);
        bindText(pattern);
        bindText(pattern);
        bindText(pattern);
    }
    if (query.classLike.has_value() && !query.classLike->empty())
    {
        bindText(likePattern(*query.classLike));
    }
    if (query.recordedAfterMs.has_value())
    {
        bindInt64(*query.recordedAfterMs);
    }
    if (query.recordedBeforeMs.has_value())
    {
        bindInt64(*query.recordedBeforeMs);
    }
    bindInt64(boundedLimit(query.limit));

    if (!bound)
    {
        setLastErrorLocked("Could not bind detection history query");
        sqlite3_finalize(statement);
        return {};
    }

    DetectionHistoryRows historyRows;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        DetectionHistoryRow row;
        row.recordId = sqlite3_column_int64(statement, 0);
        row.sessionId = sqlite3_column_int64(statement, 1);
        row.sourceId = textColumn(statement, 2);
        row.inputUrl = textColumn(statement, 3);
        row.sessionStartedAtMs = sqlite3_column_int64(statement, 4);
        row.sessionEndedAtMs = optionalInt64Column(statement, 5);
        row.frameIndex = sqlite3_column_int64(statement, 6);
        row.ptsMs = sqlite3_column_int64(statement, 7);
        row.recordedAtMs = sqlite3_column_int64(statement, 8);
        row.frameObjectCount = sqlite3_column_int64(statement, 9);
        row.classId = sqlite3_column_int(statement, 10);
        row.className = textColumn(statement, 11);
        row.confidence = static_cast<float>(sqlite3_column_double(statement, 12));
        row.box.x = static_cast<float>(sqlite3_column_double(statement, 13));
        row.box.y = static_cast<float>(sqlite3_column_double(statement, 14));
        row.box.width = static_cast<float>(sqlite3_column_double(statement, 15));
        row.box.height = static_cast<float>(sqlite3_column_double(statement, 16));
        historyRows.push_back(std::move(row));
    }

    if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not read detection history");
        historyRows.clear();
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return historyRows;
}

bool SQLiteDetectionStorage::createSchemaLocked()
{
    return executeLocked(kCreateSchemaSql);
}

bool SQLiteDetectionStorage::executeLocked(const char* sql)
{
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return false;
    }

    char* message = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK)
    {
        return true;
    }

    lastError_ = message == nullptr ? sqlite3_errmsg(database_) : message;
    sqlite3_free(message);
    return false;
}

bool SQLiteDetectionStorage::insertFrameLocked(
    std::int64_t sessionId,
    const std::string& sourceId,
    std::int64_t frameIndex,
    std::int64_t ptsMs,
    std::size_t objectCount,
    std::int64_t recordedAtMs)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kInsertFrameSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare detection frame insert");
        return false;
    }

    const std::int64_t storedObjectCount = objectCount
        > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(objectCount);
    const bool bound =
        sqlite3_bind_int64(statement, 1, sessionId) == SQLITE_OK
        && sqlite3_bind_text(statement, 2, sourceId.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, frameIndex) == SQLITE_OK
        && sqlite3_bind_int64(statement, 4, ptsMs) == SQLITE_OK
        && sqlite3_bind_int64(statement, 5, storedObjectCount) == SQLITE_OK
        && sqlite3_bind_int64(statement, 6, recordedAtMs) == SQLITE_OK;
    const bool completed =
        bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not insert detection frame");
    }

    sqlite3_finalize(statement);
    return completed;
}

bool SQLiteDetectionStorage::insertDetectionLocked(
    std::int64_t sessionId,
    const std::string& fallbackSourceId,
    std::int64_t fallbackFrameIndex,
    std::int64_t fallbackPtsMs,
    const DetectionResult& result,
    std::int64_t recordedAtMs)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kInsertDetectionSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare detection record insert");
        return false;
    }

    const std::string sourceId = result.sourceId.empty()
        ? fallbackSourceId
        : result.sourceId;
    const std::int64_t frameIndex = result.frameIndex == 0 && fallbackFrameIndex != 0
        ? fallbackFrameIndex
        : result.frameIndex;
    const std::int64_t ptsMs = result.ptsMs == 0 && fallbackPtsMs != 0
        ? fallbackPtsMs
        : result.ptsMs;
    const bool bound =
        sqlite3_bind_int64(statement, 1, sessionId) == SQLITE_OK
        && sqlite3_bind_text(statement, 2, sourceId.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, frameIndex) == SQLITE_OK
        && sqlite3_bind_int64(statement, 4, ptsMs) == SQLITE_OK
        && sqlite3_bind_int(statement, 5, result.classId) == SQLITE_OK
        && sqlite3_bind_text(statement, 6, result.className.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_double(statement, 7, result.confidence) == SQLITE_OK
        && sqlite3_bind_double(statement, 8, result.box.x) == SQLITE_OK
        && sqlite3_bind_double(statement, 9, result.box.y) == SQLITE_OK
        && sqlite3_bind_double(statement, 10, result.box.width) == SQLITE_OK
        && sqlite3_bind_double(statement, 11, result.box.height) == SQLITE_OK
        && sqlite3_bind_int64(statement, 12, recordedAtMs) == SQLITE_OK;
    const bool completed =
        bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not insert detection record");
    }

    sqlite3_finalize(statement);
    return completed;
}

DetectionResults SQLiteDetectionStorage::readResultsLocked(
    const char* sql,
    std::int64_t firstParameter,
    std::int64_t secondParameter,
    bool bindSecondParameter) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return {};
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare detection result query");
        return {};
    }

    const bool bound = bindSecondParameter
        ? sqlite3_bind_int64(statement, 1, firstParameter) == SQLITE_OK
              && sqlite3_bind_int64(statement, 2, secondParameter) == SQLITE_OK
        : sqlite3_bind_int64(statement, 1, firstParameter) == SQLITE_OK;
    if (!bound)
    {
        setLastErrorLocked("Could not bind detection result query");
        sqlite3_finalize(statement);
        return {};
    }

    DetectionResults results;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        DetectionResult result;
        result.sourceId = textColumn(statement, 0);
        result.frameIndex = sqlite3_column_int64(statement, 1);
        result.ptsMs = sqlite3_column_int64(statement, 2);
        result.classId = sqlite3_column_int(statement, 3);
        result.className = textColumn(statement, 4);
        result.confidence = static_cast<float>(sqlite3_column_double(statement, 5));
        result.box.x = static_cast<float>(sqlite3_column_double(statement, 6));
        result.box.y = static_cast<float>(sqlite3_column_double(statement, 7));
        result.box.width = static_cast<float>(sqlite3_column_double(statement, 8));
        result.box.height = static_cast<float>(sqlite3_column_double(statement, 9));
        results.push_back(std::move(result));
    }

    if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not read detection results");
        results.clear();
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return results;
}

void SQLiteDetectionStorage::setLastErrorLocked(const std::string& context) const
{
    lastError_ = database_ == nullptr
        ? context
        : context + ": " + sqlite3_errmsg(database_);
}

std::int64_t SQLiteDetectionStorage::currentTimeMs()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

} // namespace ivp
