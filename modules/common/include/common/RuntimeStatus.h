#ifndef IVP_COMMON_RUNTIMESTATUS_H
#define IVP_COMMON_RUNTIMESTATUS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace ivp
{

enum class RuntimeState
{
    Idle,
    Ready,
    Running,
    Paused,
    Stopping,
    Completed,
    Error
};

inline const char* runtimeStateName(RuntimeState state)
{
    switch (state)
    {
    case RuntimeState::Idle:
        return "Idle";
    case RuntimeState::Ready:
        return "Ready";
    case RuntimeState::Running:
        return "Running";
    case RuntimeState::Paused:
        return "Paused";
    case RuntimeState::Stopping:
        return "Stopping";
    case RuntimeState::Completed:
        return "Completed";
    case RuntimeState::Error:
        return "Error";
    }

    return "Unknown";
}

struct RuntimeMetrics
{
    std::int64_t decodedFrames = 0;
    std::int64_t displayedFrames = 0;
    std::int64_t inferredFrames = 0;
    std::int64_t droppedDisplayFrames = 0;
    std::int64_t droppedInferenceFrames = 0;
    double decodeFps = 0.0;
    double displayFps = 0.0;
    double inferenceFps = 0.0;
    std::size_t displayQueueSize = 0;
    std::size_t inferenceQueueSize = 0;
    std::int64_t currentFrameIndex = -1;
    std::int64_t currentPtsMs = 0;
    std::int64_t lastInferenceLatencyMs = 0;
};

struct RuntimeStatus
{
    RuntimeState state = RuntimeState::Idle;
    RuntimeMetrics metrics;
    std::string lastError;
};

} // namespace ivp

#endif // IVP_COMMON_RUNTIMESTATUS_H
