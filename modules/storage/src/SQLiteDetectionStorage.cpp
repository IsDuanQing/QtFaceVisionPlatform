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
    track_id INTEGER NOT NULL DEFAULT 0,
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
    track_id INTEGER NOT NULL DEFAULT 0,
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

CREATE TABLE IF NOT EXISTS face_tracks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL,
    source_id TEXT NOT NULL,
    track_id INTEGER NOT NULL,
    class_id INTEGER NOT NULL,
    class_name TEXT NOT NULL,
    first_frame_index INTEGER NOT NULL,
    first_pts_ms INTEGER NOT NULL,
    last_frame_index INTEGER NOT NULL,
    last_pts_ms INTEGER NOT NULL,
    duration_ms INTEGER NOT NULL,
    detection_count INTEGER NOT NULL,
    missed_updates INTEGER NOT NULL,
    active INTEGER NOT NULL DEFAULT 1,
    first_recognition_decision TEXT NOT NULL DEFAULT '',
    first_face_identity_id INTEGER,
    first_face_code TEXT NOT NULL DEFAULT '',
    first_face_name TEXT NOT NULL DEFAULT '',
    first_similarity REAL NOT NULL DEFAULT 0,
    first_threshold REAL NOT NULL DEFAULT 0,
    first_observed_pts_ms INTEGER NOT NULL DEFAULT 0,
    last_recognition_decision TEXT NOT NULL DEFAULT '',
    last_face_identity_id INTEGER,
    last_face_code TEXT NOT NULL DEFAULT '',
    last_face_name TEXT NOT NULL DEFAULT '',
    last_similarity REAL NOT NULL DEFAULT 0,
    last_threshold REAL NOT NULL DEFAULT 0,
    last_observed_pts_ms INTEGER NOT NULL DEFAULT 0,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    UNIQUE(session_id, source_id, track_id),
    FOREIGN KEY(session_id) REFERENCES inspection_sessions(id)
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

constexpr const char* kCreateTrackIndexesSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_detection_records_track
ON detection_records(session_id, source_id, track_id, frame_index);

CREATE INDEX IF NOT EXISTS idx_face_recognition_events_track
ON face_recognition_events(
    session_id,
    source_id,
    track_id,
    event_type,
    face_identity_id
);

CREATE INDEX IF NOT EXISTS idx_face_tracks_session_source
ON face_tracks(session_id, source_id, track_id);
)SQL";

constexpr const char* kInsertFrameSql =
    "INSERT INTO detection_frames ("
    "session_id, source_id, frame_index, pts_ms, object_count, recorded_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?);";

