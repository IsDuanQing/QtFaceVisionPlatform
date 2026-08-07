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

    storage.close();
    std::remove(databasePath);
    return 0;
}
