#include "storage/SQLiteDetectionStorage.h"

#include <algorithm>
#include <chrono>
#include <cstring>
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

CREATE TABLE IF NOT EXISTS face_identities (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    face_code TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    reference_image_path TEXT NOT NULL,
    notes TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS detection_face_links (
    detection_record_id INTEGER PRIMARY KEY,
    face_identity_id INTEGER NOT NULL,
    face_code TEXT NOT NULL,
    face_name TEXT NOT NULL,
    matched_at_ms INTEGER NOT NULL,
    distance REAL NOT NULL DEFAULT 0,
    similarity REAL NOT NULL DEFAULT 0,
    threshold_value REAL NOT NULL DEFAULT 0,
    recognizer_name TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(detection_record_id) REFERENCES detection_records(id) ON DELETE CASCADE,
    FOREIGN KEY(face_identity_id) REFERENCES face_identities(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS face_feature_templates (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    face_identity_id INTEGER NOT NULL,
    model_name TEXT NOT NULL,
    feature_fingerprint TEXT NOT NULL DEFAULT '',
    sample_image_path TEXT NOT NULL,
    feature_values BLOB NOT NULL,
    value_count INTEGER NOT NULL,
    created_at_ms INTEGER NOT NULL,
    FOREIGN KEY(face_identity_id) REFERENCES face_identities(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_face_feature_templates_identity
ON face_feature_templates(face_identity_id);

CREATE INDEX IF NOT EXISTS idx_face_feature_templates_model
ON face_feature_templates(model_name);

CREATE TABLE IF NOT EXISTS face_recognition_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    detection_record_id INTEGER,
    session_id INTEGER NOT NULL,
    source_id TEXT NOT NULL,
    frame_index INTEGER NOT NULL,
    pts_ms INTEGER NOT NULL,
    event_type TEXT NOT NULL,
    face_identity_id INTEGER,
    face_code TEXT NOT NULL,
    face_name TEXT NOT NULL,
    distance REAL NOT NULL,
    similarity REAL NOT NULL,
    threshold_value REAL NOT NULL,
    recognizer_name TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    FOREIGN KEY(detection_record_id) REFERENCES detection_records(id) ON DELETE CASCADE,
    FOREIGN KEY(face_identity_id) REFERENCES face_identities(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_face_recognition_events_created_at
ON face_recognition_events(created_at_ms);

CREATE INDEX IF NOT EXISTS idx_face_recognition_events_dedup
ON face_recognition_events(
    session_id,
    source_id,
    face_identity_id,
    event_type,
    created_at_ms
);
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
    "box_x, box_y, box_width, box_height, "
    "l.face_identity_id, l.face_code, l.face_name, l.matched_at_ms, "
    "l.distance, l.similarity, l.threshold_value, l.recognizer_name "
    "FROM detection_records r "
    "LEFT JOIN detection_face_links l ON l.detection_record_id = r.id "
    "ORDER BY r.id DESC LIMIT ?;";

constexpr const char* kFrameResultsSql =
    "SELECT source_id, frame_index, pts_ms, class_id, class_name, confidence, "
    "box_x, box_y, box_width, box_height, "
    "l.face_identity_id, l.face_code, l.face_name, l.matched_at_ms, "
    "l.distance, l.similarity, l.threshold_value, l.recognizer_name "
    "FROM detection_records r "
    "LEFT JOIN detection_face_links l ON l.detection_record_id = r.id "
    "WHERE r.session_id = ? AND r.frame_index = ? ORDER BY r.id ASC;";

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

constexpr const char* kSaveFaceIdentitySql = R"SQL(
INSERT INTO face_identities (
    face_code, display_name, reference_image_path, notes, created_at_ms, updated_at_ms
) VALUES (?, ?, ?, ?, COALESCE((SELECT created_at_ms FROM face_identities WHERE face_code = ?), ?), ?)
ON CONFLICT(face_code) DO UPDATE SET
    display_name = excluded.display_name,
    reference_image_path = excluded.reference_image_path,
    notes = excluded.notes,
    updated_at_ms = excluded.updated_at_ms;
)SQL";

constexpr const char* kDeleteFaceIdentitySql =
    "DELETE FROM face_identities WHERE id = ?;";

constexpr const char* kFaceIdentitiesSql =
    "SELECT id, face_code, display_name, reference_image_path, notes, created_at_ms, updated_at_ms "
    "FROM face_identities ORDER BY updated_at_ms DESC, id DESC LIMIT ?;";

constexpr const char* kBindFaceIdentitySql =
    "INSERT INTO detection_face_links "
    "(detection_record_id, face_identity_id, face_code, face_name, matched_at_ms, "
    "distance, similarity, threshold_value, recognizer_name) "
    "SELECT ?, id, face_code, display_name, ?, 0, 0, 0, 'manual' "
    "FROM face_identities WHERE id = ? "
    "ON CONFLICT(detection_record_id) DO UPDATE SET "
    "face_identity_id = excluded.face_identity_id, "
    "face_code = excluded.face_code, "
    "face_name = excluded.face_name, "
    "matched_at_ms = excluded.matched_at_ms, "
    "distance = excluded.distance, "
    "similarity = excluded.similarity, "
    "threshold_value = excluded.threshold_value, "
    "recognizer_name = excluded.recognizer_name;";

constexpr const char* kInsertRecognizedFaceLinkSql =
    "INSERT INTO detection_face_links "
    "(detection_record_id, face_identity_id, face_code, face_name, matched_at_ms, "
    "distance, similarity, threshold_value, recognizer_name) "
    "SELECT ?, id, face_code, display_name, ?, ?, ?, ?, ? "
    "FROM face_identities WHERE id = ? "
    "ON CONFLICT(detection_record_id) DO UPDATE SET "
    "face_identity_id = excluded.face_identity_id, "
    "face_code = excluded.face_code, "
    "face_name = excluded.face_name, "
    "matched_at_ms = excluded.matched_at_ms, "
    "distance = excluded.distance, "
    "similarity = excluded.similarity, "
    "threshold_value = excluded.threshold_value, "
    "recognizer_name = excluded.recognizer_name;";

constexpr const char* kInsertRecognitionEventSql =
    "INSERT INTO face_recognition_events ("
    "detection_record_id, session_id, source_id, frame_index, pts_ms, event_type, "
    "face_identity_id, face_code, face_name, distance, similarity, "
    "threshold_value, recognizer_name, created_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kRecentRecognitionEventForFaceSql =
    "SELECT 1 FROM face_recognition_events "
    "WHERE session_id = ? "
    "AND source_id = ? "
    "AND face_identity_id = ? "
    "AND event_type = 'face_recognized' "
    "AND created_at_ms >= ? "
    "ORDER BY created_at_ms DESC, id DESC LIMIT 1;";

constexpr const char* kDeleteFaceFeaturesSql =
    "DELETE FROM face_feature_templates WHERE face_identity_id = ?;";

constexpr const char* kInsertFaceFeatureSql =
    "INSERT INTO face_feature_templates ("
    "face_identity_id, model_name, feature_fingerprint, sample_image_path, "
    "feature_values, value_count, created_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kAllFaceFeaturesSql =
    "SELECT f.id, f.face_identity_id, i.face_code, i.display_name, "
    "f.sample_image_path, f.model_name, f.feature_fingerprint, "
    "f.feature_values, f.value_count, f.created_at_ms "
    "FROM face_feature_templates f "
    "INNER JOIN face_identities i ON i.id = f.face_identity_id "
    "ORDER BY f.face_identity_id ASC, f.id ASC;";

constexpr const char* kFaceIdentityByCodeSql =
    "SELECT id, face_code, display_name, reference_image_path, notes, "
    "created_at_ms, updated_at_ms "
    "FROM face_identities WHERE face_code = ? LIMIT 1;";

constexpr const char* kRecentRecognitionEventsSql =
    "SELECT id, detection_record_id, session_id, source_id, frame_index, pts_ms, "
    "event_type, face_identity_id, face_code, face_name, distance, similarity, "
    "threshold_value, recognizer_name, created_at_ms "
    "FROM face_recognition_events ORDER BY created_at_ms DESC, id DESC LIMIT ?;";

constexpr const char* kClearFaceIdentitySql =
    "DELETE FROM detection_face_links WHERE detection_record_id = ?;";

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

        std::int64_t recordId = 0;
        saved = insertDetectionLocked(
            sessionId,
            sourceId,
            frameIndex,
            ptsMs,
            result,
            recordedAtMs,
            &recordId);
        if (saved && result.face.matched && result.face.faceId.has_value())
        {
            saved = insertRecognizedFaceLinkLocked(recordId, result);
            bool shouldInsertEvent = false;
            if (saved)
            {
                const std::string eventSourceId = result.sourceId.empty()
                    ? sourceId
                    : result.sourceId;
                saved = shouldInsertRecognitionEventLocked(
                    sessionId,
                    eventSourceId,
                    *result.face.faceId,
                    recordedAtMs,
                    &shouldInsertEvent);
            }
            if (saved && shouldInsertEvent)
            {
                saved = insertRecognitionEventLocked(
                    recordId,
                    sessionId,
                    sourceId,
                    frameIndex,
                    ptsMs,
                    result,
                    recordedAtMs);
            }
        }
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
    r.box_height,
    l.face_identity_id,
    l.face_code,
    l.face_name,
    l.matched_at_ms,
    l.distance,
    l.similarity,
    l.threshold_value,
    l.recognizer_name
FROM detection_records r
INNER JOIN inspection_sessions s ON s.id = r.session_id
LEFT JOIN detection_face_links l ON l.detection_record_id = r.id
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
        row.faceId = optionalInt64Column(statement, 17);
        row.faceCode = textColumn(statement, 18);
        row.faceName = textColumn(statement, 19);
        row.faceMatchedAtMs = sqlite3_column_int64(statement, 20);
        row.faceDistance = static_cast<float>(sqlite3_column_double(statement, 21));
        row.faceSimilarity = static_cast<float>(sqlite3_column_double(statement, 22));
        row.faceThreshold = static_cast<float>(sqlite3_column_double(statement, 23));
        row.faceRecognizerName = textColumn(statement, 24);
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

bool SQLiteDetectionStorage::saveFaceIdentity(const FaceIdentityEntry& entry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return false;
    }

    if (entry.faceCode.empty() || entry.displayName.empty())
    {
        lastError_ = "Face code or display name is empty.";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kSaveFaceIdentitySql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face identity save");
        return false;
    }

    const std::int64_t timestamp = entry.updatedAtMs > 0 ? entry.updatedAtMs : currentTimeMs();
    const std::int64_t createdAt = entry.createdAtMs > 0 ? entry.createdAtMs : timestamp;
    const bool bound =
        sqlite3_bind_text(statement, 1, entry.faceCode.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_text(statement, 2, entry.displayName.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_text(statement, 3, entry.referenceImagePath.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_text(statement, 4, entry.notes.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_text(statement, 5, entry.faceCode.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_int64(statement, 6, createdAt) == SQLITE_OK
        && sqlite3_bind_int64(statement, 7, timestamp) == SQLITE_OK;
    if (!bound || sqlite3_step(statement) != SQLITE_DONE)
    {
        setLastErrorLocked("Could not save face identity");
        sqlite3_finalize(statement);
        return false;
    }

    sqlite3_finalize(statement);
    lastError_.clear();
    return true;
}

std::optional<FaceIdentityEntry> SQLiteDetectionStorage::faceIdentityByCode(
    const std::string& faceCode) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || faceCode.empty())
    {
        lastError_ = "SQLite database is not open or face code is empty.";
        return std::nullopt;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            kFaceIdentityByCodeSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face identity lookup");
        return std::nullopt;
    }

    const bool bound =
        sqlite3_bind_text(
            statement,
            1,
            faceCode.c_str(),
            -1,
            SQLITE_TRANSIENT)
        == SQLITE_OK;
    if (!bound || sqlite3_step(statement) != SQLITE_ROW)
    {
        if (bound)
        {
            lastError_.clear();
        }
        else
        {
            setLastErrorLocked("Could not bind face identity lookup");
        }
        sqlite3_finalize(statement);
        return std::nullopt;
    }

    FaceIdentityEntry entry;
    entry.faceId = sqlite3_column_int64(statement, 0);
    entry.faceCode = textColumn(statement, 1);
    entry.displayName = textColumn(statement, 2);
    entry.referenceImagePath = textColumn(statement, 3);
    entry.notes = textColumn(statement, 4);
    entry.createdAtMs = sqlite3_column_int64(statement, 5);
    entry.updatedAtMs = sqlite3_column_int64(statement, 6);
    sqlite3_finalize(statement);
    lastError_.clear();
    return entry;
}

bool SQLiteDetectionStorage::replaceFaceFeatures(
    std::int64_t faceId,
    const FaceFeatureTemplates& templates)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || faceId <= 0)
    {
        lastError_ = "SQLite database is not open or face id is invalid.";
        return false;
    }

    if (!executeLocked("BEGIN IMMEDIATE TRANSACTION;"))
    {
        return false;
    }

    sqlite3_stmt* deleteStatement = nullptr;
    bool completed =
        sqlite3_prepare_v2(
            database_,
            kDeleteFaceFeaturesSql,
            -1,
            &deleteStatement,
            nullptr)
        == SQLITE_OK;
    if (completed)
    {
        completed =
            sqlite3_bind_int64(deleteStatement, 1, faceId) == SQLITE_OK
            && sqlite3_step(deleteStatement) == SQLITE_DONE;
    }
    sqlite3_finalize(deleteStatement);

    sqlite3_stmt* insertStatement = nullptr;
    if (completed && !templates.empty())
    {
        completed =
            sqlite3_prepare_v2(
                database_,
                kInsertFaceFeatureSql,
                -1,
                &insertStatement,
                nullptr)
            == SQLITE_OK;
    }

    for (const FaceFeatureTemplate& item : templates)
    {
        if (!completed)
        {
            break;
        }
        if (item.feature.modelName.empty() || item.feature.values.empty())
        {
            completed = false;
            lastError_ = "Face feature template is empty.";
            break;
        }

        sqlite3_reset(insertStatement);
        sqlite3_clear_bindings(insertStatement);
        completed =
            sqlite3_bind_int64(insertStatement, 1, faceId) == SQLITE_OK
            && sqlite3_bind_text(
                   insertStatement,
                   2,
                   item.feature.modelName.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK
            && sqlite3_bind_text(
                   insertStatement,
                   3,
                   item.feature.featureFingerprint.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK
            && sqlite3_bind_text(
                   insertStatement,
                   4,
                   item.sampleImagePath.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK
            && sqlite3_bind_blob(
                   insertStatement,
                   5,
                   item.feature.values.data(),
                   static_cast<int>(
                       item.feature.values.size() * sizeof(float)),
                   SQLITE_TRANSIENT)
                == SQLITE_OK
            && sqlite3_bind_int(
                   insertStatement,
                   6,
                   static_cast<int>(item.feature.values.size()))
                == SQLITE_OK
            && sqlite3_bind_int64(
                   insertStatement,
                   7,
                   item.createdAtMs > 0 ? item.createdAtMs : currentTimeMs())
                == SQLITE_OK
            && sqlite3_step(insertStatement) == SQLITE_DONE;
    }

    sqlite3_finalize(insertStatement);
    if (!completed)
    {
        executeLocked("ROLLBACK;");
        if (lastError_.empty())
        {
            setLastErrorLocked("Could not replace face feature templates");
        }
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

FaceFeatureTemplates SQLiteDetectionStorage::allFaceFeatures() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return {};
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            kAllFaceFeaturesSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face feature query");
        return {};
    }

    FaceFeatureTemplates templates;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        FaceFeatureTemplate item;
        item.featureId = sqlite3_column_int64(statement, 0);
        item.faceId = sqlite3_column_int64(statement, 1);
        item.faceCode = textColumn(statement, 2);
        item.faceName = textColumn(statement, 3);
        item.sampleImagePath = textColumn(statement, 4);
        item.feature.modelName = textColumn(statement, 5);
        item.feature.featureFingerprint = textColumn(statement, 6);
        const void* blob = sqlite3_column_blob(statement, 7);
        const int blobBytes = sqlite3_column_bytes(statement, 7);
        const int valueCount = sqlite3_column_int(statement, 8);
        if (blob == nullptr
            || valueCount <= 0
            || blobBytes != valueCount * static_cast<int>(sizeof(float)))
        {
            continue;
        }

        item.feature.values.resize(static_cast<std::size_t>(valueCount));
        std::memcpy(
            item.feature.values.data(),
            blob,
            static_cast<std::size_t>(blobBytes));
        item.createdAtMs = sqlite3_column_int64(statement, 9);
        templates.push_back(std::move(item));
    }

    if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not read face feature templates");
        templates.clear();
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return templates;
}

FaceRecognitionEvents SQLiteDetectionStorage::recentFaceRecognitionEvents(
    std::size_t maxCount) const
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
    if (sqlite3_prepare_v2(
            database_,
            kRecentRecognitionEventsSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognition event query");
        return {};
    }

    if (sqlite3_bind_int64(statement, 1, boundedLimit(maxCount)) != SQLITE_OK)
    {
        setLastErrorLocked("Could not bind recognition event query");
        sqlite3_finalize(statement);
        return {};
    }

    FaceRecognitionEvents events;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        FaceRecognitionEvent event;
        event.eventId = sqlite3_column_int64(statement, 0);
        event.detectionRecordId = sqlite3_column_int64(statement, 1);
        event.sessionId = sqlite3_column_int64(statement, 2);
        event.sourceId = textColumn(statement, 3);
        event.frameIndex = sqlite3_column_int64(statement, 4);
        event.ptsMs = sqlite3_column_int64(statement, 5);
        event.eventType = textColumn(statement, 6);
        event.faceId = optionalInt64Column(statement, 7);
        event.faceCode = textColumn(statement, 8);
        event.faceName = textColumn(statement, 9);
        event.distance = static_cast<float>(sqlite3_column_double(statement, 10));
        event.similarity = static_cast<float>(sqlite3_column_double(statement, 11));
        event.threshold = static_cast<float>(sqlite3_column_double(statement, 12));
        event.recognizerName = textColumn(statement, 13);
        event.createdAtMs = sqlite3_column_int64(statement, 14);
        events.push_back(std::move(event));
    }

    if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not read recognition events");
        events.clear();
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return events;
}

bool SQLiteDetectionStorage::removeFaceIdentity(std::int64_t faceId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || faceId <= 0)
    {
        lastError_ = "SQLite database is not open or face id is invalid.";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kDeleteFaceIdentitySql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face identity delete");
        return false;
    }

    const bool bound = sqlite3_bind_int64(statement, 1, faceId) == SQLITE_OK;
    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    const bool changed = completed && sqlite3_changes(database_) > 0;
    if (!changed)
    {
        setLastErrorLocked("Could not delete face identity");
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return changed;
}

FaceIdentityEntries SQLiteDetectionStorage::recentFaceIdentities(std::size_t maxCount) const
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
    if (sqlite3_prepare_v2(database_, kFaceIdentitiesSql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face identity query");
        return {};
    }

    if (sqlite3_bind_int64(statement, 1, boundedLimit(maxCount)) != SQLITE_OK)
    {
        setLastErrorLocked("Could not bind face identity query");
        sqlite3_finalize(statement);
        return {};
    }

    FaceIdentityEntries entries;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        FaceIdentityEntry entry;
        entry.faceId = sqlite3_column_int64(statement, 0);
        entry.faceCode = textColumn(statement, 1);
        entry.displayName = textColumn(statement, 2);
        entry.referenceImagePath = textColumn(statement, 3);
        entry.notes = textColumn(statement, 4);
        entry.createdAtMs = sqlite3_column_int64(statement, 5);
        entry.updatedAtMs = sqlite3_column_int64(statement, 6);
        entries.push_back(std::move(entry));
    }

    if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not read face identities");
        entries.clear();
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return entries;
}

bool SQLiteDetectionStorage::bindFaceIdentity(std::int64_t recordId, std::int64_t faceId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || recordId <= 0 || faceId <= 0)
    {
        lastError_ = "SQLite database is not open or binding arguments are invalid.";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kBindFaceIdentitySql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face identity bind");
        return false;
    }

    const std::int64_t matchedAtMs = currentTimeMs();
    const bool bound =
        sqlite3_bind_int64(statement, 1, recordId) == SQLITE_OK
        && sqlite3_bind_int64(statement, 2, matchedAtMs) == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, faceId) == SQLITE_OK;
    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    const bool changed = completed && sqlite3_changes(database_) > 0;
    if (!changed)
    {
        setLastErrorLocked("Could not bind face identity");
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return changed;
}

bool SQLiteDetectionStorage::clearFaceIdentity(std::int64_t recordId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || recordId <= 0)
    {
        lastError_ = "SQLite database is not open or record id is invalid.";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kClearFaceIdentitySql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face identity clear");
        return false;
    }

    const bool bound = sqlite3_bind_int64(statement, 1, recordId) == SQLITE_OK;
    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not clear face identity");
    }
    else
    {
        lastError_.clear();
    }

    sqlite3_finalize(statement);
    return completed;
}

bool SQLiteDetectionStorage::createSchemaLocked()
{
    if (!executeLocked(kCreateSchemaSql))
    {
        return false;
    }

    // Existing databases created before recognition support need the new link
    // metadata columns without losing their manual associations.
    if (!ensureColumnLocked(
            "face_feature_templates",
            "feature_fingerprint",
            "ALTER TABLE face_feature_templates "
            "ADD COLUMN feature_fingerprint TEXT NOT NULL DEFAULT '';"))
    {
        return false;
    }

    return ensureColumnLocked(
               "detection_face_links",
               "distance",
               "ALTER TABLE detection_face_links "
               "ADD COLUMN distance REAL NOT NULL DEFAULT 0;")
        && ensureColumnLocked(
               "detection_face_links",
               "similarity",
               "ALTER TABLE detection_face_links "
               "ADD COLUMN similarity REAL NOT NULL DEFAULT 0;")
        && ensureColumnLocked(
               "detection_face_links",
               "threshold_value",
               "ALTER TABLE detection_face_links "
               "ADD COLUMN threshold_value REAL NOT NULL DEFAULT 0;")
        && ensureColumnLocked(
               "detection_face_links",
               "recognizer_name",
               "ALTER TABLE detection_face_links "
               "ADD COLUMN recognizer_name TEXT NOT NULL DEFAULT '';");
}

bool SQLiteDetectionStorage::ensureColumnLocked(
    const char* tableName,
    const char* columnName,
    const char* alterSql)
{
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return false;
    }

    const std::string pragma =
        "PRAGMA table_info(" + std::string(tableName) + ");";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            pragma.c_str(),
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not inspect SQLite table schema");
        return false;
    }

    bool found = false;
    int stepResult = SQLITE_ROW;
    while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
    {
        if (textColumn(statement, 1) == columnName)
        {
            found = true;
            break;
        }
    }
    sqlite3_finalize(statement);

    if (stepResult != SQLITE_DONE && stepResult != SQLITE_ROW)
    {
        setLastErrorLocked("Could not read SQLite table schema");
        return false;
    }
    if (found)
    {
        return true;
    }

    return executeLocked(alterSql);
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
    std::int64_t recordedAtMs,
    std::int64_t* recordId)
{
    if (recordId == nullptr)
    {
        lastError_ = "Detection record id output is null.";
        return false;
    }

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
    else
    {
        *recordId = sqlite3_last_insert_rowid(database_);
    }

    sqlite3_finalize(statement);
    return completed;
}

