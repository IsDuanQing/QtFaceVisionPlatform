#include "tracking/FaceTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{

float boxArea(const ivp::BoundingBox& box)
{
    return std::max(0.0F, box.width) * std::max(0.0F, box.height);
}

float intersectionOverUnion(
    const ivp::BoundingBox& lhs,
    const ivp::BoundingBox& rhs)
{
    const float left = std::max(lhs.x, rhs.x);
    const float top = std::max(lhs.y, rhs.y);
    const float right =
        std::min(lhs.x + lhs.width, rhs.x + rhs.width);
    const float bottom =
        std::min(lhs.y + lhs.height, rhs.y + rhs.height);
    const float intersection =
        std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
    const float unionArea = boxArea(lhs) + boxArea(rhs) - intersection;
    return unionArea > 0.0F ? intersection / unionArea : 0.0F;
}

float centerDistanceRatio(
    const ivp::BoundingBox& lhs,
    const ivp::BoundingBox& rhs)
{
    const float lhsCenterX = lhs.x + lhs.width * 0.5F;
    const float lhsCenterY = lhs.y + lhs.height * 0.5F;
    const float rhsCenterX = rhs.x + rhs.width * 0.5F;
    const float rhsCenterY = rhs.y + rhs.height * 0.5F;
    const float deltaX = lhsCenterX - rhsCenterX;
    const float deltaY = lhsCenterY - rhsCenterY;
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    const float lhsDiagonal =
        std::sqrt(lhs.width * lhs.width + lhs.height * lhs.height);
    const float rhsDiagonal =
        std::sqrt(rhs.width * rhs.width + rhs.height * rhs.height);
    const float scale = std::max(1.0F, std::max(lhsDiagonal, rhsDiagonal));
    return distance / scale;
}

bool validBox(const ivp::BoundingBox& box)
{
    return box.width > 0.0F && box.height > 0.0F;
}

ivp::FaceTrackerConfig normalizedConfig(ivp::FaceTrackerConfig config)
{
    config.minIntersectionOverUnion =
        std::clamp(config.minIntersectionOverUnion, 0.0F, 1.0F);
    config.maxCenterDistanceRatio =
        std::max(0.0F, config.maxCenterDistanceRatio);
    config.maxMissedUpdates = std::max(0, config.maxMissedUpdates);
    config.maxLostDurationMs =
        std::max<std::int64_t>(0, config.maxLostDurationMs);
    return config;
}

struct MatchCandidate
{
    std::size_t trackIndex = 0;
    std::size_t detectionIndex = 0;
    float score = 0.0F;
};

} // namespace

