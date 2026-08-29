#include "control/DetectionControlServer.h"
#include "common/DetectionResult.h"
#include "network/DetectionFramePacket.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <cassert>
#include <functional>
#include <iostream>

namespace
{

bool waitForCondition(QCoreApplication& app, const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        if (condition())
        {
            return true;
        }
    }
    return false;
}

class LineReader
{
public:
    explicit LineReader(QTcpSocket& socket)
        : socket_(socket)
    {
    }

    QJsonObject readJsonLine(QCoreApplication& app)
    {
        if (!waitForCondition(app, [&]() {
                buffer_.append(socket_.readAll());
                return buffer_.contains('\n');
            }))
        {
            return {};
        }

        const int newline = buffer_.indexOf('\n');
        QByteArray line = buffer_.left(newline);
        buffer_.remove(0, newline + 1);
        if (line.endsWith('\r'))
        {
            line.chop(1);
        }

        const QJsonDocument document = QJsonDocument::fromJson(line);
        return document.isObject() ? document.object() : QJsonObject();
    }

private:
    QTcpSocket& socket_;
    QByteArray buffer_;
};

void writeCommand(QTcpSocket& socket, const QJsonObject& command)
{
    QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket.write(payload);
    socket.flush();
}

void writeCommand(QTcpSocket& socket, const char* type)
{
    QJsonObject command;
    command.insert(QStringLiteral("type"), QString::fromLatin1(type));
    writeCommand(socket, command);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    ivp::DetectionControlServer server;
    ivp::DetectionControlServerSettings settings;
    settings.listenAddress = QStringLiteral("127.0.0.1");
    settings.listenPort = 19100;
    assert(server.start(settings));

    bool startRequested = false;
    bool stopRequested = false;
    bool taskConfigRequested = false;
    ivp::DetectionTaskConfig taskConfig;
    QObject::connect(
        &server,
        &ivp::DetectionControlServer::startRequested,
        [&]() { startRequested = true; });
    QObject::connect(
        &server,
        &ivp::DetectionControlServer::stopRequested,
        [&]() { stopRequested = true; });
    QObject::connect(
        &server,
        &ivp::DetectionControlServer::taskConfigRequested,
        [&](const ivp::DetectionTaskConfig& config) {
            taskConfigRequested = true;
            taskConfig = config;
        });

    // 两个客户端同时连接，验证控制服务端的多客户端广播能力。
    QTcpSocket socketA;
    socketA.connectToHost(settings.listenAddress, settings.listenPort);
    assert(socketA.waitForConnected(2000));
    LineReader readerA(socketA);

    QTcpSocket socketB;
    socketB.connectToHost(settings.listenAddress, settings.listenPort);
    assert(socketB.waitForConnected(2000));
    LineReader readerB(socketB);

    const QJsonObject welcomeA = readerA.readJsonLine(app);
    const QJsonObject initialStatusA = readerA.readJsonLine(app);
    const QJsonObject welcomeB = readerB.readJsonLine(app);
    const QJsonObject initialStatusB = readerB.readJsonLine(app);
    assert(welcomeA.value(QStringLiteral("type")).toString() == QStringLiteral("welcome"));
    assert(initialStatusA.value(QStringLiteral("type")).toString() == QStringLiteral("status"));
    assert(welcomeB.value(QStringLiteral("type")).toString() == QStringLiteral("welcome"));
    assert(initialStatusB.value(QStringLiteral("type")).toString() == QStringLiteral("status"));

    writeCommand(socketA, "status");
    const QJsonObject status = readerA.readJsonLine(app);
    assert(status.value(QStringLiteral("type")).toString() == QStringLiteral("status"));

    writeCommand(socketA, "start");
    const QJsonObject start = readerA.readJsonLine(app);
    assert(start.value(QStringLiteral("type")).toString() == QStringLiteral("start"));
    assert(waitForCondition(app, [&]() { return startRequested; }));

    writeCommand(socketA, "stop");
    const QJsonObject stop = readerA.readJsonLine(app);
    assert(stop.value(QStringLiteral("type")).toString() == QStringLiteral("stop"));
    assert(waitForCondition(app, [&]() { return stopRequested; }));

    QJsonObject configureTask;
    configureTask.insert(QStringLiteral("type"), QStringLiteral("configure_task"));
    configureTask.insert(QStringLiteral("request_id"), QStringLiteral("req-001"));
    configureTask.insert(QStringLiteral("task_id"), QStringLiteral("task-001"));
    configureTask.insert(QStringLiteral("source_type"), QStringLiteral("rtsp"));
    configureTask.insert(QStringLiteral("source_url"), QStringLiteral("rtsp://127.0.0.1:8554/test"));
    configureTask.insert(QStringLiteral("auto_start"), false);
    configureTask.insert(QStringLiteral("production_line_id"), QStringLiteral("line-a"));
    configureTask.insert(QStringLiteral("batch_id"), QStringLiteral("batch-20260812"));
    configureTask.insert(QStringLiteral("confidence_threshold"), 0.62);
    configureTask.insert(QStringLiteral("detect_every_n_frames"), 3);
    writeCommand(socketA, configureTask);
    const QJsonObject taskReply = readerA.readJsonLine(app);
    assert(taskReply.value(QStringLiteral("type")).toString() == QStringLiteral("configure_task"));
    assert(taskReply.value(QStringLiteral("request_id")).toString() == QStringLiteral("req-001"));
    assert(waitForCondition(app, [&]() { return taskConfigRequested; }));
    assert(taskConfig.taskId.has_value());
    assert(*taskConfig.taskId == QStringLiteral("task-001"));
    assert(taskConfig.sourceType.has_value());
    assert(*taskConfig.sourceType == QStringLiteral("rtsp"));
    assert(taskConfig.autoStart.has_value());
    assert(!*taskConfig.autoStart);
    assert(taskConfig.confidenceThreshold.has_value());
    assert(*taskConfig.confidenceThreshold > 0.61F);

    QJsonObject invalidTask;
    invalidTask.insert(QStringLiteral("type"), QStringLiteral("configure_task"));
    invalidTask.insert(QStringLiteral("request_id"), QStringLiteral("req-bad-source"));
    invalidTask.insert(QStringLiteral("source_type"), QStringLiteral("rtsp"));
    writeCommand(socketA, invalidTask);
    const QJsonObject invalidTaskReply = readerA.readJsonLine(app);
    assert(invalidTaskReply.value(QStringLiteral("type")).toString() == QStringLiteral("error"));
    assert(invalidTaskReply.value(QStringLiteral("code")).toString()
           == QStringLiteral("invalid_task_config"));
    assert(invalidTaskReply.value(QStringLiteral("request_id")).toString()
           == QStringLiteral("req-bad-source"));

    ivp::DetectionFramePacket packet;
    packet.taskId = "task-001";
    packet.productionLineId = "line-a";
    packet.batchId = "batch-20260812";
    packet.sourceId = "rtsp://127.0.0.1:8554/test";
    packet.frameIndex = 42;
    packet.ptsMs = 1333;
    packet.recordedAtMs = 1700000000000LL;

    ivp::DetectionResult result;
    result.sourceId = packet.sourceId;
    result.frameIndex = packet.frameIndex;
    result.ptsMs = packet.ptsMs;
    result.trackId = 81;
    result.classId = 7;
    result.className = "scratch";
    result.confidence = 0.93F;
    result.box.x = 10.0F;
    result.box.y = 20.0F;
    result.box.width = 30.0F;
    result.box.height = 40.0F;
    packet.results.push_back(result);

    assert(server.publishDetectionPacket(packet));

    const QJsonObject detectionA = readerA.readJsonLine(app);
    const QJsonObject detectionB = readerB.readJsonLine(app);
    assert(detectionA.value(QStringLiteral("type")).toString() == QStringLiteral("detection"));
    assert(detectionB.value(QStringLiteral("type")).toString() == QStringLiteral("detection"));
    assert(detectionA.value(QStringLiteral("detection_count")).toInt() == 1);
    assert(detectionB.value(QStringLiteral("detection_count")).toInt() == 1);

    const QJsonArray detections = detectionA.value(QStringLiteral("detections")).toArray();
    assert(detections.size() == 1);
    const QJsonObject firstDetection = detections.at(0).toObject();
    assert(firstDetection.value(QStringLiteral("class_name")).toString() == QStringLiteral("scratch"));
    assert(firstDetection.value(QStringLiteral("track_id")).toInt() == 81);
    assert(firstDetection.value(QStringLiteral("confidence")).toDouble() > 0.92);
    const QJsonObject box = firstDetection.value(QStringLiteral("box")).toObject();
    assert(box.value(QStringLiteral("width")).toDouble() == 30.0);

    ivp::DetectionControlStatus broadcastStatus;
    broadcastStatus.taskId = QStringLiteral("task-001");
    broadcastStatus.productionLineId = QStringLiteral("line-a");
    broadcastStatus.batchId = QStringLiteral("batch-20260812");
    broadcastStatus.opened = true;
    broadcastStatus.playing = true;
    broadcastStatus.sourceId = QString::fromLatin1(packet.sourceId.c_str());
    broadcastStatus.frameIndex = 42;
    broadcastStatus.ptsMs = 1333;
    broadcastStatus.processedFrames = 99;
    broadcastStatus.framesWithDetections = 17;
    broadcastStatus.totalObjects = 23;
    broadcastStatus.videoWidth = 1920;
    broadcastStatus.videoHeight = 1080;
    broadcastStatus.videoFps = 25.0;
    broadcastStatus.durationMs = 60000;
    broadcastStatus.runtime.state = ivp::RuntimeState::Running;
    broadcastStatus.runtime.metrics.decodedFrames = 120;
    broadcastStatus.runtime.metrics.displayedFrames = 118;
    broadcastStatus.runtime.metrics.inferredFrames = 100;
    broadcastStatus.runtime.metrics.droppedDisplayFrames = 2;
    broadcastStatus.runtime.metrics.droppedInferenceFrames = 20;
    broadcastStatus.runtime.metrics.decodeFps = 25.0;
    broadcastStatus.runtime.metrics.displayFps = 24.6;
    broadcastStatus.runtime.metrics.inferenceFps = 20.8;
    broadcastStatus.runtime.metrics.displayQueueSize = 1;
    broadcastStatus.runtime.metrics.inferenceQueueSize = 2;
    broadcastStatus.runtime.metrics.currentFrameIndex = 118;
    broadcastStatus.runtime.metrics.currentPtsMs = 4720;
    broadcastStatus.runtime.metrics.lastInferenceLatencyMs = 18;
    broadcastStatus.message = QStringLiteral("Broadcast ready");
    server.setStatusSnapshot(broadcastStatus);
    server.publishStatusSnapshot();

    const QJsonObject broadcastStatusA = readerA.readJsonLine(app);
    const QJsonObject broadcastStatusB = readerB.readJsonLine(app);
    assert(broadcastStatusA.value(QStringLiteral("type")).toString() == QStringLiteral("status"));
    assert(broadcastStatusB.value(QStringLiteral("type")).toString() == QStringLiteral("status"));
    assert(broadcastStatusA.value(QStringLiteral("connected_clients")).toInt() == 2);
    assert(broadcastStatusB.value(QStringLiteral("connected_clients")).toInt() == 2);
    assert(broadcastStatusA.value(QStringLiteral("message")).toString() == QStringLiteral("Broadcast ready"));
    assert(broadcastStatusA.value(QStringLiteral("runtime_state")).toString() == QStringLiteral("Running"));
    assert(broadcastStatusA.value(QStringLiteral("decoded_frames")).toInt() == 120);
    assert(broadcastStatusA.value(QStringLiteral("displayed_frames")).toInt() == 118);
    assert(broadcastStatusA.value(QStringLiteral("dropped_inference_frames")).toInt() == 20);
    assert(broadcastStatusA.value(QStringLiteral("current_frame_index")).toInt() == 118);

    server.stop();
    std::cout << "Detection control server smoke test passed.\n";
    return 0;
}
