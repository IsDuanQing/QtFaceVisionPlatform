#ifndef IVP_TRACKING_FACETRACKER_H
#define IVP_TRACKING_FACETRACKER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/DetectionResult.h"

namespace ivp
{

struct FaceTrackerConfig
{
    float minIntersectionOverUnion = 0.12F;
    float maxCenterDistanceRatio = 0.75F;
    int maxMissedUpdates = 8;
    std::int64_t maxLostDurationMs = 1500;
};

class FaceTracker final
{
public:
    explicit FaceTracker(const FaceTrackerConfig& config = {});

    void setConfig(const FaceTrackerConfig& config);
    FaceTrackerConfig config() const;

    // Clears active trajectories without reusing track identifiers.
    void reset();
    void update(
        std::int64_t frameIndex,
        std::int64_t ptsMs,
        const std::string& sourceId,
        DetectionResults* detections);
    void updateRecognition(DetectionResults* detections);

    // Returns tracks that expired since the previous update.
    FaceTrackSnapshots takeEndedTracks();

    // Closes all active tracks, for example when playback is stopped. The
    // snapshots remain available through takeEndedTracks().
    void finish();

    std::size_t activeTrackCount() const;

private:
    struct Track
    {
        std::int64_t id = 0;
        int classId = -1;
        BoundingBox box;
        float velocityX = 0.0F;
        float velocityY = 0.0F;
        std::int64_t lastFrameIndex = 0;
        std::int64_t lastPtsMs = 0;
        int missedUpdates = 0;
        FaceTrackSnapshot snapshot;
    };

    BoundingBox predictedBox(const Track& track) const;
    bool isExpired(const Track& track, std::int64_t ptsMs) const;
    void removeExpiredTracks(std::int64_t ptsMs);

    FaceTrackerConfig config_;
    std::vector<Track> tracks_;
    FaceTrackSnapshots endedTracks_;
    std::string sourceId_;
    std::int64_t nextTrackId_;
};

} // namespace ivp

#endif // IVP_TRACKING_FACETRACKER_H