namespace ivp
{

FaceTracker::FaceTracker(const FaceTrackerConfig& config)
    : config_(normalizedConfig(config)),
      tracks_(),
      endedTracks_(),
      sourceId_(),
      nextTrackId_(1)
{
}

void FaceTracker::setConfig(const FaceTrackerConfig& config)
{
    config_ = normalizedConfig(config);
}

FaceTrackerConfig FaceTracker::config() const
{
    return config_;
}

void FaceTracker::reset()
{
    tracks_.clear();
    endedTracks_.clear();
    sourceId_.clear();
}

void FaceTracker::update(
    std::int64_t frameIndex,
    std::int64_t ptsMs,
    const std::string& sourceId,
    DetectionResults* detections)
{
    if (detections == nullptr)
    {
        return;
    }

    std::string effectiveSourceId = sourceId;
    if (effectiveSourceId.empty() && !detections->empty())
    {
        effectiveSourceId = detections->front().sourceId;
    }
    if (!sourceId_.empty()
        && !effectiveSourceId.empty()
        && sourceId_ != effectiveSourceId)
    {
        finish();
    }
    if (!effectiveSourceId.empty())
    {
        sourceId_ = effectiveSourceId;
    }

    removeExpiredTracks(ptsMs);
    for (DetectionResult& detection : *detections)
    {
        detection.trackId = 0;
        detection.trackState = FaceTrackSnapshot();
    }

    std::vector<MatchCandidate> candidates;
    candidates.reserve(tracks_.size() * detections->size());
    for (std::size_t trackIndex = 0;
         trackIndex < tracks_.size();
         ++trackIndex)
    {
        const Track& track = tracks_[trackIndex];
        const BoundingBox predicted = predictedBox(track);
        for (std::size_t detectionIndex = 0;
             detectionIndex < detections->size();
             ++detectionIndex)
        {
            const DetectionResult& detection = (*detections)[detectionIndex];
            if (!validBox(detection.box)
                || detection.classId != track.classId)
            {
                continue;
            }

            const float overlap =
                intersectionOverUnion(predicted, detection.box);
            const float centerRatio =
                centerDistanceRatio(predicted, detection.box);
            if (overlap < config_.minIntersectionOverUnion
                && centerRatio > config_.maxCenterDistanceRatio)
            {
                continue;
            }

            const float centerScore =
                std::max(0.0F, 1.0F - centerRatio);
            candidates.push_back(MatchCandidate{
                trackIndex,
                detectionIndex,
                overlap * 0.70F + centerScore * 0.30F});
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const MatchCandidate& lhs, const MatchCandidate& rhs) {
            return lhs.score > rhs.score;
        });

    std::vector<bool> matchedTracks(tracks_.size(), false);
    std::vector<bool> matchedDetections(detections->size(), false);
    for (const MatchCandidate& candidate : candidates)
    {
        if (matchedTracks[candidate.trackIndex]
            || matchedDetections[candidate.detectionIndex])
        {
            continue;
        }

        Track& track = tracks_[candidate.trackIndex];
        DetectionResult& detection = (*detections)[candidate.detectionIndex];
        const float deltaX = detection.box.x - track.box.x;
        const float deltaY = detection.box.y - track.box.y;
        track.velocityX = track.velocityX * 0.45F + deltaX * 0.55F;
        track.velocityY = track.velocityY * 0.45F + deltaY * 0.55F;
        track.box = detection.box;
        track.lastFrameIndex = frameIndex;
        track.lastPtsMs = ptsMs;
        track.missedUpdates = 0;
        track.snapshot.lastFrameIndex = frameIndex;
        track.snapshot.lastPtsMs = ptsMs;
        track.snapshot.durationMs = std::max<std::int64_t>(
            0,
            track.snapshot.lastPtsMs - track.snapshot.firstPtsMs);
        ++track.snapshot.detectionCount;
        track.snapshot.missedUpdates = 0;
        track.snapshot.active = true;
        detection.trackId = track.id;
        detection.trackState = track.snapshot;
        matchedTracks[candidate.trackIndex] = true;
        matchedDetections[candidate.detectionIndex] = true;
    }

    for (std::size_t trackIndex = 0;
         trackIndex < tracks_.size();
         ++trackIndex)
    {
        if (!matchedTracks[trackIndex])
        {
            ++tracks_[trackIndex].missedUpdates;
            tracks_[trackIndex].snapshot.missedUpdates =
                tracks_[trackIndex].missedUpdates;
        }
    }

    for (std::size_t detectionIndex = 0;
         detectionIndex < detections->size();
         ++detectionIndex)
    {
        if (matchedDetections[detectionIndex])
        {
            continue;
        }

        DetectionResult& detection = (*detections)[detectionIndex];
        if (!validBox(detection.box))
        {
            continue;
        }

        if (nextTrackId_ == std::numeric_limits<std::int64_t>::max())
        {
            nextTrackId_ = 1;
        }
        const std::int64_t trackId = nextTrackId_++;
        detection.trackId = trackId;
        Track track{
            trackId,
            detection.classId,
            detection.box,
            0.0F,
            0.0F,
            frameIndex,
            ptsMs,
            0,
            FaceTrackSnapshot{}};
        track.snapshot.trackId = trackId;
        track.snapshot.sourceId = effectiveSourceId;
        track.snapshot.classId = detection.classId;
        track.snapshot.className = detection.className;
        track.snapshot.firstFrameIndex = frameIndex;
        track.snapshot.firstPtsMs = ptsMs;
        track.snapshot.lastFrameIndex = frameIndex;
        track.snapshot.lastPtsMs = ptsMs;
        track.snapshot.durationMs = 0;
        track.snapshot.detectionCount = 1;
        track.snapshot.missedUpdates = 0;
        track.snapshot.active = true;
        detection.trackState = track.snapshot;
        tracks_.push_back(std::move(track));
    }

    removeExpiredTracks(ptsMs);
}

void FaceTracker::updateRecognition(DetectionResults* detections)
{
    if (detections == nullptr)
    {
        return;
    }

    for (DetectionResult& detection : *detections)
    {
        if (detection.trackId <= 0 || detection.face.decision.empty())
        {
            continue;
        }

        const auto trackIt = std::find_if(
            tracks_.begin(),
            tracks_.end(),
            [&detection](const Track& track) {
                return track.id == detection.trackId;
            });
        if (trackIt == tracks_.end())
        {
            continue;
        }

        FaceTrackRecognitionState state;
        state.available = true;
        state.matched = detection.face.matched;
        state.faceId = detection.face.faceId;
        state.faceCode = detection.face.faceCode;
        state.faceName = detection.face.faceName;
        state.decision = detection.face.decision;
        state.similarity = detection.face.similarity;
        state.threshold = detection.face.threshold;
        state.observedAtPtsMs = detection.ptsMs;

        if (!trackIt->snapshot.firstRecognition.available)
        {
            trackIt->snapshot.firstRecognition = state;
        }
        trackIt->snapshot.lastRecognition = state;
        detection.trackState = trackIt->snapshot;
    }
}

FaceTrackSnapshots FaceTracker::takeEndedTracks()
{
    FaceTrackSnapshots ended;
    ended.swap(endedTracks_);
    return ended;
}

void FaceTracker::finish()
{
    for (Track& track : tracks_)
    {
        track.snapshot.missedUpdates = track.missedUpdates;
        track.snapshot.active = false;
        endedTracks_.push_back(track.snapshot);
    }
    tracks_.clear();
    sourceId_.clear();
}

std::size_t FaceTracker::activeTrackCount() const
{
    return tracks_.size();
}

BoundingBox FaceTracker::predictedBox(const Track& track) const
{
    const float predictionSteps = static_cast<float>(
        std::min(3, track.missedUpdates + 1));
    BoundingBox predicted = track.box;
    predicted.x += track.velocityX * predictionSteps;
    predicted.y += track.velocityY * predictionSteps;
    return predicted;
}

bool FaceTracker::isExpired(
    const Track& track,
    std::int64_t ptsMs) const
{
    if (track.missedUpdates > config_.maxMissedUpdates)
    {
        return true;
    }

    return config_.maxLostDurationMs > 0
        && ptsMs >= track.lastPtsMs
        && ptsMs - track.lastPtsMs > config_.maxLostDurationMs;
}

void FaceTracker::removeExpiredTracks(std::int64_t ptsMs)
{
    auto it = tracks_.begin();
    while (it != tracks_.end())
    {
        if (!isExpired(*it, ptsMs))
        {
            ++it;
            continue;
        }

        it->snapshot.missedUpdates = it->missedUpdates;
        it->snapshot.active = false;
        endedTracks_.push_back(it->snapshot);
        it = tracks_.erase(it);
    }
}

} // namespace ivp
