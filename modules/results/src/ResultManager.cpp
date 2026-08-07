#include "results/ResultManager.h"

#include <algorithm>
#include <utility>

namespace ivp
{

ResultManager::ResultManager(ResultManagerConfig config)
    : config_(config),
      mutex_(),
      records_(),
      latestFrameResults_(),
      summary_()
{
}

void ResultManager::configure(const ResultManagerConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    trimToCapacity();
}

void ResultManager::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
    latestFrameResults_.clear();
    summary_ = DetectionSummary();
}

void ResultManager::addFrameResults(
    const std::string& sourceId,
    std::int64_t frameIndex,
    std::int64_t ptsMs,
    const DetectionResults& results)
{
    std::lock_guard<std::mutex> lock(mutex_);

    ++summary_.processedFrames;
    summary_.latestFrameIndex = frameIndex;
    summary_.latestPtsMs = ptsMs;

    DetectionResults normalizedResults;
    normalizedResults.reserve(results.size());

    for (DetectionResult result : results)
    {
        if (result.sourceId.empty())
        {
            result.sourceId = sourceId;
        }

        normalizedResults.push_back(result);
        records_.push_back(result);
        ++summary_.totalObjects;
        ++summary_.classCounts[classKey(result)];
    }

    if (!normalizedResults.empty())
    {
        ++summary_.framesWithDetections;
    }

    latestFrameResults_ = std::move(normalizedResults);
    trimToCapacity();
}

DetectionResults ResultManager::latestFrameResults() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latestFrameResults_;
}

DetectionResults ResultManager::recentResults(std::size_t maxCount) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    const std::size_t count = std::min(maxCount, records_.size());
    DetectionResults results;
    results.reserve(count);

    const auto begin = records_.end() - static_cast<std::ptrdiff_t>(count);
    for (auto it = begin; it != records_.end(); ++it)
    {
        results.push_back(*it);
    }

    return results;
}

DetectionResults ResultManager::resultsForFrame(std::int64_t frameIndex) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    DetectionResults results;
    for (const DetectionResult& record : records_)
    {
        if (record.frameIndex == frameIndex)
        {
            results.push_back(record);
        }
    }

    return results;
}

DetectionSummary ResultManager::summary() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return summary_;
}

std::size_t ResultManager::storedResultCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

std::string ResultManager::classKey(const DetectionResult& result)
{
    if (!result.className.empty())
    {
        return result.className;
    }

    return "class_" + std::to_string(result.classId);
}

void ResultManager::trimToCapacity()
{
    while (records_.size() > config_.maxStoredResults)
    {
        records_.pop_front();
    }
}

} // namespace ivp
