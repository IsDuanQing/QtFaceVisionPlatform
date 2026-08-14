#ifndef IVP_CONTROL_DETECTIONCONTROLSERVER_H
#define IVP_CONTROL_DETECTIONCONTROLSERVER_H

#include <memory>

#include <QObject>
#include <QString>

#include "control/DetectionControlProtocol.h"
#include "network/DetectionFramePacket.h"

namespace ivp
{

class DetectionControlServer final : public QObject
{
    Q_OBJECT

public:
    explicit DetectionControlServer(QObject* parent = nullptr);
    ~DetectionControlServer() override;

    DetectionControlServer(const DetectionControlServer&) = delete;
    DetectionControlServer& operator=(const DetectionControlServer&) = delete;

    bool start(const DetectionControlServerSettings& settings);
    void stop();

    bool isRunning() const;
    int connectedClientCount() const;
    DetectionControlServerSettings settings() const;

    void setStatusSnapshot(const DetectionControlStatus& status);
    DetectionControlStatus statusSnapshot() const;
    void publishStatusSnapshot();

    bool publishDetectionPacket(const DetectionFramePacket& packet);
    QString lastError() const;

signals:
    void startRequested();
    void stopRequested();
    void taskConfigRequested(const ivp::DetectionTaskConfig& config);
    void clientCountChanged(int count);
    void errorOccurred(const QString& message);
    void runningChanged(bool running);

private:
    void notifyStartRequested();
    void notifyStopRequested();
    void notifyTaskConfigRequested(const ivp::DetectionTaskConfig& config);
    void notifyClientCountChanged(int count);
    void notifyError(const QString& message);
    void notifyRunningChanged(bool running);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ivp

#endif // IVP_CONTROL_DETECTIONCONTROLSERVER_H
