#ifndef IVP_NETWORK_DETECTIONFRAMEPACKET_H
#define IVP_NETWORK_DETECTIONFRAMEPACKET_H

#include <cstdint>
#include <string>

#include "common/DetectionResult.h"

namespace ivp
{

struct DetectionFramePacket
{
    std::string taskId;
    std::string productionLineId;
    std::string batchId;
    std::string sourceId;
    std::int64_t frameIndex = 0;
    std::int64_t ptsMs = 0;
    std::int64_t recordedAtMs = 0;
    DetectionResults results;
};

} // namespace ivp

#endif // IVP_NETWORK_DETECTIONFRAMEPACKET_H
