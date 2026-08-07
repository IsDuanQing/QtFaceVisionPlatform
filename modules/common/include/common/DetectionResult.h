#ifndef IVP_COMMON_DETECTIONRESULT_H
#define IVP_COMMON_DETECTIONRESULT_H

#include <cstdint>
#include <string>
#include <vector>

namespace ivp
{

struct BoundingBox
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct DetectionResult
{
    std::string sourceId;
    std::int64_t frameIndex = 0;
    std::int64_t ptsMs = 0;
    int classId = -1;
    std::string className;
    float confidence = 0.0F;
    BoundingBox box;
};

using DetectionResults = std::vector<DetectionResult>;

} // namespace ivp

#endif // IVP_COMMON_DETECTIONRESULT_H
