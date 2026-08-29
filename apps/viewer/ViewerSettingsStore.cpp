#include "ViewerSettingsStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

namespace
{

constexpr int kSettingsVersion = 4;

constexpr int kMinInputSize = 32;
constexpr int kMaxInputSize = 4096;
constexpr int kMinMaxDetections = 1;
constexpr int kMaxMaxDetections = 10000;
constexpr int kMinEveryNFrames = 1;
constexpr int kMaxEveryNFrames = 1000;
constexpr int kDefaultNetworkPort = 9000;
constexpr int kMinNetworkPort = 1;
constexpr int kMaxNetworkPort = 65535;
constexpr float kMinTrackerCenterDistance = 0.0F;
constexpr float kMaxTrackerCenterDistance = 3.0F;
constexpr int kMinTrackerMissedUpdates = 0;
constexpr int kMaxTrackerMissedUpdates = 1000;
constexpr int kMinTrackerLostDurationMs = 0;
constexpr int kMaxTrackerLostDurationMs = 120000;

QString detectorGroupKey(const QString& key)
{
    return QStringLiteral("detector/") + key;
}

QString recognitionGroupKey(const QString& key)
{
    return QStringLiteral("recognition/") + key;
}

QString trackingGroupKey(const QString& key)
{
    return QStringLiteral("tracking/") + key;
}

std::string validOrDefaultPath(
    const QString& configuredPath,
    const std::string& defaultPath)
{
    const QString trimmedPath = configuredPath.trimmed();
    if (!trimmedPath.isEmpty() && QFileInfo(trimmedPath).exists())
    {
        return QFileInfo(trimmedPath).absoluteFilePath().toStdString();
    }

    return defaultPath;
}

bool isLegacyDefectModel(const QString& path)
{
    return QFileInfo(path).fileName().compare(
               QStringLiteral("defect.onnx"),
               Qt::CaseInsensitive)
        == 0
        || path.contains(QStringLiteral("yolo11l"), Qt::CaseInsensitive);
}

bool isBundledFaceModel(const QString& path)
{
    return QFileInfo(path).fileName().compare(
               QStringLiteral("face.onnx"),
               Qt::CaseInsensitive)
        == 0;
}

} // namespace

namespace ivp::viewer
{

ViewerSettingsStore::ViewerSettingsStore(const QString& filePath)
    : filePathOverride_(filePath)
{
}

QString ViewerSettingsStore::resolvedFilePath() const
{
    if (!filePathOverride_.isEmpty())
    {
        return QFileInfo(filePathOverride_).absoluteFilePath();
    }

    QString baseDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDirectory.isEmpty() && QCoreApplication::instance() != nullptr)
    {
        baseDirectory = QCoreApplication::applicationDirPath();
    }
    if (baseDirectory.isEmpty())
    {
        baseDirectory = QDir::currentPath();
    }

    return QDir(baseDirectory).filePath(QStringLiteral("settings.ini"));
}

QString ViewerSettingsStore::filePath() const
{
    return resolvedFilePath();
}

