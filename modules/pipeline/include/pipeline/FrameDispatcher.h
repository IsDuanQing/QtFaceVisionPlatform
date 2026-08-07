#ifndef IVP_PIPELINE_FRAMEDISPATCHER_H
#define IVP_PIPELINE_FRAMEDISPATCHER_H

#include <cstddef>
#include <memory>

#include "common/BlockingQueue.h"
#include "common/VideoFrame.h"

namespace ivp
{

using VideoFramePtr = std::shared_ptr<const VideoFrame>;

enum class FrameQueuePolicy
{
    BlockWhenFull,
    DropOldest
};

// Splits decoded frames into independent consumer queues.
// Display and inference must not compete for the same queue because their
// latency and frame-dropping policies are different.
class FrameDispatcher final
{
public:
    FrameDispatcher(std::size_t displayCapacity, std::size_t inferenceCapacity);

    FrameDispatcher(const FrameDispatcher&) = delete;
    FrameDispatcher& operator=(const FrameDispatcher&) = delete;

    bool dispatch(
        VideoFrame frame,
        FrameQueuePolicy displayPolicy,
        FrameQueuePolicy inferencePolicy);

    bool tryPopDisplay(VideoFramePtr* frame);
    bool popInference(VideoFramePtr* frame);

    void close();
    void reset();
    void clear();

    std::size_t displayQueueSize() const;
    std::size_t inferenceQueueSize() const;

private:
    static bool pushFrame(
        BlockingQueue<VideoFramePtr>* queue,
        VideoFramePtr frame,
        FrameQueuePolicy policy);

    BlockingQueue<VideoFramePtr> displayQueue_;
    BlockingQueue<VideoFramePtr> inferenceQueue_;
};

} // namespace ivp

#endif // IVP_PIPELINE_FRAMEDISPATCHER_H
