# 模块 17：远程检测任务配置协议

## 模块目标

模块 17 的目标，是把模块 16 的 `start` / `stop` / `status` 控制服务，扩展成真正的远程检测任务管理入口。

当前版本支持外部客户端通过 TCP JSON Lines 下发：

- 视频源类型
- 视频源地址
- 是否自动启动
- 检测后端
- 置信度阈值
- NMS 阈值
- TensorRT / YOLO 模型路径
- 输入尺寸
- 最大检测数量
- 产线编号
- 批次编号
- 任务编号

这样外部系统不再只是“按播放按钮”，而是可以描述一个完整检测任务。

## 为什么任务配置要走协议

工业现场里，检测平台通常不只服务一个视频文件。

更常见的是：

- 上位机告诉检测软件当前产线和批次
- MES 或调度系统下发当前产品型号
- 外部系统指定 RTSP 地址或本地图像目录
- 不同任务使用不同模型或阈值
- 检测结果需要带上任务号，方便追溯

如果这些信息只存在 Qt 界面里，外部系统就无法自动控制检测流程。

模块 17 做的事情，就是把这些配置变成结构化协议。

## 当前新增协议

命令类型：

```json
{"type":"configure_task"}
```

示例：

```json
{
  "type": "configure_task",
  "task_id": "task-001",
  "source_type": "rtsp",
  "source_url": "rtsp://127.0.0.1:8554/test",
  "auto_start": true,
  "production_line_id": "line-a",
  "batch_id": "batch-20260812",
  "detector_backend": "mock",
  "confidence_threshold": 0.55,
  "nms_threshold": 0.45,
  "detect_every_n_frames": 2,
  "input_width": 1088,
  "input_height": 1088,
  "class_count": 20,
  "max_detections": 100,
  "engine_path": "D:/IndustrialVisionPlatform/models/yolo11l/defect.engine",
  "labels_path": "D:/IndustrialVisionPlatform/models/yolo11l/labels.txt"
}
```

服务端返回：

```json
{"type":"configure_task","ok":true,"accepted":true}
```

如果字段类型或取值范围不合法，服务端返回：

```json
{"type":"error","code":"invalid_task_config","message":"confidence_threshold is out of range."}
```

## 字段说明

`source_type` 支持：

- `file`
- `rtsp`
- `image_sequence`

`source_url` 含义取决于 `source_type`：

- `file`：本地视频文件路径
- `rtsp`：RTSP 地址
- `image_sequence`：图片文件夹路径

`detector_backend` 支持：

- `mock`
- `opencv_dnn`
- `tensorrt`

当前字段采用“可选覆盖”设计。客户端只传想修改的字段，没有传的字段继续沿用 Qt 界面当前配置。

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
  +--> 打开 file / rtsp / image_sequence
  +--> 根据 auto_start 决定是否播放
```

注意：服务端只负责协议解析和信号通知，不直接操作播放器。真正打开视频源和应用检测参数仍然在 UI 主线程里完成。

## 检测结果追溯

模块 17 还把任务上下文写入检测结果包：

- `task_id`
- `production_line_id`
- `batch_id`

这样 JSON Lines、CSV 导出、TCP 发布和控制服务广播里的检测结果都能带上任务归属。

这对工业质检很重要。否则外部系统收到一条缺陷记录时，只知道“某一帧有缺陷”，但不知道它属于哪条产线、哪个批次、哪一次检测任务。

## 本地测试方式

当前 Windows / Qt Creator 也能测试控制协议，因为非 Linux 分支使用 Qt `QTcpServer` 提供本地 TCP 后端。

启动 Qt 程序后，可以用任意 TCP 客户端连接：

```text
127.0.0.1:9100
```

发送一行：

```json
{"type":"configure_task","task_id":"task-local","source_type":"rtsp","source_url":"rtsp://127.0.0.1:8554/test","auto_start":true,"detector_backend":"mock","confidence_threshold":0.5,"production_line_id":"line-a","batch_id":"batch-001"}
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

1. 当前远程任务仍然运行在 Qt demo 进程内，还不是独立无界面服务。
2. 远程命令没有鉴权，只适合本地或可信网络测试。
3. `configure_task` 只有 accepted 回复，不代表视频源一定打开成功。
4. 打开失败信息目前主要体现在后续 `status` 和 Qt 日志中。
5. 真正工业部署时还需要任务 ID 幂等、命令序号、ACK、错误码规范和权限控制。

## 学习重点

完成这个模块后，建议你重点学习：

1. 协议字段为什么要区分“命令 accepted”和“任务执行成功”。
2. 为什么远程任务配置采用可选字段覆盖，而不是要求每次传完整配置。
3. 为什么服务端不能直接操作 UI 或播放器，而要发信号交给主线程。
4. 为什么检测结果必须带任务上下文，方便追溯和统计。
5. 如何设计错误码，而不是只返回一段自然语言错误。

## 下一步建议

模块 18 可以做任务执行反馈和错误码体系。

当前 `configure_task` 回复的是“命令已接收”。下一步可以增加：

- `task_configured`
- `task_started`
- `task_failed`
- `task_stopped`
- `error_code`
- `request_id`

这样外部客户端就能可靠知道每条命令的最终执行结果。
