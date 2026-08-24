#include "pipeline/FrameDispatcher.h"

#include <utility>

namespace ivp
{

FrameDispatcher::FrameDispatcher(std::size_t displayCapacity, std::size_t inferenceCapacity)
    : displayQueue_(displayCapacity),
      inferenceQueue_(inferenceCapacity),
      droppedDisplayFrames_(0),
      droppedInferenceFrames_(0)
{
}

bool FrameDispatcher::dispatch(
    VideoFrame frame,
    FrameQueuePolicy displayPolicy,
    FrameQueuePolicy inferencePolicy)
{
    auto sharedFrame = std::make_shared<VideoFrame>(std::move(frame));

    if (!pushFrame(&displayQueue_, sharedFrame, displayPolicy, &droppedDisplayFrames_))
    {
        return false;
    }

    return pushFrame(
        &inferenceQueue_,
        std::move(sharedFrame),
        inferencePolicy,
        &droppedInferenceFrames_);
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
    droppedDisplayFrames_.store(0);
    droppedInferenceFrames_.store(0);
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

std::int64_t FrameDispatcher::droppedDisplayFrames() const
{
    return droppedDisplayFrames_.load();
}

std::int64_t FrameDispatcher::droppedInferenceFrames() const
{
    return droppedInferenceFrames_.load();
}

bool FrameDispatcher::pushFrame(
    BlockingQueue<VideoFramePtr>* queue,
    VideoFramePtr frame,
    FrameQueuePolicy policy,
    std::atomic<std::int64_t>* droppedFrames)
{
    if (queue == nullptr || frame == nullptr)
    {
        return false;
    }

    if (policy == FrameQueuePolicy::DropOldest)
    {
        bool dropped = false;
        const bool pushed = queue->pushDropOldest(std::move(frame), &dropped);
        if (pushed && dropped && droppedFrames != nullptr)
        {
            droppedFrames->fetch_add(1);
        }
        return pushed;
    }

    return queue->push(std::move(frame));
}

} // namespace ivp
