#include <cassert>

#include "tracking/FaceTracker.h"

namespace
{

ivp::DetectionResult faceAt(float x, float y)
{
    ivp::DetectionResult result;
    result.sourceId = "camera_a";
    result.classId = 0;
    result.className = "face";
    result.confidence = 0.9F;
    result.box = ivp::BoundingBox{x, y, 80.0F, 80.0F};
    return result;
}

} // namespace

int main()
{
    ivp::FaceTrackerConfig config;
    config.maxMissedUpdates = 1;
    config.maxLostDurationMs = 1000;
    ivp::FaceTracker tracker(config);

    ivp::DetectionResults firstFrame{faceAt(100.0F, 100.0F)};
    tracker.update(1, 0, "camera_a", &firstFrame);
    assert(firstFrame.front().trackId > 0);
    assert(firstFrame.front().trackState.firstFrameIndex == 1);
    assert(firstFrame.front().trackState.durationMs == 0);
    assert(firstFrame.front().trackState.detectionCount == 1);
    const std::int64_t firstTrackId = firstFrame.front().trackId;

    ivp::DetectionResults movedFrame{faceAt(108.0F, 104.0F)};
    tracker.update(2, 33, "camera_a", &movedFrame);
    assert(movedFrame.front().trackId == firstTrackId);
    assert(movedFrame.front().trackState.durationMs == 33);
    assert(movedFrame.front().trackState.detectionCount == 2);

    movedFrame.front().face.matched = true;
    movedFrame.front().face.faceId = 7;
    movedFrame.front().face.faceCode = "employee_001";
    movedFrame.front().face.faceName = "Employee 1";
    movedFrame.front().face.similarity = 0.88F;
    movedFrame.front().face.threshold = 0.36F;
    movedFrame.front().face.decision = "matched";
    tracker.updateRecognition(&movedFrame);
    assert(movedFrame.front().trackState.firstRecognition.decision == "matched");
    assert(movedFrame.front().trackState.lastRecognition.faceName == "Employee 1");

    ivp::DetectionResults missedFrame;
    tracker.update(3, 66, "camera_a", &missedFrame);
    ivp::DetectionResults recoveredFrame{faceAt(116.0F, 108.0F)};
    tracker.update(4, 99, "camera_a", &recoveredFrame);
    assert(recoveredFrame.front().trackId == firstTrackId);

    tracker.update(5, 132, "camera_a", &missedFrame);
    tracker.update(6, 165, "camera_a", &missedFrame);
    ivp::DetectionResults reenteredFrame{faceAt(118.0F, 110.0F)};
    tracker.update(7, 198, "camera_a", &reenteredFrame);
    assert(reenteredFrame.front().trackId > firstTrackId);
    const std::int64_t reenteredTrackId = reenteredFrame.front().trackId;

    ivp::DetectionResults otherSourceFrame{faceAt(118.0F, 110.0F)};
    otherSourceFrame.front().sourceId = "camera_b";
    tracker.update(8, 231, "camera_b", &otherSourceFrame);
    assert(otherSourceFrame.front().trackId > reenteredTrackId);

    tracker.finish();
    const ivp::FaceTrackSnapshots endedTracks = tracker.takeEndedTracks();
    assert(!endedTracks.empty());
    assert(!endedTracks.back().active);

    return 0;
}