bool SQLiteDetectionStorage::insertRecognizedFaceLinkLocked(
    std::int64_t recordId,
    const DetectionResult& result)
{
    if (recordId <= 0
        || !result.face.matched
        || !result.face.faceId.has_value()
        || *result.face.faceId <= 0)
    {
        lastError_ = "Recognized face link arguments are invalid.";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            kInsertRecognizedFaceLinkSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognized face link insert");
        return false;
    }

    const bool bound =
        sqlite3_bind_int64(statement, 1, recordId) == SQLITE_OK
        && sqlite3_bind_int64(
               statement,
               2,
               result.face.matchedAtMs > 0
                   ? result.face.matchedAtMs
                   : currentTimeMs())
            == SQLITE_OK
        && sqlite3_bind_double(statement, 3, result.face.distance) == SQLITE_OK
        && sqlite3_bind_double(statement, 4, result.face.similarity) == SQLITE_OK
        && sqlite3_bind_double(statement, 5, result.face.threshold) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               6,
               result.face.recognizerName.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_int64(statement, 7, *result.face.faceId) == SQLITE_OK;
    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not insert recognized face link");
    }

    sqlite3_finalize(statement);
    return completed;
}

bool SQLiteDetectionStorage::insertRecognitionEventLocked(
    std::int64_t recordId,
    std::int64_t sessionId,
    const std::string& fallbackSourceId,
    std::int64_t fallbackFrameIndex,
    std::int64_t fallbackPtsMs,
    const DetectionResult& result,
    std::int64_t createdAtMs)
{
    if (recordId <= 0
        || sessionId <= 0
        || !result.face.matched
        || !result.face.faceId.has_value())
    {
        lastError_ = "Recognition event arguments are invalid.";
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

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            kInsertRecognitionEventSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognition event insert");
        return false;
    }

    const bool bound =
        sqlite3_bind_int64(statement, 1, recordId) == SQLITE_OK
        && sqlite3_bind_int64(statement, 2, sessionId) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               3,
               sourceId.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_int64(statement, 4, frameIndex) == SQLITE_OK
        && sqlite3_bind_int64(statement, 5, ptsMs) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               6,
               "face_recognized",
               -1,
               SQLITE_STATIC)
            == SQLITE_OK
        && sqlite3_bind_int64(statement, 7, *result.face.faceId) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               8,
               result.face.faceCode.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               9,
               result.face.faceName.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_double(statement, 10, result.face.distance) == SQLITE_OK
        && sqlite3_bind_double(statement, 11, result.face.similarity) == SQLITE_OK
        && sqlite3_bind_double(statement, 12, result.face.threshold) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               13,
               result.face.recognizerName.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_int64(statement, 14, createdAtMs) == SQLITE_OK;
    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not insert recognition event");
    }

    sqlite3_finalize(statement);
    return completed;
}

