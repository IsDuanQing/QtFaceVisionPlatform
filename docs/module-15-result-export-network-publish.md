# 模块 15：检测结果导出和网络发送

模块 7 到模块 14 已经把结果缓存、SQLite 存储、历史查询和 TensorRT
推理闭环连接起来。模块 15 的目标，是把一帧检测结果从内存和数据库之外，
继续交付给文件系统和外部网络系统。

## 这个模块到底在补什么

工业视觉系统通常需要同时满足三类需求：

- 现场人员查看当前检测结果
- 质量系统保存检测记录，便于追溯
- MES、上位机或采集服务实时接收检测结果

因此，导出和网络发送不应该直接写进 `MainWindow` 或 `VideoPlayer`。
它们应该有独立的数据对象、配置对象和交付服务。

## 模块拆解

### 1. `DetectionDeliverySettings`

这是结果交付配置：

- 是否启用文件导出
- 导出目录
- 导出格式：JSON Lines 或 CSV
- 是否发送空检测帧
- 是否启用 TCP 发布
- TCP 主机和端口

配置对象放在网络模块中，但由 Qt 客户端的设置层负责持久化。
这样 `VideoPlayer` 不需要知道配置文件的存在。

### 2. `DetectionFramePacket`

这是跨文件和网络边界传递的一帧结果：

- `sourceId`
- `frameIndex`
- `ptsMs`
- `recordedAtMs`
- `DetectionResults`

这里要特别区分：

- `ptsMs` 是视频时间轴上的时间
- `recordedAtMs` 是系统实际记录时间

两者不能混用，否则后续做延迟分析和历史追溯时会丢失信息。

### 3. `DetectionResultDelivery`

这个类负责三件事：

- JSON Lines 文件导出
- CSV 文件导出
- TCP JSON Lines 发布

网络发送使用 Qt 事件循环和 `QTcpSocket`。调用方只负责把结果交给
`deliver()`，不需要同步等待远端服务处理完成。

TCP 消息采用一行一个 JSON 对象的格式：

```text
{"type":"detection","source_id":"camera-1",...}
```

这种格式比把多个 JSON 对象拼成一个大 JSON 数组更适合流式发送，
接收端可以按换行符逐条解析。

### 4. Qt 客户端接入

`MainWindow` 增加了以下配置：

- 导出开关
- JSON Lines / CSV 格式选择
- 导出目录
- 是否包含空检测帧
- TCP 发布开关
- TCP 主机和端口

检测结果回调的处理顺序是：

```text
VideoPlayer::detectionResultsReady
  |
  v
MainWindow::displayDetections
  |
  +--> VideoDisplayWidget 绘制
  +--> ResultManager 统计
  +--> SQLiteDetectionStorage 持久化
  +--> DetectionResultDelivery 导出 / TCP 发布
```

## 文件格式选择

### JSON Lines

适合：

- 网络发送
- 日志采集
- 逐行追加
- 后续使用脚本流式处理

每一行代表一帧检测结果，里面可以包含多个目标。

### CSV

适合：

- Excel 查看
- 简单统计
- 离线分析

CSV 当前采用“一行一个检测目标”的结构，因此一帧有多个目标时会产生
多行记录。

## 当前测试

新增 `ivp_detection_result_delivery_smoke_test`：

- 创建临时导出目录
- 启动本地 `QTcpServer`
- 验证 CSV 文件是否生成
- 验证 TCP 是否收到 JSON Lines
- 验证来源、类别和协议字段是否存在

这类测试不需要 YOLO 或 TensorRT，可以先验证结果交付链路本身。

## 当前工程限制

- 当前实现是单接收端 TCP 发布器，还不是 Linux `epoll` 多客户端服务端。
- TCP 侧只负责发布结果，不负责启动、停止、状态查询等控制命令。
- 文件写入目前仍在调用线程执行，高帧率场景下应该继续拆出专用写线程。
- 当前只有基础队列上限和重连行为，还没有完整的 ACK、重试次数和消息持久化策略。
- CSV 只用于本地导出，网络协议统一使用 JSON Lines。

这些限制是有意保留的。先把“结果能正确交付”验证清楚，再扩展成服务端、
重试和消息可靠性机制，排查问题会更容易。

## 学习重点

1. 阅读 `DetectionFramePacket`，理解为什么需要把帧上下文和检测目标一起传递。
2. 阅读 `DetectionResultDelivery::toJsonLine()`，理解 JSON Lines 的消息边界。
3. 阅读 `enqueueNetworkMessage()` 和 `flushNetworkQueue()`，理解 Qt 异步 Socket 队列。
4. 阅读 `tests/network/DetectionResultDeliverySmokeTest.cpp`，理解如何测试异步 TCP。
5. 思考高帧率下为什么文件 I/O 不应该长期运行在 UI 线程。

## 下一步学习建议

完成本模块后，建议先学习：

- Qt `QTcpSocket` 和事件循环
- TCP 半包、粘包和应用层消息边界
- JSON Lines 与结构化日志
- 生产者-消费者队列中的背压和丢弃策略
- 网络重连、ACK 和幂等设计

然后再进入真正的 Linux `epoll` 服务端模块。
