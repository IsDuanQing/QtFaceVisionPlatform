#include <cassert>
#include <cstdio>

#include "storage/SQLiteDetectionStorage.h"

int main()
{
    const char* databasePath = "ivp_storage_smoke_test.db";
    std::remove(databasePath);

    ivp::SQLiteDetectionStorage storage;
    assert(storage.open(databasePath));

    const std::int64_t sessionId =
        storage.startSession("camera_a", "rtsp://127.0.0.1:8554/test");
    assert(sessionId > 0);

    ivp::DetectionResult defect;
    defect.sourceId = "camera_a";
    defect.frameIndex = 42;
    defect.ptsMs = 1400;
    defect.classId = 3;
    defect.className = "scratch";
    defect.confidence = 0.91F;
    defect.box = ivp::BoundingBox{12.0F, 18.0F, 50.0F, 26.0F};

    assert(storage.saveFrameResults(sessionId, "camera_a", 42, 1400, {defect}));
    assert(storage.saveFrameResults(sessionId, "camera_a", 43, 1433, {}));
    assert(storage.finishSession(sessionId));

    const ivp::DetectionResults frameResults =
        storage.resultsForFrame(sessionId, 42);
    assert(frameResults.size() == 1);
    assert(frameResults.front().className == "scratch");

    const ivp::DetectionResults recentResults = storage.recentResults(5);
    assert(recentResults.size() == 1);
    assert(recentResults.front().frameIndex == 42);

    const ivp::InspectionSessionSummaries sessions = storage.recentSessions(5);
    assert(sessions.size() == 1);
    assert(sessions.front().sessionId == sessionId);
    assert(sessions.front().frameCount == 2);
    assert(sessions.front().objectCount == 1);
    assert(sessions.front().endedAtMs.has_value());

    const ivp::DetectionHistoryRows recentHistory = storage.recentHistory(5);
    assert(recentHistory.size() == 1);
    assert(recentHistory.front().sessionId == sessionId);
    assert(recentHistory.front().frameIndex == 42);
    assert(recentHistory.front().frameObjectCount == 1);

    ivp::DetectionHistoryQuery query;
    query.sessionId = sessionId;
    query.sourceLike = std::string("camera");
    query.classLike = std::string("SCR");
    query.limit = 5;
    const ivp::DetectionHistoryRows filteredHistory = storage.queryHistory(query);
    assert(filteredHistory.size() == 1);
    assert(filteredHistory.front().className == "scratch");

    storage.close();
    std::remove(databasePath);
    return 0;
}
