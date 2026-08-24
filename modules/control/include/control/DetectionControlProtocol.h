#ifndef IVP_CONTROL_DETECTIONCONTROLPROTOCOL_H
#define IVP_CONTROL_DETECTIONCONTROLPROTOCOL_H

#include <optional>

#include <QMetaType>
#include <QtGlobal>

#include <QString>

#include "common/RuntimeStatus.h"

namespace ivp
{

struct DetectionControlServerSettings
{
    QString listenAddress = QStringLiteral("127.0.0.1");
    quint16 listenPort = 9100;
    int backlog = 32;
};

struct DetectionControlStatus
{
    bool serviceRunning = false;
    QString listenAddress = QStringLiteral("127.0.0.1");
    quint16 listenPort = 9100;
    int connectedClients = 0;
    QString taskId;
    QString productionLineId;
    QString batchId;
    bool opened = false;
    bool playing = false;
    QString sourceId;
    qint64 frameIndex = 0;
    qint64 ptsMs = 0;
    qint64 processedFrames = 0;
    qint64 framesWithDetections = 0;
    qint64 totalObjects = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    double videoFps = 0.0;
    qint64 durationMs = 0;
    RuntimeStatus runtime;
    QString message;
};

// A partial remote task update parsed from the control protocol.
// Fields are optional on purpose: remote clients may override only the
// parameters they need, while the Qt UI keeps providing defaults.
struct DetectionTaskConfig
{
    // Optional client-generated identifier used to correlate the protocol ACK
    // with the original remote task request.
    std::optional<QString> requestId;
    std::optional<QString> taskId;
    std::optional<QString> sourceType;
    std::optional<QString> sourceUrl;
    std::optional<bool> autoStart;
    std::optional<QString> productionLineId;
    std::optional<QString> batchId;

    std::optional<float> confidenceThreshold;
    std::optional<float> nmsThreshold;
    std::optional<int> detectEveryNFrames;
    std::optional<int> inputWidth;
    std::optional<int> inputHeight;
    std::optional<int> classCount;
    std::optional<int> maxDetections;
    std::optional<QString> onnxPath;
    std::optional<QString> labelsPath;
};

} // namespace ivp

Q_DECLARE_METATYPE(ivp::DetectionTaskConfig)

#endif // IVP_CONTROL_DETECTIONCONTROLPROTOCOL_H
