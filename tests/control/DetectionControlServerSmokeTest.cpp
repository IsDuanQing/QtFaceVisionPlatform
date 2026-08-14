#include "control/DetectionControlServer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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

    QTcpSocket socket;
    socket.connectToHost(settings.listenAddress, settings.listenPort);
    assert(socket.waitForConnected(2000));
    LineReader reader(socket);

    const QJsonObject welcome = reader.readJsonLine(app);
    assert(welcome.value(QStringLiteral("type")).toString() == QStringLiteral("welcome"));
    const QJsonObject initialStatus = reader.readJsonLine(app);
    assert(initialStatus.value(QStringLiteral("type")).toString() == QStringLiteral("status"));

    writeCommand(socket, "status");
    const QJsonObject status = reader.readJsonLine(app);
    assert(status.value(QStringLiteral("type")).toString() == QStringLiteral("status"));

    writeCommand(socket, "start");
    const QJsonObject start = reader.readJsonLine(app);
    assert(start.value(QStringLiteral("type")).toString() == QStringLiteral("start"));
    assert(waitForCondition(app, [&]() { return startRequested; }));

    writeCommand(socket, "stop");
    const QJsonObject stop = reader.readJsonLine(app);
    assert(stop.value(QStringLiteral("type")).toString() == QStringLiteral("stop"));
    assert(waitForCondition(app, [&]() { return stopRequested; }));

    QJsonObject configureTask;
    configureTask.insert(QStringLiteral("type"), QStringLiteral("configure_task"));
    configureTask.insert(QStringLiteral("task_id"), QStringLiteral("task-001"));
    configureTask.insert(QStringLiteral("source_type"), QStringLiteral("rtsp"));
    configureTask.insert(QStringLiteral("source_url"), QStringLiteral("rtsp://127.0.0.1:8554/test"));
    configureTask.insert(QStringLiteral("auto_start"), false);
    configureTask.insert(QStringLiteral("production_line_id"), QStringLiteral("line-a"));
    configureTask.insert(QStringLiteral("batch_id"), QStringLiteral("batch-20260812"));
    configureTask.insert(QStringLiteral("detector_backend"), QStringLiteral("opencv"));
    configureTask.insert(QStringLiteral("confidence_threshold"), 0.62);
    configureTask.insert(QStringLiteral("detect_every_n_frames"), 3);
    writeCommand(socket, configureTask);
    const QJsonObject taskReply = reader.readJsonLine(app);
    assert(taskReply.value(QStringLiteral("type")).toString() == QStringLiteral("configure_task"));
    assert(waitForCondition(app, [&]() { return taskConfigRequested; }));
    assert(taskConfig.taskId.has_value());
    assert(*taskConfig.taskId == QStringLiteral("task-001"));
    assert(taskConfig.sourceType.has_value());
    assert(*taskConfig.sourceType == QStringLiteral("rtsp"));
    assert(taskConfig.autoStart.has_value());
    assert(!*taskConfig.autoStart);
    assert(taskConfig.confidenceThreshold.has_value());
    assert(*taskConfig.confidenceThreshold > 0.61F);
    assert(taskConfig.detectorBackend.has_value());
    assert(*taskConfig.detectorBackend == QStringLiteral("opencv_dnn"));

    server.stop();
    std::cout << "Detection control server smoke test passed.\n";
    return 0;
}
