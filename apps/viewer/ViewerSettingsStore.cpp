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

constexpr int kSettingsVersion = 1;

constexpr int kMinInputSize = 32;
constexpr int kMaxInputSize = 4096;
constexpr int kMinMaxDetections = 1;
constexpr int kMaxMaxDetections = 10000;
constexpr int kMinEveryNFrames = 1;
constexpr int kMaxEveryNFrames = 1000;
constexpr int kMinMockDelayMs = 0;
constexpr int kMaxMockDelayMs = 1000;
constexpr double kMinImageSequenceFps = 1.0;
constexpr double kMaxImageSequenceFps = 120.0;
constexpr int kDefaultNetworkPort = 9000;
constexpr int kMinNetworkPort = 1;
constexpr int kMaxNetworkPort = 65535;

QString detectorGroupKey(const QString& key)
{
    return QStringLiteral("detector/") + key;
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
    const int backendValue = storage.value(
        detectorGroupKey(QStringLiteral("backend")),
        static_cast<int>(defaults.detectorConfig.backend)).toInt();
    if (backendValue == static_cast<int>(ivp::DetectorBackend::TensorRT))
    {
        settings.detectorConfig.backend = ivp::DetectorBackend::TensorRT;
    }
    else if (backendValue == static_cast<int>(ivp::DetectorBackend::OpenCVDnn))
    {
        settings.detectorConfig.backend = ivp::DetectorBackend::OpenCVDnn;
    }
    else
    {
        settings.detectorConfig.backend = ivp::DetectorBackend::Mock;
    }

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
    settings.detectorConfig.simulatedDelayMs = qBound(
        kMinMockDelayMs,
        storage.value(
            detectorGroupKey(QStringLiteral("simulatedDelayMs")),
            defaults.detectorConfig.simulatedDelayMs).toInt(),
        kMaxMockDelayMs);
    settings.detectorConfig.onnxPath = validOrDefaultPath(
        storage.value(
            detectorGroupKey(QStringLiteral("onnxPath")),
            QString::fromStdString(defaults.detectorConfig.onnxPath)).toString(),
        defaults.detectorConfig.onnxPath);
    settings.detectorConfig.enginePath = validOrDefaultPath(
        storage.value(
            detectorGroupKey(QStringLiteral("enginePath")),
            QString::fromStdString(defaults.detectorConfig.enginePath)).toString(),
        defaults.detectorConfig.enginePath);
    settings.detectorConfig.labelsPath = validOrDefaultPath(
        storage.value(
            detectorGroupKey(QStringLiteral("labelsPath")),
            QString::fromStdString(defaults.detectorConfig.labelsPath)).toString(),
        defaults.detectorConfig.labelsPath);
    settings.imageSequenceFps = qBound(
        kMinImageSequenceFps,
        storage.value(
            QStringLiteral("imageSequenceFps"),
            defaults.imageSequenceFps).toDouble(),
        kMaxImageSequenceFps);
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
        detectorGroupKey(QStringLiteral("backend")),
        static_cast<int>(settings.detectorConfig.backend));
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
        detectorGroupKey(QStringLiteral("simulatedDelayMs")),
        settings.detectorConfig.simulatedDelayMs);
    storage.setValue(
        detectorGroupKey(QStringLiteral("onnxPath")),
        QString::fromStdString(settings.detectorConfig.onnxPath));
    storage.setValue(
        detectorGroupKey(QStringLiteral("enginePath")),
        QString::fromStdString(settings.detectorConfig.enginePath));
    storage.setValue(
        detectorGroupKey(QStringLiteral("labelsPath")),
        QString::fromStdString(settings.detectorConfig.labelsPath));
    storage.setValue(
        QStringLiteral("imageSequenceFps"),
        settings.imageSequenceFps);
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