ViewerSettings ViewerSettingsStore::load(const ViewerSettings& defaults) const
{
    ViewerSettings settings = defaults;
    const QString settingsPath = resolvedFilePath();
    if (settingsPath.isEmpty())
    {
        return settings;
    }

    QSettings storage(settingsPath, QSettings::IniFormat);
    settings.detectorConfig.confidenceThreshold = qBound(
        0.0F,
        storage.value(
            detectorGroupKey(QStringLiteral("confidenceThreshold")),
            defaults.detectorConfig.confidenceThreshold).toFloat(),
        1.0F);
    settings.detectorConfig.nmsThreshold = qBound(
        0.0F,
        storage.value(
            detectorGroupKey(QStringLiteral("nmsThreshold")),
            defaults.detectorConfig.nmsThreshold).toFloat(),
        1.0F);
    settings.detectorConfig.maxDetections = qBound(
        kMinMaxDetections,
        storage.value(
            detectorGroupKey(QStringLiteral("maxDetections")),
            defaults.detectorConfig.maxDetections).toInt(),
        kMaxMaxDetections);
    settings.detectorConfig.inputWidth = qBound(
        kMinInputSize,
        storage.value(
            detectorGroupKey(QStringLiteral("inputWidth")),
            defaults.detectorConfig.inputWidth).toInt(),
        kMaxInputSize);
    settings.detectorConfig.inputHeight = qBound(
        kMinInputSize,
        storage.value(
            detectorGroupKey(QStringLiteral("inputHeight")),
            defaults.detectorConfig.inputHeight).toInt(),
        kMaxInputSize);
    settings.detectorConfig.classCount = qBound(
        0,
        storage.value(
            detectorGroupKey(QStringLiteral("classCount")),
            defaults.detectorConfig.classCount).toInt(),
        10000);
    settings.detectorConfig.detectEveryNFrames = qBound(
        kMinEveryNFrames,
        storage.value(
            detectorGroupKey(QStringLiteral("detectEveryNFrames")),
            defaults.detectorConfig.detectEveryNFrames).toInt(),
        kMaxEveryNFrames);
    const QString storedOnnxPath = storage.value(
        detectorGroupKey(QStringLiteral("onnxPath")),
        QString::fromStdString(defaults.detectorConfig.onnxPath)).toString();
    const QString storedLabelsPath = storage.value(
        detectorGroupKey(QStringLiteral("labelsPath")),
        QString::fromStdString(defaults.detectorConfig.labelsPath)).toString();
    const bool migrateLegacyModel = isLegacyDefectModel(storedOnnxPath)
        || (storedOnnxPath.isEmpty()
            && storedLabelsPath.contains(QStringLiteral("yolo11l"),
                                         Qt::CaseInsensitive));
    settings.detectorConfig.onnxPath = migrateLegacyModel
        ? defaults.detectorConfig.onnxPath
        : validOrDefaultPath(storedOnnxPath, defaults.detectorConfig.onnxPath);
    settings.detectorConfig.labelsPath = migrateLegacyModel
        ? defaults.detectorConfig.labelsPath
        : validOrDefaultPath(storedLabelsPath, defaults.detectorConfig.labelsPath);

    // The bundled face model is exported with a fixed 640x640 input and one
    // class. Correct stale settings before they reach OpenCV DNN.
    if (isBundledFaceModel(
            QString::fromStdString(settings.detectorConfig.onnxPath)))
    {
        settings.detectorConfig.inputWidth = 640;
        settings.detectorConfig.inputHeight = 640;
        settings.detectorConfig.classCount = 1;
    }

    settings.faceTrackerConfig.minIntersectionOverUnion = qBound(
        0.0F,
        storage.value(
            trackingGroupKey(QStringLiteral("minIntersectionOverUnion")),
            defaults.faceTrackerConfig.minIntersectionOverUnion).toFloat(),
        1.0F);
    settings.faceTrackerConfig.maxCenterDistanceRatio = qBound(
        kMinTrackerCenterDistance,
        storage.value(
            trackingGroupKey(QStringLiteral("maxCenterDistanceRatio")),
            defaults.faceTrackerConfig.maxCenterDistanceRatio).toFloat(),
        kMaxTrackerCenterDistance);
    settings.faceTrackerConfig.maxMissedUpdates = qBound(
        kMinTrackerMissedUpdates,
        storage.value(
            trackingGroupKey(QStringLiteral("maxMissedUpdates")),
            defaults.faceTrackerConfig.maxMissedUpdates).toInt(),
        kMaxTrackerMissedUpdates);
    settings.faceTrackerConfig.maxLostDurationMs = qBound(
        kMinTrackerLostDurationMs,
        storage.value(
            trackingGroupKey(QStringLiteral("maxLostDurationMs")),
            defaults.faceTrackerConfig.maxLostDurationMs).toInt(),
        kMaxTrackerLostDurationMs);

    settings.faceRecognitionConfig.enabled = storage.value(
        recognitionGroupKey(QStringLiteral("enabled")),
        defaults.faceRecognitionConfig.enabled).toBool();
    const QString storedFeatureModelPath = storage.value(
        recognitionGroupKey(QStringLiteral("featureModelPath")),
        QString::fromStdString(defaults.faceRecognitionConfig.featureModelPath)).toString();
    settings.faceRecognitionConfig.featureModelPath = validOrDefaultPath(
        storedFeatureModelPath,
        defaults.faceRecognitionConfig.featureModelPath);
    settings.faceRecognitionConfig.similarityThreshold = qBound(
        0.0F,
        storage.value(
            recognitionGroupKey(QStringLiteral("similarityThreshold")),
            defaults.faceRecognitionConfig.similarityThreshold).toFloat(),
        1.0F);
    settings.faceRecognitionConfig.minSimilarityMargin = qBound(
        0.0F,
        storage.value(
            recognitionGroupKey(QStringLiteral("minSimilarityMargin")),
            defaults.faceRecognitionConfig.minSimilarityMargin).toFloat(),
        1.0F);
    settings.faceRecognitionConfig.minFaceSizePixels = qBound(
        1,
        storage.value(
            recognitionGroupKey(QStringLiteral("minFaceSizePixels")),
            defaults.faceRecognitionConfig.minFaceSizePixels).toInt(),
        4096);
    settings.faceRecognitionConfig.normalizedWidth = qBound(
        1,
        storage.value(
            recognitionGroupKey(QStringLiteral("normalizedWidth")),
            defaults.faceRecognitionConfig.normalizedWidth).toInt(),
        4096);
    settings.faceRecognitionConfig.normalizedHeight = qBound(
        1,
        storage.value(
            recognitionGroupKey(QStringLiteral("normalizedHeight")),
            defaults.faceRecognitionConfig.normalizedHeight).toInt(),
        4096);
    settings.faceRecognitionConfig.facePaddingRatio = qBound(
        0.0F,
        storage.value(
            recognitionGroupKey(QStringLiteral("facePaddingRatio")),
            defaults.faceRecognitionConfig.facePaddingRatio).toFloat(),
        1.0F);

    settings.delivery.exportEnabled = storage.value(
        QStringLiteral("delivery/exportEnabled"),
        defaults.delivery.exportEnabled).toBool();
    settings.delivery.exportDirectory = storage.value(
        QStringLiteral("delivery/exportDirectory"),
        defaults.delivery.exportDirectory).toString();
    const int exportFormatValue = storage.value(
        QStringLiteral("delivery/exportFormat"),
        static_cast<int>(defaults.delivery.exportFormat)).toInt();
    settings.delivery.exportFormat =
        exportFormatValue == static_cast<int>(ivp::ResultExportFormat::Csv)
        ? ivp::ResultExportFormat::Csv
        : ivp::ResultExportFormat::JsonLines;
    settings.delivery.includeEmptyFrames = storage.value(
        QStringLiteral("delivery/includeEmptyFrames"),
        defaults.delivery.includeEmptyFrames).toBool();
    settings.delivery.networkEnabled = storage.value(
        QStringLiteral("delivery/networkEnabled"),
        defaults.delivery.networkEnabled).toBool();
    settings.delivery.networkHost = storage.value(
        QStringLiteral("delivery/networkHost"),
        defaults.delivery.networkHost).toString().trimmed();
    if (settings.delivery.networkHost.isEmpty())
    {
        settings.delivery.networkHost = defaults.delivery.networkHost;
    }
    settings.delivery.networkPort = qBound(
        kMinNetworkPort,
        storage.value(
            QStringLiteral("delivery/networkPort"),
            defaults.delivery.networkPort > 0
                ? defaults.delivery.networkPort
                : kDefaultNetworkPort).toInt(),
        kMaxNetworkPort);

    if (storage.status() != QSettings::NoError)
    {
        qWarning() << "Could not read viewer settings:" << settingsPath
                   << "status:" << storage.status();
        return defaults;
    }

    return settings;
}

