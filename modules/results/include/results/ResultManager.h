#ifndef IVP_RESULTS_RESULTMANAGER_H
#define IVP_RESULTS_RESULTMANAGER_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "common/DetectionResult.h"

namespace ivp
{

struct ResultManagerConfig
{
    std::size_t maxStoredResults = 1000;
};

struct DetectionSummary
{
    std::int64_t processedFrames = 0;
    std::int64_t framesWithDetections = 0;
    std::int64_t totalObjects = 0;
    std::int64_t latestFrameIndex = -1;
    std::int64_t latestPtsMs = 0;
    std::map<std::string, std::int64_t> classCounts;
};

class ResultManager final
{
public:
    explicit ResultManager(ResultManagerConfig config = {});

    void configure(const ResultManagerConfig& config);
    void clear();

    void addFrameResults(
        const std::string& sourceId,
        std::int64_t frameIndex,
        std::int64_t ptsMs,
        const DetectionResults& results);

    DetectionResults latestFrameResults() const;
    DetectionResults recentResults(std::size_t maxCount) const;
    DetectionResults resultsForFrame(std::int64_t frameIndex) const;
    DetectionSummary summary() const;
    std::size_t storedResultCount() const;

private:
    static std::string classKey(const DetectionResult& result);
    void trimToCapacity();

    ResultManagerConfig config_;
    mutable std::mutex mutex_;
    std::deque<DetectionResult> records_;
    DetectionResults latestFrameResults_;
    DetectionSummary summary_;
};

} // namespace ivp

#endif // IVP_RESULTS_RESULTMANAGER_H
