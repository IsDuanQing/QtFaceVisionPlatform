#ifndef VIEWERSETTINGSSTORE_H
#define VIEWERSETTINGSSTORE_H

#include <QString>

#include "inference/IDetector.h"
#include "common/FaceFeature.h"
#include "network/DetectionDeliverySettings.h"
#include "tracking/FaceTracker.h"

namespace ivp::viewer
{

struct ViewerSettings
{
    ivp::DetectorConfig detectorConfig;
    ivp::FaceTrackerConfig faceTrackerConfig;
    ivp::FaceRecognitionConfig faceRecognitionConfig;
    ivp::DetectionDeliverySettings delivery;
};

// Persists viewer-owned settings without coupling the inference or playback modules
// to QSettings.
class ViewerSettingsStore final
{
public:
    explicit ViewerSettingsStore(const QString& filePath = QString());

    ViewerSettings load(const ViewerSettings& defaults) const;
    bool save(const ViewerSettings& settings) const;
    QString filePath() const;

private:
    QString resolvedFilePath() const;

    QString filePathOverride_;
};

} // namespace ivp::viewer

#endif // VIEWERSETTINGSSTORE_H
