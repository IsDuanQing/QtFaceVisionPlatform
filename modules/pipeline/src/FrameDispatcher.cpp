#include "pipeline/FrameDispatcher.h"

#include <utility>

namespace ivp
{

FrameDispatcher::FrameDispatcher(std::size_t displayCapacity, std::size_t inferenceCapacity)
    : displayQueue_(displayCapacity),
      inferenceQueue_(inferenceCapacity)
{
}

bool FrameDispatcher::dispatch(
    VideoFrame frame,
    FrameQueuePolicy displayPolicy,
    FrameQueuePolicy inferencePolicy)
{
    auto sharedFrame = std::make_shared<VideoFrame>(std::move(frame));

    if (!pushFrame(&displayQueue_, sharedFrame, displayPolicy))
    {
        return false;
    }

    return pushFrame(&inferenceQueue_, std::move(sharedFrame), inferencePolicy);
}

bool FrameDispatcher::tryPopDisplay(VideoFramePtr* frame)
{
    return displayQueue_.tryPop(frame);
}

bool FrameDispatcher::popInference(VideoFramePtr* frame)
{
    return inferenceQueue_.pop(frame);
}

void FrameDispatcher::close()
{
    displayQueue_.close();
    inferenceQueue_.close();
}

void FrameDispatcher::reset()
{
    displayQueue_.reset();
    inferenceQueue_.reset();
}

void FrameDispatcher::clear()
{
    displayQueue_.clear();
    inferenceQueue_.clear();
}

std::size_t FrameDispatcher::displayQueueSize() const
{
    return displayQueue_.size();
}

std::size_t FrameDispatcher::inferenceQueueSize() const
{
    return inferenceQueue_.size();
}

bool FrameDispatcher::pushFrame(
    BlockingQueue<VideoFramePtr>* queue,
    VideoFramePtr frame,
    FrameQueuePolicy policy)
{
    if (queue == nullptr || frame == nullptr)
    {
        return false;
    }

    if (policy == FrameQueuePolicy::DropOldest)
    {
        return queue->pushDropOldest(std::move(frame));
    }

    return queue->push(std::move(frame));
}

} // namespace ivp
