#ifndef IVP_COMMON_FACETRACK_H
#define IVP_COMMON_FACETRACK_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ivp
{

struct FaceTrackRecognitionState
{
    bool available = false;
    bool matched = false;
    std::optional<std::int64_t> faceId;
    std::string faceCode;
    std::string faceName;
    std::string decision;
    float similarity = 0.0F;
    float threshold = 0.0F;
    std::int64_t observedAtPtsMs = 0;
};

struct FaceTrackSnapshot
{
    std::int64_t trackId = 0;
    std::string sourceId;
    int classId = -1;
    std::string className;
    std::int64_t firstFrameIndex = 0;
    std::int64_t firstPtsMs = 0;
    std::int64_t lastFrameIndex = 0;
    std::int64_t lastPtsMs = 0;
    std::int64_t durationMs = 0;
    int detectionCount = 0;
    int missedUpdates = 0;
    bool active = true;
    FaceTrackRecognitionState firstRecognition;
    FaceTrackRecognitionState lastRecognition;
};

using FaceTrackSnapshots = std::vector<FaceTrackSnapshot>;

} // namespace ivp

#endif // IVP_COMMON_FACETRACK_H