bool SQLiteDetectionStorage::shouldInsertRecognitionEventLocked(
    std::int64_t sessionId,
    const std::string& sourceId,
    std::int64_t faceId,
    std::int64_t createdAtMs,
    bool* shouldInsert)
{
    if (shouldInsert == nullptr
        || sessionId <= 0
        || faceId <= 0
        || createdAtMs <= 0)
    {
        lastError_ = "Recognition event deduplication arguments are invalid.";
        return false;
    }

    *shouldInsert = true;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            kRecentRecognitionEventForFaceSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognition event deduplication query");
        return false;
    }

    const std::int64_t cutoffMs =
        std::max<std::int64_t>(0, createdAtMs - kRecognitionEventCooldownMs);
    const bool bound =
        sqlite3_bind_int64(statement, 1, sessionId) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               2,
               sourceId.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, faceId) == SQLITE_OK
        && sqlite3_bind_int64(statement, 4, cutoffMs) == SQLITE_OK;
    if (!bound)
    {
        setLastErrorLocked("Could not bind recognition event deduplication query");
        sqlite3_finalize(statement);
        return false;
    }

    const int stepResult = sqlite3_step(statement);
    if (stepResult == SQLITE_ROW)
    {
        *shouldInsert = false;
    }
    else if (stepResult != SQLITE_DONE)
    {
        setLastErrorLocked("Could not execute recognition event deduplication query");
        sqlite3_finalize(statement);
        return false;
    }

    sqlite3_finalize(statement);
    return true;
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
        result.face.faceId = optionalInt64Column(statement, 10);
        result.face.matched = result.face.faceId.has_value();
        result.face.faceCode = textColumn(statement, 11);
        result.face.faceName = textColumn(statement, 12);
        result.face.matchedAtMs = sqlite3_column_int64(statement, 13);
        result.face.distance = static_cast<float>(sqlite3_column_double(statement, 14));
        result.face.similarity = static_cast<float>(sqlite3_column_double(statement, 15));
        result.face.threshold = static_cast<float>(sqlite3_column_double(statement, 16));
        result.face.recognizerName = textColumn(statement, 17);
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