bool ViewerSettingsStore::save(const ViewerSettings& settings) const
{
    const QString settingsPath = resolvedFilePath();
    if (settingsPath.isEmpty())
    {
        return false;
    }

    const QFileInfo settingsInfo(settingsPath);
    if (!QDir().mkpath(settingsInfo.absolutePath()))
    {
        qWarning() << "Could not create viewer settings directory:"
                   << settingsInfo.absolutePath();
        return false;
    }

    QSettings storage(settingsPath, QSettings::IniFormat);
    storage.setValue(QStringLiteral("schemaVersion"), kSettingsVersion);
    storage.setValue(
        detectorGroupKey(QStringLiteral("confidenceThreshold")),
        settings.detectorConfig.confidenceThreshold);
    storage.setValue(
        detectorGroupKey(QStringLiteral("nmsThreshold")),
        settings.detectorConfig.nmsThreshold);
    storage.setValue(
        detectorGroupKey(QStringLiteral("maxDetections")),
        settings.detectorConfig.maxDetections);
    storage.setValue(
        detectorGroupKey(QStringLiteral("inputWidth")),
        settings.detectorConfig.inputWidth);
    storage.setValue(
        detectorGroupKey(QStringLiteral("inputHeight")),
        settings.detectorConfig.inputHeight);
    storage.setValue(
        detectorGroupKey(QStringLiteral("classCount")),
        settings.detectorConfig.classCount);
    storage.setValue(
        detectorGroupKey(QStringLiteral("detectEveryNFrames")),
        settings.detectorConfig.detectEveryNFrames);
    storage.setValue(
        detectorGroupKey(QStringLiteral("onnxPath")),
        QString::fromStdString(settings.detectorConfig.onnxPath));
    storage.setValue(
        detectorGroupKey(QStringLiteral("labelsPath")),
        QString::fromStdString(settings.detectorConfig.labelsPath));
    storage.setValue(
        trackingGroupKey(QStringLiteral("minIntersectionOverUnion")),
        settings.faceTrackerConfig.minIntersectionOverUnion);
    storage.setValue(
        trackingGroupKey(QStringLiteral("maxCenterDistanceRatio")),
        settings.faceTrackerConfig.maxCenterDistanceRatio);
    storage.setValue(
        trackingGroupKey(QStringLiteral("maxMissedUpdates")),
        settings.faceTrackerConfig.maxMissedUpdates);
    storage.setValue(
        trackingGroupKey(QStringLiteral("maxLostDurationMs")),
        settings.faceTrackerConfig.maxLostDurationMs);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("enabled")),
        settings.faceRecognitionConfig.enabled);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("featureModelPath")),
        QString::fromStdString(settings.faceRecognitionConfig.featureModelPath));
    storage.setValue(
        recognitionGroupKey(QStringLiteral("similarityThreshold")),
        settings.faceRecognitionConfig.similarityThreshold);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("minSimilarityMargin")),
        settings.faceRecognitionConfig.minSimilarityMargin);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("minFaceSizePixels")),
        settings.faceRecognitionConfig.minFaceSizePixels);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("normalizedWidth")),
        settings.faceRecognitionConfig.normalizedWidth);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("normalizedHeight")),
        settings.faceRecognitionConfig.normalizedHeight);
    storage.setValue(
        recognitionGroupKey(QStringLiteral("facePaddingRatio")),
        settings.faceRecognitionConfig.facePaddingRatio);
    storage.setValue(
        QStringLiteral("delivery/exportEnabled"),
        settings.delivery.exportEnabled);
    storage.setValue(
        QStringLiteral("delivery/exportDirectory"),
        settings.delivery.exportDirectory);
    storage.setValue(
        QStringLiteral("delivery/exportFormat"),
        static_cast<int>(settings.delivery.exportFormat));
    storage.setValue(
        QStringLiteral("delivery/includeEmptyFrames"),
        settings.delivery.includeEmptyFrames);
    storage.setValue(
        QStringLiteral("delivery/networkEnabled"),
        settings.delivery.networkEnabled);
    storage.setValue(
        QStringLiteral("delivery/networkHost"),
        settings.delivery.networkHost);
    storage.setValue(
        QStringLiteral("delivery/networkPort"),
        settings.delivery.networkPort);
    storage.sync();

    if (storage.status() != QSettings::NoError)
    {
        qWarning() << "Could not write viewer settings:" << settingsPath
                   << "status:" << storage.status();
        return false;
    }

    return true;
}

} // namespace ivp::viewer
