#include "inference/YoloPostprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace
{

bool isCandidateAttributeCount(int value, int classCount)
{
    return value == classCount + 4 || value == classCount + 5;
}

} // namespace

namespace ivp
{

YoloPostprocessor::YoloPostprocessor(YoloPostprocessorConfig config)
    : config_(std::move(config))
{
    config_.confidenceThreshold = std::clamp(
        config_.confidenceThreshold, 0.0F, 1.0F);
    config_.nmsThreshold = std::clamp(config_.nmsThreshold, 0.0F, 1.0F);
    config_.classCount = std::max(0, config_.classCount);
    config_.maxDetections = std::max(1, config_.maxDetections);
}

DetectionResults YoloPostprocessor::process(
    const YoloTensorOutput& output,
    const VideoFrameMetadata& metadata,
    const LetterboxTransform& transform) const
{
    if (output.values.empty() || output.shape.size() < 2
        || config_.classCount <= 0)
    {
        return {};
    }

    const std::int64_t firstDimension = output.shape[output.shape.size() - 2];
    const std::int64_t secondDimension = output.shape.back();
    if (firstDimension <= 0 || secondDimension <= 0)
    {
        return {};
    }

    int rows = static_cast<int>(firstDimension);
    int columns = static_cast<int>(secondDimension);
    bool attributesByCandidates = false;

    if (config_.outputLayout == YoloOutputLayout::AttributesByCandidates)
    {
        attributesByCandidates = true;
    }
    else if (config_.outputLayout == YoloOutputLayout::CandidatesByAttributes)
    {
        attributesByCandidates = false;
    }
    else if (isCandidateAttributeCount(rows, config_.classCount))
    {
        attributesByCandidates = true;
    }
    else if (isCandidateAttributeCount(columns, config_.classCount))
    {
        attributesByCandidates = false;
    }
    else
    {
        return {};
    }

    const int attributes = attributesByCandidates ? rows : columns;
    const int candidates = attributesByCandidates ? columns : rows;
    const std::size_t expectedValues =
        static_cast<std::size_t>(attributes) * candidates;
    if (attributes < config_.classCount + 4
        || output.values.size() < expectedValues)
    {
        return {};
    }

    std::vector<Candidate> candidatesWithScores;
    candidatesWithScores.reserve(static_cast<std::size_t>(candidates));

    for (int candidateIndex = 0; candidateIndex < candidates; ++candidateIndex)
    {
        const auto valueAt = [&](int attributeIndex) -> float {
            if (attributesByCandidates)
            {
                return output.values[
                    static_cast<std::size_t>(attributeIndex) * candidates
                        + candidateIndex];
            }

            return output.values[
                static_cast<std::size_t>(candidateIndex) * attributes
                    + attributeIndex];
        };

        const float centerX = valueAt(0);
        const float centerY = valueAt(1);
        const float width = valueAt(2);
        const float height = valueAt(3);
        if (!std::isfinite(centerX) || !std::isfinite(centerY)
            || !std::isfinite(width) || !std::isfinite(height)
            || width <= 0.0F || height <= 0.0F)
        {
            continue;
        }

        const bool hasObjectness = attributes == config_.classCount + 5;
        const float objectness = hasObjectness ? valueAt(4) : 1.0F;
        const int classOffset = hasObjectness ? 5 : 4;

        int bestClassId = -1;
        float bestClassScore = 0.0F;
        for (int classId = 0; classId < config_.classCount; ++classId)
        {
            const float classScore = valueAt(classOffset + classId);
            if (classScore > bestClassScore)
            {
                bestClassScore = classScore;
                bestClassId = classId;
            }
        }

        const float confidence = objectness * bestClassScore;
        if (bestClassId < 0 || confidence < config_.confidenceThreshold)
        {
            continue;
        }

        DetectionResult result;
        result.sourceId = metadata.sourceId;
        result.frameIndex = metadata.frameIndex;
        result.ptsMs = metadata.ptsMs;
        result.classId = bestClassId;
        result.className = bestClassId < static_cast<int>(config_.classNames.size())
            ? config_.classNames[static_cast<std::size_t>(bestClassId)]
            : "class_" + std::to_string(bestClassId);
        result.confidence = confidence;
        result.box = YoloPreprocessor::restoreBox(
            BoundingBox{
                centerX - width * 0.5F,
                centerY - height * 0.5F,
                width,
                height},
            transform);

        if (result.box.width > 0.0F && result.box.height > 0.0F)
        {
            candidatesWithScores.push_back(Candidate{std::move(result)});
        }
    }

    std::sort(
        candidatesWithScores.begin(),
        candidatesWithScores.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.result.confidence > rhs.result.confidence;
        });

    DetectionResults results;
    results.reserve(std::min(
        static_cast<std::size_t>(config_.maxDetections),
        candidatesWithScores.size()));

    for (const Candidate& candidate : candidatesWithScores)
    {
        bool suppressed = false;
        for (const DetectionResult& selected : results)
        {
            if (selected.classId == candidate.result.classId
                && intersectionOverUnion(selected.box, candidate.result.box)
                    > config_.nmsThreshold)
            {
                suppressed = true;
                break;
            }
        }

        if (!suppressed)
        {
            results.push_back(candidate.result);
            if (static_cast<int>(results.size()) >= config_.maxDetections)
            {
                break;
            }
        }
    }

    return results;
}

float YoloPostprocessor::intersectionOverUnion(
    const BoundingBox& lhs,
    const BoundingBox& rhs)
{
    const float left = std::max(lhs.x, rhs.x);
    const float top = std::max(lhs.y, rhs.y);
    const float right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
    const float bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
    const float intersectionWidth = std::max(0.0F, right - left);
    const float intersectionHeight = std::max(0.0F, bottom - top);
    const float intersectionArea = intersectionWidth * intersectionHeight;
    const float unionArea = lhs.width * lhs.height
        + rhs.width * rhs.height - intersectionArea;

    return unionArea > std::numeric_limits<float>::epsilon()
        ? intersectionArea / unionArea
        : 0.0F;
}

} // namespace ivp
