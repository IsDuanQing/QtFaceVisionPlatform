# 模块 17：远程检测任务配置协议

## 模块目标

模块 17 的目标，是把模块 16 的 `start` / `stop` / `status` 控制服务，扩展成远程人脸检测任务配置入口。

当前版本支持外部客户端通过 TCP JSON Lines 下发：

- 视频源类型。
- 视频源地址。
- 是否自动启动。
- 置信度阈值。
- NMS 阈值。
- 检测抽帧间隔。
- 输入尺寸。
- 类别数量。
- 最大检测数量。
- ONNX / labels 路径。
- 任务编号。
- 场景或业务上下文编号。

当前检测后端固定为 OpenCV DNN，人脸模型参数通过 `onnx_path`、`labels_path`、`input_width`、`input_height` 和 `class_count` 配置。

## 为什么任务配置要走协议

人脸平台通常不只服务一个视频文件。更常见的是：

- 外部系统指定 RTSP 地址。
- 调度系统告诉客户端当前任务编号。
- 不同场景使用不同阈值。
- 检测结果需要带上任务上下文，方便追溯。

如果这些信息只存在 Qt 界面里，外部系统就无法自动控制检测流程。

模块 17 做的事情，就是把这些配置变成结构化协议。

## 当前协议

命令类型：

```json
{"type":"configure_task"}
```

示例：

```json
{
  "type": "configure_task",
  "request_id": "req-001",
  "task_id": "face-task-001",
  "source_type": "rtsp",
  "source_url": "rtsp://127.0.0.1:8554/test",
  "auto_start": true,
  "production_line_id": "gate-a",
  "batch_id": "shift-20260821",
  "confidence_threshold": 0.5,
  "nms_threshold": 0.45,
  "detect_every_n_frames": 1,
  "input_width": 640,
  "input_height": 640,
  "class_count": 1,
  "max_detections": 300,
  "onnx_path": "D:/QtFaceVisionPlatform/models/yolov8-face/face.onnx",
  "labels_path": "D:/QtFaceVisionPlatform/models/yolov8-face/labels.txt"
}
```

服务端返回：

```json
{"type":"configure_task","ok":true,"accepted":true,"request_id":"req-001"}
```

如果字段类型或取值范围不合法，服务端返回：

```json
{"type":"error","code":"invalid_task_config","request_id":"req-001","message":"confidence_threshold is out of range."}
```

## 字段说明

`request_id` 是客户端生成的请求标识，用来把服务端 ACK 或错误回包和原始请求对应起来。它不是检测任务编号，不参与结果追溯，可以每次请求都不同。

`task_id` 是检测任务编号，会进入状态快照和检测结果包，用于后续查询、导出、统计和追溯。

`source_type` 当前支持：

- `file`
- `rtsp`

`source_url` 含义取决于 `source_type`：

- `file`：本地视频文件路径。
- `rtsp`：RTSP 地址。

检测参数采用“可选覆盖”设计。客户端只传想修改的字段，没有传的字段继续沿用 Qt 界面当前配置。

注意：如果传了 `source_type`，必须同时传 `source_url`；如果只想修改阈值或模型路径，可以不传视频源字段。

## 当前代码流程

```text
外部客户端
  |
  v
TCP JSON Lines
  |
  v
DetectionControlServer
  |
  v
DetectionTaskConfig
  |
  v
MainWindow::applyRemoteTaskConfig()
  |
  +--> 更新 Qt 参数控件
  +--> 应用 DetectorConfig
  +--> 打开 file / rtsp
  +--> 根据 auto_start 决定是否播放
```

服务端只负责协议解析和信号通知，不直接操作播放器。真正打开视频源和应用检测参数仍然在 UI 主线程里完成。

## 结果追溯

模块 17 会把任务上下文写入运行状态和检测结果包：

- `task_id`
- `production_line_id`
- `batch_id`

在人脸场景里，这些字段可以代表门禁点位、摄像头分组、班次、任务批次或业务场景。外部系统收到人脸检测 / 识别结果时，可以知道它来自哪一路输入、哪一次任务和哪一个业务上下文。

## 本地测试方式

当前 Windows / Qt Creator 也能测试控制协议，因为非 Linux 分支使用 Qt `QTcpServer` 提供本地 TCP 后端。

启动 Qt 程序后，可以用任意 TCP 客户端连接：

```text
127.0.0.1:9100
```

发送一行：

```json
{"type":"configure_task","request_id":"req-local","task_id":"face-local","source_type":"rtsp","source_url":"rtsp://127.0.0.1:8554/test","auto_start":true,"confidence_threshold":0.5,"nms_threshold":0.45,"input_width":640,"input_height":640,"class_count":1,"max_detections":300,"onnx_path":"D:/QtFaceVisionPlatform/models/yolov8-face/face.onnx","labels_path":"D:/QtFaceVisionPlatform/models/yolov8-face/labels.txt","production_line_id":"gate-a","batch_id":"shift-001"}
```

然后再发送：

```json
{"type":"status"}
```

状态里应该能看到：

- `task_id`
- `production_line_id`
- `batch_id`
- `source_id`
- `opened`
- `playing`

## 当前限制

1. 当前远程任务仍然运行在 Qt 客户端进程内，还不是独立无界面服务。
2. 远程命令没有鉴权，只适合本地或可信网络测试。
3. `configure_task` 只有 accepted 回复，不代表视频源一定打开成功。
4. 打开失败信息目前主要体现在后续 `status` 和 Qt 日志中。
5. 当前协议不负责同步人脸库，Faces 仍在客户端本地 SQLite 中管理。
6. 后续部署时还需要任务 ID 幂等、命令序号、ACK、错误码规范和权限控制。

## 学习重点

1. 协议字段为什么要区分“命令 accepted”和“任务执行成功”。
2. 为什么远程任务配置采用可选字段覆盖，而不是要求每次传完整配置。
3. 为什么服务端不能直接操作 UI 或播放器，而要发信号交给主线程。
4. 为什么检测结果必须带任务上下文，方便追溯和统计。
5. 如何设计错误码，而不是只返回一段自然语言错误。

你现在最该重点学的是：

- `request_id` 为什么要和 ACK 一起返回。
- 为什么服务端要先做字段校验，再把任务交给 UI 线程。
- 为什么 `status` 既是查询接口，也是外部系统的同步依据。

## 下一步建议

下一步可以增加任务执行反馈和错误码体系：

- `task_configured`
- `task_started`
- `task_failed`
- `task_stopped`
- `error_code`
- `request_id`

这样外部客户端就能可靠知道每条命令的最终执行结果。