constexpr const char* kInsertDetectionSql =
    "INSERT INTO detection_records ("
    "session_id, source_id, frame_index, pts_ms, track_id, class_id, class_name, confidence, "
    "box_x, box_y, box_width, box_height, recorded_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kUpsertFaceTrackSql = R"SQL(
INSERT INTO face_tracks (
    session_id, source_id, track_id, class_id, class_name,
    first_frame_index, first_pts_ms, last_frame_index, last_pts_ms,
    duration_ms, detection_count, missed_updates, active,
    first_recognition_decision, first_face_identity_id, first_face_code,
    first_face_name, first_similarity, first_threshold, first_observed_pts_ms,
    last_recognition_decision, last_face_identity_id, last_face_code,
    last_face_name, last_similarity, last_threshold, last_observed_pts_ms,
    created_at_ms, updated_at_ms
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(session_id, source_id, track_id) DO UPDATE SET
    class_id = excluded.class_id,
    class_name = excluded.class_name,
    first_frame_index = excluded.first_frame_index,
    first_pts_ms = excluded.first_pts_ms,
    last_frame_index = excluded.last_frame_index,
    last_pts_ms = excluded.last_pts_ms,
    duration_ms = excluded.duration_ms,
    detection_count = excluded.detection_count,
    missed_updates = excluded.missed_updates,
    active = excluded.active,
    first_recognition_decision = excluded.first_recognition_decision,
    first_face_identity_id = excluded.first_face_identity_id,
    first_face_code = excluded.first_face_code,
    first_face_name = excluded.first_face_name,
    first_similarity = excluded.first_similarity,
    first_threshold = excluded.first_threshold,
    first_observed_pts_ms = excluded.first_observed_pts_ms,
    last_recognition_decision = excluded.last_recognition_decision,
    last_face_identity_id = excluded.last_face_identity_id,
    last_face_code = excluded.last_face_code,
    last_face_name = excluded.last_face_name,
    last_similarity = excluded.last_similarity,
    last_threshold = excluded.last_threshold,
    last_observed_pts_ms = excluded.last_observed_pts_ms,
    updated_at_ms = excluded.updated_at_ms;
)SQL";

constexpr const char* kRecentResultsSql =
    "SELECT r.source_id, r.frame_index, r.pts_ms, r.track_id, r.class_id, "
    "r.class_name, r.confidence, r.box_x, r.box_y, r.box_width, r.box_height, "
    "l.face_identity_id, l.face_code, l.face_name, l.matched_at_ms, "
    "l.distance, l.similarity, l.threshold_value, l.recognizer_name, "
    "t.track_id, t.first_frame_index, t.first_pts_ms, t.last_frame_index, "
    "t.last_pts_ms, t.duration_ms, t.detection_count, t.missed_updates, t.active, "
    "t.first_recognition_decision, t.first_face_identity_id, t.first_face_code, "
    "t.first_face_name, t.first_similarity, t.first_threshold, t.first_observed_pts_ms, "
    "t.last_recognition_decision, t.last_face_identity_id, t.last_face_code, "
    "t.last_face_name, t.last_similarity, t.last_threshold, t.last_observed_pts_ms "
    "FROM detection_records r "
    "LEFT JOIN detection_face_links l ON l.detection_record_id = r.id "
    "LEFT JOIN face_tracks t ON t.session_id = r.session_id "
    "AND t.source_id = r.source_id AND t.track_id = r.track_id "
    "ORDER BY r.id DESC LIMIT ?;";

constexpr const char* kFrameResultsSql =
    "SELECT r.source_id, r.frame_index, r.pts_ms, r.track_id, r.class_id, "
    "r.class_name, r.confidence, r.box_x, r.box_y, r.box_width, r.box_height, "
    "l.face_identity_id, l.face_code, l.face_name, l.matched_at_ms, "
    "l.distance, l.similarity, l.threshold_value, l.recognizer_name, "
    "t.track_id, t.first_frame_index, t.first_pts_ms, t.last_frame_index, "
    "t.last_pts_ms, t.duration_ms, t.detection_count, t.missed_updates, t.active, "
    "t.first_recognition_decision, t.first_face_identity_id, t.first_face_code, "
    "t.first_face_name, t.first_similarity, t.first_threshold, t.first_observed_pts_ms, "
    "t.last_recognition_decision, t.last_face_identity_id, t.last_face_code, "
    "t.last_face_name, t.last_similarity, t.last_threshold, t.last_observed_pts_ms "
    "FROM detection_records r "
    "LEFT JOIN detection_face_links l ON l.detection_record_id = r.id "
    "LEFT JOIN face_tracks t ON t.session_id = r.session_id "
    "AND t.source_id = r.source_id AND t.track_id = r.track_id "
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
    "detection_record_id, session_id, source_id, frame_index, pts_ms, track_id, event_type, "
    "face_identity_id, face_code, face_name, distance, similarity, "
    "threshold_value, recognizer_name, created_at_ms"
    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kRecentRecognitionEventBaseSql =
    "SELECT 1 FROM face_recognition_events "
    "WHERE session_id = ? "
    "AND source_id = ? "
    "AND event_type = ? "
    "AND created_at_ms >= ? ";

constexpr const char* kTrackRecognitionEventBaseSql =
    "SELECT 1 FROM face_recognition_events "
    "WHERE session_id = ? "
    "AND source_id = ? "
    "AND track_id = ? "
    "AND event_type = ? ";

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

constexpr const char* kRecognitionEventsSelectSql =
    "SELECT e.id, e.detection_record_id, e.session_id, e.source_id, e.frame_index, e.pts_ms, "
    "e.track_id, e.event_type, e.face_identity_id, e.face_code, e.face_name, e.distance, e.similarity, "
    "e.threshold_value, e.recognizer_name, e.created_at_ms, "
    "COALESCE(t.duration_ms, 0), COALESCE(t.active, 0), "
    "COALESCE(t.first_recognition_decision, ''), COALESCE(t.first_face_code, ''), "
    "COALESCE(t.first_face_name, ''), COALESCE(t.last_recognition_decision, ''), "
    "COALESCE(t.last_face_code, ''), COALESCE(t.last_face_name, '') "
    "FROM face_recognition_events e "
    "LEFT JOIN face_tracks t ON t.session_id = e.session_id "
    "AND t.source_id = e.source_id AND t.track_id = e.track_id "
    "WHERE 1 = 1 ";

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

void appendHistoryFilterConditions(
    std::string& sql,
    const ivp::DetectionHistoryQuery& query)
{
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
}

bool bindHistoryFilterConditions(
    sqlite3_stmt* statement,
    const ivp::DetectionHistoryQuery& query,
    int* parameterIndex)
{
    if (statement == nullptr || parameterIndex == nullptr)
    {
        return false;
    }

    int& index = *parameterIndex;
    bool bound = true;
    auto bindInt64 = [&](std::int64_t value) {
        bound = bound
            && sqlite3_bind_int64(statement, index++, value) == SQLITE_OK;
    };
    auto bindText = [&](const std::string& value) {
        bound = bound
            && sqlite3_bind_text(
                   statement,
                   index++,
                   value.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK;
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

    return bound;
}

void appendRecognitionEventFilterConditions(
    std::string& sql,
    const ivp::FaceRecognitionEventQuery& query)
{
    if (query.sessionId.has_value())
    {
        sql += " AND e.session_id = ?";
    }
    if (query.sourceLike.has_value() && !query.sourceLike->empty())
    {
        sql += " AND e.source_id LIKE ? COLLATE NOCASE";
    }
    if (query.eventType.has_value() && !query.eventType->empty())
    {
        sql += " AND e.event_type = ?";
    }
    if (query.faceLike.has_value() && !query.faceLike->empty())
    {
        sql += " AND (e.face_code LIKE ? COLLATE NOCASE"
               " OR e.face_name LIKE ? COLLATE NOCASE)";
    }
}

bool bindRecognitionEventFilterConditions(
    sqlite3_stmt* statement,
    const ivp::FaceRecognitionEventQuery& query,
    int* parameterIndex)
{
    if (statement == nullptr || parameterIndex == nullptr)
    {
        return false;
    }

    int& index = *parameterIndex;
    bool bound = true;
    auto bindInt64 = [&](std::int64_t value) {
        bound = bound
            && sqlite3_bind_int64(statement, index++, value) == SQLITE_OK;
    };
    auto bindText = [&](const std::string& value) {
        bound = bound
            && sqlite3_bind_text(
                   statement,
                   index++,
                   value.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK;
    };

    if (query.sessionId.has_value())
    {
        bindInt64(*query.sessionId);
    }
    if (query.sourceLike.has_value() && !query.sourceLike->empty())
    {
        bindText(likePattern(*query.sourceLike));
    }
    if (query.eventType.has_value() && !query.eventType->empty())
    {
        bindText(*query.eventType);
    }
    if (query.faceLike.has_value() && !query.faceLike->empty())
    {
        const std::string pattern = likePattern(*query.faceLike);
        bindText(pattern);
        bindText(pattern);
    }

    return bound;
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
        if (saved)
        {
            std::string eventType;
            std::optional<std::int64_t> eventFaceId;
            if (result.face.matched && result.face.faceId.has_value()
                && *result.face.faceId > 0)
            {
                eventType = "face_recognized";
                eventFaceId = result.face.faceId;
                saved = insertRecognizedFaceLinkLocked(recordId, result);
            }
            else if (result.face.decision == "low_similarity")
            {
                eventType = "face_low_similarity";
            }
            else if (result.face.decision == "ambiguous")
            {
                eventType = "face_ambiguous";
            }
            else if (result.face.decision == "no_candidates")
            {
                eventType = "face_unknown";
            }

            if (saved && !eventType.empty())
            {
                bool shouldInsertEvent = false;
                const std::string eventSourceId = result.sourceId.empty()
                    ? sourceId
                    : result.sourceId;
                saved = shouldInsertRecognitionEventLocked(
                    sessionId,
                    eventSourceId,
                    eventType,
                    eventFaceId,
                    result.trackId,
                    recordedAtMs,
                    &shouldInsertEvent);
                if (saved && shouldInsertEvent)
                {
                    saved = insertRecognitionEventLocked(
                        recordId,
                        sessionId,
                        sourceId,
                        frameIndex,
                        ptsMs,
                        result,
                        eventType,
                        eventFaceId,
                        recordedAtMs);
                }
            }

            if (saved && result.trackState.trackId > 0)
            {
                FaceTrackSnapshot snapshot = result.trackState;
                if (snapshot.sourceId.empty())
                {
                    snapshot.sourceId = result.sourceId.empty()
                        ? sourceId
                        : result.sourceId;
                }
                saved = upsertFaceTrackLocked(
                    sessionId,
                    snapshot,
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

bool SQLiteDetectionStorage::saveFaceTrackSnapshots(
    std::int64_t sessionId,
    const FaceTrackSnapshots& snapshots)
{
    if (snapshots.empty())
    {
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr || sessionId <= 0)
    {
        lastError_ = "SQLite database is not open or session id is invalid.";
        return false;
    }

    if (!executeLocked("BEGIN IMMEDIATE TRANSACTION;"))
    {
        return false;
    }

    const std::int64_t updatedAtMs = currentTimeMs();
    bool saved = true;
    for (const FaceTrackSnapshot& snapshot : snapshots)
    {
        if (!saved)
        {
            break;
        }
        saved = upsertFaceTrackLocked(sessionId, snapshot, updatedAtMs);
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
    r.track_id,
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
    l.recognizer_name,
    COALESCE(t.duration_ms, 0),
    COALESCE(t.active, 0),
    COALESCE(t.first_recognition_decision, ''),
    COALESCE(t.first_face_code, ''),
    COALESCE(t.first_face_name, ''),
    COALESCE(t.last_recognition_decision, ''),
    COALESCE(t.last_face_code, ''),
    COALESCE(t.last_face_name, '')
FROM detection_records r
INNER JOIN inspection_sessions s ON s.id = r.session_id
LEFT JOIN detection_face_links l ON l.detection_record_id = r.id
LEFT JOIN face_tracks t
    ON t.session_id = r.session_id
    AND t.source_id = r.source_id
    AND t.track_id = r.track_id
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
        row.trackId = sqlite3_column_int64(statement, 8);
        row.recordedAtMs = sqlite3_column_int64(statement, 9);
        row.frameObjectCount = sqlite3_column_int64(statement, 10);
        row.classId = sqlite3_column_int(statement, 11);
        row.className = textColumn(statement, 12);
        row.confidence = static_cast<float>(sqlite3_column_double(statement, 13));
        row.box.x = static_cast<float>(sqlite3_column_double(statement, 14));
        row.box.y = static_cast<float>(sqlite3_column_double(statement, 15));
        row.box.width = static_cast<float>(sqlite3_column_double(statement, 16));
        row.box.height = static_cast<float>(sqlite3_column_double(statement, 17));
        row.faceId = optionalInt64Column(statement, 18);
        row.faceCode = textColumn(statement, 19);
        row.faceName = textColumn(statement, 20);
        row.faceMatchedAtMs = sqlite3_column_int64(statement, 21);
        row.faceDistance = static_cast<float>(sqlite3_column_double(statement, 22));
        row.faceSimilarity = static_cast<float>(sqlite3_column_double(statement, 23));
        row.faceThreshold = static_cast<float>(sqlite3_column_double(statement, 24));
        row.faceRecognizerName = textColumn(statement, 25);
        row.trackDurationMs = sqlite3_column_int64(statement, 26);
        row.trackActive = sqlite3_column_int(statement, 27) != 0;
        row.trackFirstDecision = textColumn(statement, 28);
        row.trackFirstFaceCode = textColumn(statement, 29);
        row.trackFirstFaceName = textColumn(statement, 30);
        row.trackLastDecision = textColumn(statement, 31);
        row.trackLastFaceCode = textColumn(statement, 32);
        row.trackLastFaceName = textColumn(statement, 33);
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

bool SQLiteDetectionStorage::deleteHistoryRecords(
    const DetectionHistoryQuery& query,
    std::size_t* deletedCount)
{
    if (deletedCount != nullptr)
    {
        *deletedCount = 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return false;
    }

    const std::string selectedRecordIdsSql = R"SQL(
SELECT r.id
FROM detection_records r
INNER JOIN inspection_sessions s ON s.id = r.session_id
WHERE 1 = 1
)SQL";
    std::string selectionSql = selectedRecordIdsSql;
    appendHistoryFilterConditions(selectionSql, query);

    if (!executeLocked("BEGIN IMMEDIATE TRANSACTION;"))
    {
        return false;
    }

    auto executeDelete = [&](const std::string& sql, const char* errorMessage) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(
                database_,
                sql.c_str(),
                -1,
                &statement,
                nullptr)
            != SQLITE_OK)
        {
            setLastErrorLocked(errorMessage);
            return false;
        }

        int parameterIndex = 1;
        const bool bound =
            bindHistoryFilterConditions(
                statement,
                query,
                &parameterIndex);
        const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
        if (!completed)
        {
            setLastErrorLocked(errorMessage);
        }
        sqlite3_finalize(statement);
        return completed;
    };

    const std::string deleteEventsSql =
        "DELETE FROM face_recognition_events "
        "WHERE detection_record_id IN ("
        + selectionSql
        + ");";
    const std::string deleteLinksSql =
        "DELETE FROM detection_face_links "
        "WHERE detection_record_id IN ("
        + selectionSql
        + ");";
    const std::string deleteRecordsSql =
        "DELETE FROM detection_records "
        "WHERE id IN ("
        + selectionSql
        + ");";

    bool completed = executeDelete(
        deleteEventsSql,
        "Could not delete recognition events linked to history");
    completed = completed
        && executeDelete(
               deleteLinksSql,
               "Could not delete face associations linked to history");

    std::size_t removedRecords = 0;
    if (completed)
    {
        completed = executeDelete(
            deleteRecordsSql,
            "Could not delete detection history records");
        if (completed)
        {
            removedRecords = static_cast<std::size_t>(
                std::max(0, sqlite3_changes(database_)));
        }
    }

    if (completed
        && !executeLocked(
            "DELETE FROM face_tracks "
            "WHERE NOT EXISTS ("
            "SELECT 1 FROM detection_records r "
            "WHERE r.session_id = face_tracks.session_id "
            "AND r.source_id = face_tracks.source_id "
            "AND r.track_id = face_tracks.track_id"
            ");"))
    {
        completed = false;
    }

    if (!completed)
    {
        executeLocked("ROLLBACK;");
        return false;
    }

    if (!executeLocked("COMMIT;"))
    {
        executeLocked("ROLLBACK;");
        return false;
    }

    if (deletedCount != nullptr)
    {
        *deletedCount = removedRecords;
    }
    lastError_.clear();
    return true;
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
    FaceRecognitionEventQuery query;
    query.limit = maxCount;
    return queryFaceRecognitionEvents(query);
}

FaceRecognitionEvents SQLiteDetectionStorage::queryFaceRecognitionEvents(
    const FaceRecognitionEventQuery& query) const
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

    std::string sql = kRecognitionEventsSelectSql;
    if (query.sessionId.has_value())
    {
        sql += "AND e.session_id = ? ";
    }
    if (query.sourceLike.has_value() && !query.sourceLike->empty())
    {
        sql += "AND e.source_id LIKE ? COLLATE NOCASE ";
    }
    if (query.eventType.has_value() && !query.eventType->empty())
    {
        sql += "AND e.event_type = ? ";
    }
    if (query.faceLike.has_value() && !query.faceLike->empty())
    {
        sql += "AND (e.face_code LIKE ? COLLATE NOCASE "
               "OR e.face_name LIKE ? COLLATE NOCASE) ";
    }
    sql += "ORDER BY e.created_at_ms DESC, e.id DESC LIMIT ?;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            sql.c_str(),
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognition event query");
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
            && sqlite3_bind_text(
                   statement,
                   parameterIndex++,
                   value.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK;
    };

    if (query.sessionId.has_value())
    {
        bindInt64(*query.sessionId);
    }
    if (query.sourceLike.has_value() && !query.sourceLike->empty())
    {
        bindText(likePattern(*query.sourceLike));
    }
    if (query.eventType.has_value() && !query.eventType->empty())
    {
        bindText(*query.eventType);
    }
    if (query.faceLike.has_value() && !query.faceLike->empty())
    {
        const std::string pattern = likePattern(*query.faceLike);
        bindText(pattern);
        bindText(pattern);
    }
    bindInt64(boundedLimit(query.limit));

    if (!bound)
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
        event.trackId = sqlite3_column_int64(statement, 6);
        event.eventType = textColumn(statement, 7);
        event.faceId = optionalInt64Column(statement, 8);
        event.faceCode = textColumn(statement, 9);
        event.faceName = textColumn(statement, 10);
        event.distance = static_cast<float>(sqlite3_column_double(statement, 11));
        event.similarity = static_cast<float>(sqlite3_column_double(statement, 12));
        event.threshold = static_cast<float>(sqlite3_column_double(statement, 13));
        event.recognizerName = textColumn(statement, 14);
        event.createdAtMs = sqlite3_column_int64(statement, 15);
        event.trackDurationMs = sqlite3_column_int64(statement, 16);
        event.trackActive = sqlite3_column_int(statement, 17) != 0;
        event.trackFirstDecision = textColumn(statement, 18);
        event.trackFirstFaceCode = textColumn(statement, 19);
        event.trackFirstFaceName = textColumn(statement, 20);
        event.trackLastDecision = textColumn(statement, 21);
        event.trackLastFaceCode = textColumn(statement, 22);
        event.trackLastFaceName = textColumn(statement, 23);
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

bool SQLiteDetectionStorage::deleteRecognitionEvents(
    const FaceRecognitionEventQuery& query,
    std::size_t* deletedCount)
{
    if (deletedCount != nullptr)
    {
        *deletedCount = 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr)
    {
        lastError_ = "SQLite database is not open.";
        return false;
    }

    std::string selectionSql =
        "SELECT e.id FROM face_recognition_events e WHERE 1 = 1";
    appendRecognitionEventFilterConditions(selectionSql, query);
    const std::string sql =
        "DELETE FROM face_recognition_events WHERE id IN ("
        + selectionSql
        + ");";

    if (!executeLocked("BEGIN IMMEDIATE TRANSACTION;"))
    {
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            sql.c_str(),
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognition event delete");
        executeLocked("ROLLBACK;");
        return false;
    }

    int parameterIndex = 1;
    const bool bound =
        bindRecognitionEventFilterConditions(
            statement,
            query,
            &parameterIndex);
    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    const std::size_t removedEvents = completed
        ? static_cast<std::size_t>(std::max(0, sqlite3_changes(database_)))
        : 0;
    if (!completed)
    {
        setLastErrorLocked("Could not delete recognition events");
    }
    sqlite3_finalize(statement);

    if (!completed)
    {
        executeLocked("ROLLBACK;");
        return false;
    }

    if (!executeLocked("COMMIT;"))
    {
        executeLocked("ROLLBACK;");
        return false;
    }

    if (deletedCount != nullptr)
    {
        *deletedCount = removedEvents;
    }
    lastError_.clear();
    return true;
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
               "ADD COLUMN recognizer_name TEXT NOT NULL DEFAULT '';")
        && ensureColumnLocked(
               "detection_records",
               "track_id",
               "ALTER TABLE detection_records "
               "ADD COLUMN track_id INTEGER NOT NULL DEFAULT 0;")
        && ensureColumnLocked(
               "face_recognition_events",
               "track_id",
               "ALTER TABLE face_recognition_events "
               "ADD COLUMN track_id INTEGER NOT NULL DEFAULT 0;")
        && executeLocked(kCreateTrackIndexesSql);
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
        && sqlite3_bind_int64(statement, 5, result.trackId) == SQLITE_OK
        && sqlite3_bind_int(statement, 6, result.classId) == SQLITE_OK
        && sqlite3_bind_text(statement, 7, result.className.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
        && sqlite3_bind_double(statement, 8, result.confidence) == SQLITE_OK
        && sqlite3_bind_double(statement, 9, result.box.x) == SQLITE_OK
        && sqlite3_bind_double(statement, 10, result.box.y) == SQLITE_OK
        && sqlite3_bind_double(statement, 11, result.box.width) == SQLITE_OK
        && sqlite3_bind_double(statement, 12, result.box.height) == SQLITE_OK
        && sqlite3_bind_int64(statement, 13, recordedAtMs) == SQLITE_OK;
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

bool SQLiteDetectionStorage::upsertFaceTrackLocked(
    std::int64_t sessionId,
    const FaceTrackSnapshot& snapshot,
    std::int64_t updatedAtMs)
{
    if (sessionId <= 0 || snapshot.trackId <= 0)
    {
        lastError_ = "Face track snapshot arguments are invalid.";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            kUpsertFaceTrackSql,
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare face track upsert");
        return false;
    }

    int parameterIndex = 1;
    bool bound = true;
    auto bindInt64 = [&](std::int64_t value) {
        bound = bound
            && sqlite3_bind_int64(statement, parameterIndex++, value) == SQLITE_OK;
    };
    auto bindInt = [&](int value) {
        bound = bound
            && sqlite3_bind_int(statement, parameterIndex++, value) == SQLITE_OK;
    };
    auto bindDouble = [&](float value) {
        bound = bound
            && sqlite3_bind_double(statement, parameterIndex++, value) == SQLITE_OK;
    };
    auto bindText = [&](const std::string& value) {
        bound = bound
            && sqlite3_bind_text(
                   statement,
                   parameterIndex++,
                   value.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK;
    };
    auto bindOptionalFaceId = [&](const std::optional<std::int64_t>& faceId) {
        bound = bound
            && (faceId.has_value()
                    ? sqlite3_bind_int64(statement, parameterIndex++, *faceId)
                    : sqlite3_bind_null(statement, parameterIndex++))
                == SQLITE_OK;
    };
    auto bindRecognitionState = [&](const FaceTrackRecognitionState& state) {
        bindText(state.decision);
        bindOptionalFaceId(state.faceId);
        bindText(state.faceCode);
        bindText(state.faceName);
        bindDouble(state.similarity);
        bindDouble(state.threshold);
        bindInt64(state.observedAtPtsMs);
    };

    bindInt64(sessionId);
    bindText(snapshot.sourceId);
    bindInt64(snapshot.trackId);
    bindInt(snapshot.classId);
    bindText(snapshot.className);
    bindInt64(snapshot.firstFrameIndex);
    bindInt64(snapshot.firstPtsMs);
    bindInt64(snapshot.lastFrameIndex);
    bindInt64(snapshot.lastPtsMs);
    bindInt64(snapshot.durationMs);
    bindInt(snapshot.detectionCount);
    bindInt(snapshot.missedUpdates);
    bindInt(snapshot.active ? 1 : 0);
    bindRecognitionState(snapshot.firstRecognition);
    bindRecognitionState(snapshot.lastRecognition);
    bindInt64(updatedAtMs);
    bindInt64(updatedAtMs);

    const bool completed = bound && sqlite3_step(statement) == SQLITE_DONE;
    if (!completed)
    {
        setLastErrorLocked("Could not upsert face track");
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
    const std::string& eventType,
    const std::optional<std::int64_t>& faceId,
    std::int64_t createdAtMs)
{
    if (recordId <= 0
        || sessionId <= 0
        || eventType.empty())
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
        && sqlite3_bind_int64(statement, 6, result.trackId) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               7,
               eventType.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && (faceId.has_value()
                ? sqlite3_bind_int64(statement, 8, *faceId) == SQLITE_OK
                : sqlite3_bind_null(statement, 8) == SQLITE_OK)
        && sqlite3_bind_text(
               statement,
               9,
               result.face.faceCode.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               10,
               result.face.faceName.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_double(statement, 11, result.face.distance) == SQLITE_OK
        && sqlite3_bind_double(statement, 12, result.face.similarity) == SQLITE_OK
        && sqlite3_bind_double(statement, 13, result.face.threshold) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               14,
               result.face.recognizerName.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK
        && sqlite3_bind_int64(statement, 15, createdAtMs) == SQLITE_OK;
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
    const std::string& eventType,
    const std::optional<std::int64_t>& faceId,
    std::int64_t trackId,
    std::int64_t createdAtMs,
    bool* shouldInsert)
{
    if (shouldInsert == nullptr
        || sessionId <= 0
        || eventType.empty()
        || createdAtMs <= 0)
    {
        lastError_ = "Recognition event deduplication arguments are invalid.";
        return false;
    }

    *shouldInsert = true;
    const bool useTrackDeduplication = trackId > 0;
    std::string sql = useTrackDeduplication
        ? kTrackRecognitionEventBaseSql
        : kRecentRecognitionEventBaseSql;
    if (faceId.has_value())
    {
        sql += "AND face_identity_id = ? ";
    }
    else
    {
        sql += "AND face_identity_id IS NULL ";
    }
    sql += "ORDER BY id DESC LIMIT 1;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database_,
            sql.c_str(),
            -1,
            &statement,
            nullptr)
        != SQLITE_OK)
    {
        setLastErrorLocked("Could not prepare recognition event deduplication query");
        return false;
    }

    bool bound =
        sqlite3_bind_int64(statement, 1, sessionId) == SQLITE_OK
        && sqlite3_bind_text(
               statement,
               2,
               sourceId.c_str(),
               -1,
               SQLITE_TRANSIENT)
            == SQLITE_OK;
    int identityParameterIndex = 5;
    if (useTrackDeduplication)
    {
        bound = bound
            && sqlite3_bind_int64(statement, 3, trackId) == SQLITE_OK
            && sqlite3_bind_text(
                   statement,
                   4,
                   eventType.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK;
    }
    else
    {
        const std::int64_t cutoffMs =
            std::max<std::int64_t>(
                0,
                createdAtMs - kRecognitionEventCooldownMs);
        bound = bound
            && sqlite3_bind_text(
                   statement,
                   3,
                   eventType.c_str(),
                   -1,
                   SQLITE_TRANSIENT)
                == SQLITE_OK
            && sqlite3_bind_int64(statement, 4, cutoffMs) == SQLITE_OK;
    }
    const bool identityBound = !bound
        || !faceId.has_value()
        || sqlite3_bind_int64(
               statement,
               identityParameterIndex,
               *faceId)
            == SQLITE_OK;
    if (!bound || !identityBound)
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
        result.trackId = sqlite3_column_int64(statement, 3);
        result.classId = sqlite3_column_int(statement, 4);
        result.className = textColumn(statement, 5);
        result.confidence = static_cast<float>(sqlite3_column_double(statement, 6));
        result.box.x = static_cast<float>(sqlite3_column_double(statement, 7));
        result.box.y = static_cast<float>(sqlite3_column_double(statement, 8));
        result.box.width = static_cast<float>(sqlite3_column_double(statement, 9));
        result.box.height = static_cast<float>(sqlite3_column_double(statement, 10));
        result.face.faceId = optionalInt64Column(statement, 11);
        result.face.matched = result.face.faceId.has_value();
        result.face.faceCode = textColumn(statement, 12);
        result.face.faceName = textColumn(statement, 13);
        result.face.matchedAtMs = sqlite3_column_int64(statement, 14);
        result.face.distance = static_cast<float>(sqlite3_column_double(statement, 15));
        result.face.similarity = static_cast<float>(sqlite3_column_double(statement, 16));
        result.face.threshold = static_cast<float>(sqlite3_column_double(statement, 17));
        result.face.recognizerName = textColumn(statement, 18);
        if (optionalInt64Column(statement, 19).has_value())
        {
            result.trackState.trackId = result.trackId;
            result.trackState.sourceId = result.sourceId;
            result.trackState.classId = result.classId;
            result.trackState.className = result.className;
            result.trackState.firstFrameIndex =
                sqlite3_column_int64(statement, 20);
            result.trackState.firstPtsMs =
                sqlite3_column_int64(statement, 21);
            result.trackState.lastFrameIndex =
                sqlite3_column_int64(statement, 22);
            result.trackState.lastPtsMs =
                sqlite3_column_int64(statement, 23);
            result.trackState.durationMs =
                sqlite3_column_int64(statement, 24);
            result.trackState.detectionCount =
                sqlite3_column_int(statement, 25);
            result.trackState.missedUpdates =
                sqlite3_column_int(statement, 26);
            result.trackState.active =
                sqlite3_column_int(statement, 27) != 0;

            result.trackState.firstRecognition.decision =
                textColumn(statement, 28);
            result.trackState.firstRecognition.available =
                !result.trackState.firstRecognition.decision.empty();
            result.trackState.firstRecognition.faceId =
                optionalInt64Column(statement, 29);
            result.trackState.firstRecognition.faceCode =
                textColumn(statement, 30);
            result.trackState.firstRecognition.faceName =
                textColumn(statement, 31);
            result.trackState.firstRecognition.similarity =
                static_cast<float>(sqlite3_column_double(statement, 32));
            result.trackState.firstRecognition.threshold =
                static_cast<float>(sqlite3_column_double(statement, 33));
            result.trackState.firstRecognition.observedAtPtsMs =
                sqlite3_column_int64(statement, 34);
            result.trackState.firstRecognition.matched =
                result.trackState.firstRecognition.faceId.has_value();

            result.trackState.lastRecognition.decision =
                textColumn(statement, 35);
            result.trackState.lastRecognition.available =
                !result.trackState.lastRecognition.decision.empty();
            result.trackState.lastRecognition.faceId =
                optionalInt64Column(statement, 36);
            result.trackState.lastRecognition.faceCode =
                textColumn(statement, 37);
            result.trackState.lastRecognition.faceName =
                textColumn(statement, 38);
            result.trackState.lastRecognition.similarity =
                static_cast<float>(sqlite3_column_double(statement, 39));
            result.trackState.lastRecognition.threshold =
                static_cast<float>(sqlite3_column_double(statement, 40));
            result.trackState.lastRecognition.observedAtPtsMs =
                sqlite3_column_int64(statement, 41);
            result.trackState.lastRecognition.matched =
                result.trackState.lastRecognition.faceId.has_value();
        }
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
