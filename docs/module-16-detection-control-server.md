# 模块 16：检测服务端与控制协议

## 模块目标

模块 16 的目标，是给检测平台增加一个 Linux 服务端控制入口。

前面的模块已经完成了视频输入、推理、存储、结果导出和 TCP 发布，但这些能力主要由 Qt 界面主动触发。工业现场部署时，检测软件通常还需要被外部系统控制，例如上位机、产线调度服务或质检平台。控制服务端负责接收这些外部命令，并把当前运行状态和检测结果回传给多个客户端。

当前版本先实现最小可用闭环：

- 支持 Linux `epoll` 多客户端 TCP 服务端
- 支持 JSON Lines 控制协议
- 支持 `start` / `stop` / `status` / `ping` 命令
- 支持把检测结果广播给所有已连接客户端
- Qt Creator / MinGW 下保留可编译的占位实现

## 为什么要新建 control 模块

模块 15 的 `DetectionResultDelivery` 是“客户端发布器”：它把检测结果主动发给一个外部接收端，也可以落盘导出。

模块 16 的 `DetectionControlServer` 是“服务端控制入口”：它监听端口，允许多个客户端连接进来，然后接收命令、返回状态、广播结果。

这两个模块职责不同：

- `network`：结果交付，偏数据输出
- `control`：运行控制，偏服务端协议
- `viewer`：只负责把 UI 操作和控制命令映射到 `VideoPlayer`

这样后续增加权限认证、任务配置、模型切换、心跳超时或生产线协议时，不会把所有网络代码堆进 `MainWindow`。

## 当前代码结构

```text
modules/control/
  include/control/
    DetectionControlProtocol.h   控制服务配置与状态快照结构
    DetectionControlServer.h     控制服务端对外接口

  src/
    DetectionControlServer.cpp   Linux epoll 服务端实现

tests/control/
  DetectionControlServerSmokeTest.cpp
```

Qt 客户端接入点在：

```text
apps/viewer/MainWindow.cpp
```

主窗口启动时会尝试监听：

```text
127.0.0.1:9100
```

在 Windows / MinGW demo 环境中，服务端不会真正启动，只会给出“该服务仅在 Linux 启用”的提示。这样做是为了让你当前 Qt Creator demo 继续能正常编译和运行，同时保留 Linux 目标代码。

## 控制协议

协议采用 JSON Lines。每条消息都是一行 JSON，以 `\n` 结尾。

### 查询状态

客户端发送：

```json
{"type":"status"}
```

服务端返回：

```json
{"type":"status","service_running":true,"listen_address":"127.0.0.1","listen_port":9100,"connected_clients":1,"opened":true,"playing":true,"source_id":"video.mp4","frame_index":120,"pts_ms":4000}
```

### 启动检测

客户端发送：

```json
{"type":"start"}
```

服务端返回：

```json
{"type":"start","ok":true,"accepted":true}
```

当前版本的 `start` 表示：如果 Qt 客户端已经打开视频源，并且当前处于暂停状态，则恢复播放。

### 停止检测

客户端发送：

```json
{"type":"stop"}
```

服务端返回：

```json
{"type":"stop","ok":true,"accepted":true}
```

当前版本的 `stop` 会调用播放器停止当前输入源。

### 心跳

客户端发送：

```json
{"type":"ping"}
```

服务端返回：

```json
{"type":"pong","ok":true}
```

### 检测结果广播

有新检测结果时，服务端会向所有客户端广播：

```json
{"type":"detection","source_id":"video.mp4","frame_index":120,"pts_ms":4000,"detection_count":1,"detections":[{"class_id":0,"class_name":"defect","confidence":0.92,"box":{"x":100,"y":80,"width":60,"height":40}}]}
```

## Linux 测试方式

在 Linux 上启动 Qt 程序后，可以用 `nc` 连接：

```bash
nc 127.0.0.1 9100
```

然后手动输入：

```json
{"type":"status"}
```

```json
{"type":"start"}
```

```json
{"type":"stop"}
```

```json
{"type":"ping"}
```

也可以运行 CMake smoke test：

```bash
cmake --build build --target ivp_detection_control_server_smoke_test
ctest -R ivp_detection_control_server_smoke_test --output-on-failure
```

## 当前工程问题与限制

1. 当前控制服务端只在 Linux 下启用，因为模块目标是学习 `epoll`。
2. `start` 命令暂时不负责打开任意视频源，只控制已经打开的输入源。
3. 协议没有鉴权，不能直接暴露到不可信网络。
4. 当前没有命令序号和 ACK 重试机制，后续接工业系统时需要补充。
5. 结果广播采用内存队列，慢客户端积压过多时会丢弃旧消息。

## 学习重点

完成这个模块后，建议你重点学习：

1. TCP 是字节流，为什么必须自己定义消息边界。
2. JSON Lines 如何解决粘包、半包问题。
3. `epoll` 的 `EPOLLIN` / `EPOLLOUT` / `EPOLLHUP` 分别代表什么。
4. 为什么 socket 要设置成 non-blocking。
5. 为什么服务端线程不能直接操作 Qt UI，而要通过信号切回主线程。
6. 为什么控制命令和检测结果应该走结构化协议，而不是简单字符串。

## 下一步建议

模块 16 完成后，可以进入模块 17：检测任务配置协议。

模块 17 可以让外部客户端通过 TCP 命令设置视频源、检测后端、置信度阈值、NMS 阈值和模型路径。那一步会把“启动/停止控制”升级成真正的“远程检测任务管理”。
