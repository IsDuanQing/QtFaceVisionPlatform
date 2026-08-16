# Industrial Vision Platform

基于 C++17 / Linux / Qt / FFmpeg / TensorRT 的工业视觉检测平台。

当前仓库处于工程骨架阶段，已从单体 Qt demo 拆分为应用层和功能模块层。现阶段重点是先稳定视频输入、解码、播放和模块边界，后续再逐步接入 OpenCV、TensorRT、网络服务和数据存储。

## 工程结构

```text
IndustrialVisionPlatform/
  apps/
    viewer/                  Qt 可视化客户端

  modules/
    common/                  公共数据结构与基础工具
    pipeline/                帧分发、显示/推理消费队列
    video/                   视频输入、FFmpeg 解码、帧格式转换
    audio/                   demo 阶段的音频播放支持
    playback/                播放流程协调
    inference/               TensorRT / YOLO 推理模块占位
    results/                 检测结果缓存、查询与统计
    network/                 检测结果导出与 TCP 发布
    control/                 检测服务端与控制协议
    storage/                 SQLite / 检测记录存储模块

  docs/                      学习笔记与阶段性设计文档
  CMakeLists.txt             主 CMake 工程入口
  IndustrialVisionPlatform.pro Qt Creator qmake 过渡入口
```

## 学习文档

- `docs/module-0-architecture.md`：工程骨架与模块边界
- `docs/module-1-video-input.md`：视频输入模块重构
- `docs/module-2-producer-consumer.md`：生产者-消费者队列
- `docs/module-3-rtsp-input.md`：RTSP 输入入口
- `docs/module-4-frame-dispatcher.md`：帧分发与双消费队列
- `docs/module-5-inference-interface.md`：推理接口与 MockDetector
- `docs/module-6-detection-overlay.md`：检测结果回传与 UI 画框
- `docs/module-7-result-management.md`：检测结果管理与统计
- `docs/module-8-sqlite-storage.md`：SQLite 检测结果存储
- `docs/module-9-history-query.md`：历史检测记录查询界面
- `docs/module-10-yolo-tensorrt.md`：YOLO TensorRT 推理准备
- `docs/module-11-image-sequence-input.md`：图片文件夹模拟视频输入
- `docs/module-12-detector-configuration.md`：检测参数配置界面
- `docs/module-13-settings-persistence.md`：检测参数持久化与启动恢复
- `docs/module-14-tensorrt-closure.md`：真实 TensorRT 推理闭环
- `docs/module-15-result-export-network-publish.md`：检测结果导出和网络发送
- `docs/module-16-detection-control-server.md`：检测服务端与控制协议
- `docs/module-17-remote-task-configuration.md`：远程检测任务配置协议
- `docs/module-18-yolo-opencv-dnn.md`：OpenCV DNN 真实 YOLO 检测闭环
- `docs/test-issues-and-solutions.md`：测试问题记录与解决方案

## 模块职责

### apps/viewer

Qt 可视化客户端。

负责：
- 创建主窗口和交互界面
- 打开本地视频文件
- 输入并打开 RTSP 视频流
- 打开图片文件夹作为模拟视频输入
- 显示视频画面
- 叠加显示检测框和标签
- 编辑检测后端、阈值、模型路径和图片序列 FPS
- 保存并恢复检测参数配置
- 显示分辨率、FPS、音频状态、播放状态等信息
- 响应播放、暂停、停止等用户操作

不负责：
- FFmpeg 解码细节
- TensorRT 推理
- 数据存储
- 网络通信

### modules/common

公共结构模块。

负责：
- 定义跨模块共享的数据结构
- 放置后续线程队列、时间戳、检测结果等公共类型

当前已有：
- `VideoFrameMetadata`：视频帧元信息结构
- `VideoFrame`
- `PixelFormat`
- `BlockingQueue`
- `DetectionResult`
- `BoundingBox`

后续会扩展：
- 时间戳与错误码工具

### modules/video

视频处理模块。

负责：
- 使用 FFmpeg 打开视频输入
- 解析视频流
- 解码 H264/H265 等视频帧
- 管理 `AVFormatContext`、`AVCodecContext`、`AVPacket` 等 FFmpeg 资源
- 将 `AVFrame` 转换为平台内部的 `VideoFrame`
- 不包含 Qt 界面显示逻辑

当前类：
- `VideoInputConfig`
- `FFmpegDecoder`
- `FrameConverter`
- `ImageSequenceReader`

后续目标：
- 支持 MP4、RTSP、图片文件夹
- 支持转换为 `cv::Mat`
- 为 TensorRT 输入预处理提供帧数据

### modules/pipeline

帧分发与流水线协调模块。

负责：
- 接收解码后的 `VideoFrame`
- 将帧分发到显示队列和推理队列
- 为不同消费链路提供不同丢帧策略
- 避免显示和推理直接抢同一个队列

当前类：
- `FrameDispatcher`

后续目标：
- 增加推理任务分发
- 增加检测结果回传通道
- 支持显示、推理、存储、网络发送之间的流水线拆分

### modules/audio

音频播放模块。

负责：
- 从视频文件中打开音频流
- 使用 FFmpeg 解码音频
- 使用 `swresample` 转换为 PCM
- 使用 Qt6 `QAudioSink` 播放声音
- 提供音频播放时钟，辅助音画同步

说明：
- 工业检测主流程通常不依赖音频
- 当前模块主要服务于 demo 播放体验
- 后续实际检测链路可以弱化或移除该模块

### modules/playback

播放流程协调模块。

负责：
- 协调视频解码、显示适配、音频播放
- 管理视频读取生产线程
- 根据当前配置初始化检测器
- 使用 `FrameDispatcher` 分发显示帧和推理帧
- 将 `VideoFrame` 转换为 Qt 可显示的 `QImage`
- 向 UI 层发送可显示帧
- 向 UI 层发送检测结果
- 启动推理消费线程
- 处理播放、暂停、停止
- 处理基础音画同步

当前类：
- `VideoPlayer`

后续目标：
- 将检测结果回传给 UI 绘制缺陷框
- 继续减少播放模块对推理实现细节的了解
- 支持文件、RTSP、图片序列三种输入源统一调度

### modules/inference

AI 推理模块。

负责：
- 定义推理接口
- 定义推理参数
- 输出结构化检测结果
- 提供 `MockDetector` 验证推理链路
- 加载 ONNX / TensorRT Engine
- 管理 CUDA / TensorRT 资源
- 执行 YOLO 推理
- 图像预处理
- 检测结果解析
- 支持 FP16 加速

当前接口与配置：
- `IDetector`
- `DetectorConfig`

当前实现类：
- `MockDetector`
- `TensorRTEngine`
- `YoloPreprocessor`
- `YoloPostprocessor`
- `YoloOpenCVDnnDetector`
- `YoloTensorRTDetector`

说明：
- 默认 Qt demo 仍使用 `MockDetector`
- MSYS2 MinGW/UCRT64 检测到 OpenCV 4/5 后会自动启用 OpenCV DNN；其他工具链可通过 `IVP_ENABLE_OPENCV_DNN` 显式启用
- 定义 `IVP_ENABLE_TENSORRT` 后才编译真实 TensorRT 执行代码
- 当前 YOLO 输入约定为 `images [1, 3, 1088, 1088]`
- 当前 YOLO 输出约定为 `output0 [1, 24, 24276]`

### modules/results

检测结果管理模块。

负责：
- 接收一帧推理结果
- 补齐结果的 sourceId、帧号和时间戳上下文
- 保存有界的最近检测记录
- 提供按帧查询和最近结果查询
- 统计处理帧数、检测帧数、目标总数和类别数量

当前类：
- `ResultManager`
- `ResultManagerConfig`
- `DetectionSummary`

说明：
- 当前使用内存缓存，避免过早把 UI 和 SQLite 绑定
- `maxStoredResults` 控制内存记录上限
- 后续可以在 ResultManager 外侧增加 SQLite 持久化消费者

### modules/network

检测结果导出与网络发布模块。

负责：
- 将一帧检测结果导出为 JSON Lines 或 CSV
- 通过 TCP 将检测结果推送到外部接收端
- 维护导出路径、连接状态和错误信息
- 尽量不阻塞播放和推理主流程

当前类：
- `DetectionDeliverySettings`
- `DetectionFramePacket`
- `DetectionResultDelivery`

说明：
- 当前实现是 Qt 客户端侧的结果发布器，采用异步 `QTcpSocket`
- 先支持单接收端、JSON Lines 发送和可选 CSV 落盘
- 后续如果要做 Linux `epoll` 服务端，可以在这个模块之外再拆控制通道和结果通道

### modules/control

检测服务端与控制协议模块。

负责：
- 在 Linux 下启动 `epoll` TCP 服务端
- 支持多个外部客户端同时连接
- 接收检测控制命令
- 返回当前检测状态快照
- 将最新检测结果广播给已连接客户端
- 保持控制协议和 Qt 界面解耦

当前类：
- `DetectionControlServerSettings`
- `DetectionControlStatus`
- `DetectionControlServer`

说明：
- 当前协议采用 JSON Lines，一行就是一条完整命令或事件
- 当前支持 `start`、`stop`、`status`、`ping`
- Windows / MinGW Qt demo 下保留占位实现，真实 `epoll` 服务端只在 Linux 启用

### modules/storage

检测结果持久化模块。

负责：
- 将检测会话写入 SQLite
- 将每帧检测结果写入数据库
- 记录目标框、置信度、类别、帧号和时间戳
- 支持按会话和帧号查询历史记录
- 为后续统计和追溯提供持久化底座

当前类：
- `SQLiteDetectionStorage`

说明：
- 当前版本采用 SQLite C API
- 写入时使用事务，保证一帧检测结果要么全写入，要么全回滚
- 数据库文件默认放在应用本地数据目录

## 当前主流程

```text
MainWindow
  |
  v
VideoPlayer
  |-----------------> VideoInputConfig(File / RTSP)
  |
  |-----------------> Producer Thread
  |                       |
  |                       v
  |                  FFmpegDecoder
  |                       |
  |                       v
  |                  AVPacket -> AVFrame -> VideoFrame
  |                       |
  |                       v
  |                  BlockingQueue<VideoFrame>
  |
  |-----------------> UI Consumer Timer
  |                       |
  |                       v
  |                  VideoFrame -> QImage
  |
  |-----------------> Inference Consumer Thread
  |                       |
  |                       v
  |                  IDetector -> DetectionResults
  |                       |
  |                       v
  |                  ResultManager -> UI 统计 / 存储 / 结果导出 / TCP 发布 / 控制服务广播
  |
  |-----------------> AudioPlayer
                          |
                          v
                    AudioFrame -> PCM -> QAudioSink
```

## 模块 1 当前状态

模块 1：视频输入模块重构。

已完成：
- `modules/common` 定义了平台内部帧结构 `VideoFrame`
- `modules/video` 输出 `VideoFrame`，不再输出 `QImage`
- `modules/video` 新增 `VideoInputConfig`，统一描述本地文件和 RTSP 输入
- `FFmpegDecoder` 支持通过 `VideoInputConfig` 打开输入源
- `modules/playback` 负责把 `VideoFrame` 适配成 `QImage`
- `apps/viewer` 继续只和 `VideoPlayer` 交互，不直接接触 FFmpeg

## 模块 2 当前状态

模块 2：生产者-消费者队列。

已完成：
- `modules/common` 新增通用 `BlockingQueue<T>`
- `VideoPlayer` 内部新增视频读取生产线程
- 生产线程负责 `FFmpegDecoder::readFrame()`
- UI 定时器负责从队列取出 `VideoFrame` 并转换为 `QImage`
- `FFmpegDecoder` 增加中断回调，方便停止阻塞式读取

学习重点：
- FFmpeg 解码流程
- `AVPacket` 和 `AVFrame` 生命周期
- C++ RAII 资源管理
- 模块接口设计
- `std::thread`、`std::mutex`、`std::condition_variable`
- 有界队列、阻塞等待、关闭唤醒

后续演进：
- `FrameDispatcher` 已在模块 4 中实现
- 等显示、推理两条消费链路继续稳定后，再考虑增加 `IVideoSource` 多态接口

## 模块 3 当前状态

模块 3：RTSP 输入入口。

已完成：
- `VideoPlayer` 新增 `openRtsp()`
- `MainWindow` 新增 `Open RTSP` 按钮和 RTSP URL 输入框
- RTSP 输入复用 `VideoInputConfig::fromRtsp()`
- RTSP 播放复用生产者-消费者队列
- RTSP 预览暂时不启用音频，使用视频 fallback clock

学习重点：
- 本地文件输入和 RTSP 输入的差异
- FFmpeg RTSP options：`rtsp_transport`、`stimeout`、`rw_timeout`
- 网络流停止时为什么不能像文件一样 seek 回开头
- UI 层为什么只调用 `VideoPlayer::openRtsp()`，不直接接触 FFmpeg

## 模块 4 当前状态

模块 4：帧分发 `FrameDispatcher`。

已完成：
- 新增 `modules/pipeline`
- 新增 `FrameDispatcher`
- 显示队列和推理队列分离
- 显示链路继续由 Qt UI 消费
- 推理链路先由模拟推理线程消费
- 使用 `shared_ptr<const VideoFrame>` 避免大图像重复拷贝

学习重点：
- 多消费者队列设计
- 显示和推理为什么不能抢同一个队列
- 实时预览和推理任务的不同丢帧策略
- `shared_ptr<const T>` 在跨线程只读共享中的作用

## 模块 5 当前状态

模块 5：推理接口与 `MockDetector`。

已完成：
- 新增 `DetectionResult` 和 `BoundingBox`
- 新增 `IDetector` 推理接口
- 新增 `DetectorConfig`
- 新增 `MockDetector`
- `VideoPlayer::inferenceLoop()` 接入 `IDetector::detect()`
- Mock 推理结果会在日志中输出消费 FPS 和检测数量

学习重点：
- 推理接口为什么要先于 TensorRT 实现
- `DetectionResult` 为什么属于公共模块
- `MockDetector` 如何验证推理线程和队列链路
- 后续如何把 `MockDetector` 替换成 `YoloTensorRTDetector`

## 模块 6 当前状态

模块 6：检测结果回传与 UI 画框。

已完成：
- `VideoPlayer` 新增 `detectionResultsReady` 信号
- 推理线程把 `DetectionResults` 回传到 UI 线程
- `apps/viewer` 新增 `VideoDisplayWidget`
- `VideoDisplayWidget` 负责绘制视频帧和检测框
- `MainWindow` 不再自己缩放 `QPixmap`，而是把帧和结果交给专用控件

学习重点：
- 为什么检测结果要从后台线程回到主线程
- 为什么界面层只负责绘制，不直接参与推理逻辑
- 如何把检测框坐标从原图映射到显示区域
- `QPainter` 和 `paintEvent()` 的基本用法

## 模块 7 当前状态

模块 7：检测结果管理与统计。

已完成：
- 新增 `modules/results`
- 新增线程安全的 `ResultManager`
- 支持最近结果缓存、按帧查询和容量限制
- 支持处理帧数、检测帧数、目标总数和类别统计
- `MainWindow` 接入结果管理器，显示“当前帧检测数 / 累计检测数”

学习重点：
- 为什么结果管理不应该放进 `MainWindow`
- 为什么缓存必须设置容量上限
- 为什么“最近缓存”和“累计统计”要分开
- `std::mutex` 如何保护跨线程查询和写入

## 模块 8 当前状态

模块 8：SQLite 检测结果存储。

已完成：
- 新增 `modules/storage`
- 新增 `SQLiteDetectionStorage`
- 新增 `inspection_sessions`、`detection_frames`、`detection_records` 表
- 使用事务写入一帧结果和该帧所有检测框
- `MainWindow` 在播放时自动创建存储会话并写入 SQLite

学习重点：
- 为什么持久化层要和 UI 分离
- 为什么一帧结果要放进同一个事务
- 为什么要记录 session、frameIndex、ptsMs 和 sourceId
- 为什么 SQLite 适合作为模块 8 的第一版存储后端

## 模块 9 当前状态

模块 9：历史检测记录查询界面。

已完成第一版：
- `SQLiteDetectionStorage` 新增会话摘要和历史记录查询接口
- 支持按会话、sourceId、类别和记录时间筛选
- 新增 `DetectionHistoryTableModel`
- Qt 主窗口增加可拖动的历史记录查询面板
- 增加最近会话下拉框、筛选条件、数量限制和历史记录表格

学习重点：
- SQLite 查询层为什么不能放进 Qt 控件
- `QAbstractTableModel` 如何承载结构化历史数据
- 为什么查询结果需要携带 session、frame 和检测框上下文
- 为什么历史查询默认需要数量上限

## 模块 10 当前状态

模块 10：YOLO TensorRT 推理准备。

已完成第一阶段：
- `DetectorConfig` 增加 TensorRT/YOLO 所需模型配置。
- 新增 `YoloPreprocessor`，完成 RGB24 到 letterbox/CHW float 输入。
- 新增 `YoloPostprocessor`，完成常见 YOLO 输出解析、置信度过滤和 NMS。
- 新增 `YoloTensorRTDetector` 作为 `IDetector` 的可替换实现。
- `VideoPlayer` 支持通过 `DetectorConfig` 选择 `MockDetector` 或 TensorRT 后端。
- `VideoPlayer` 可默认查找 `models/yolo11l` 下的 ONNX、Engine 和标签文件。
- 新增 `TensorRTEngine`，封装 TensorRT Engine 加载、I/O Tensor、CUDA Buffer 和 `enqueueV3`。
- `YoloTensorRTDetector` 已串起前处理、TensorRT 推理和后处理流程。
- 新增前后处理单元测试。

当前限制：
- TensorRT 后端默认不编译；需要显式打开 `IVP_ENABLE_TENSORRT` 并链接 `nvinfer/cudart`。
- 当前只支持一个输入 Tensor 和一个输出 Tensor，且 Tensor 类型为 FLOAT。
- `.onnx` 和 `.engine` 为本地模型资源，不建议提交到 GitHub。

学习重点：
- letterbox 缩放和检测框反算。
- HWC 与 CHW 内存布局。
- YOLO 输出格式、objectness、类别分数和 class-aware NMS。
- TensorRT 资源生命周期与 GPU 异步执行。

## 模块 12 当前状态

模块 12：检测参数配置界面。

已完成：
- `VideoPlayer` 持有 `DetectorConfig`，打开输入源前按当前配置初始化检测器。
- `MainWindow` 增加检测参数面板。
- 支持设置检测后端、置信度阈值、NMS 阈值、最大检测数量、输入尺寸、类别数、Mock 延迟和抽帧间隔。
- 支持设置 ONNX、TensorRT Engine、labels 文件路径。
- 支持设置图片文件夹模拟视频输入的 FPS。
- 支持点击 `Apply Parameters` 运行中应用检测器参数。
- `Clear Filters` 只清历史筛选条件，`Clear Overlay` 用于清当前画面检测框。

当前限制：
- 图片序列 FPS 修改后仍需要重新打开图片目录才生效。
- MinGW Qt demo 默认仍建议使用 `MockDetector`。
- TensorRT 后端配置入口已存在，但真实执行仍建议放到 MSVC Kit 或 Linux 环境验证。

学习重点：
- 为什么运行参数要集中到配置对象。
- UI 参数如何传递到业务模块。
- 为什么不要让 `VideoPlayer` 长期依赖环境变量。
- 为什么运行中热切换检测器需要线程同步。

## 模块 13 当前状态

模块 13：检测参数持久化与启动恢复。

已完成：
- 新增 `ViewerSettingsStore`，使用 `QSettings` 读写 INI 配置文件。
- 启动时恢复检测参数和图片序列 FPS。
- 退出时保存当前参数。
- 支持 `Restore Defaults` 恢复程序默认配置。
- 配置文件默认位于 `QStandardPaths::AppLocalDataLocation/settings.ini`。

当前限制：
- 只保存检测参数，不保存最近打开的视频、RTSP 地址或图片目录。
- 配置文件版本迁移只预留了 `schemaVersion`，暂未实现升级逻辑。
- 参数仍在下一次打开输入源时生效，不做运行中热切换。

学习重点：
- `QSettings` 的 INI 持久化方式。
- 默认配置和用户配置的边界。
- 为什么配置存储属于 Qt 客户端层，而不是播放层或推理层。

## 模块 14 当前状态

模块 14：真实 TensorRT 推理闭环。

已完成：
- `YoloTensorRTDetector` 现在支持 `detectEveryNFrames`。
- 推理线程会检查 detector 的 `lastError()`，真实失败会回传到 UI。
- 新增 `ivp_yolo_tensorrt_detector_smoke_test`，用于验证真实推理链路。

当前限制：
- MinGW Qt demo 依然更适合跑 `MockDetector`。
- 真实 TensorRT 建议在 MSVC Kit 或 Linux 上验证。
- 目前还没有做多 engine、多模型版本切换。

学习重点：
- 真实推理失败如何回传。
- 为什么 detector 的节流行为要和 Mock 对齐。
- 为什么真实 TensorRT 要先做 smoke test，再接 UI。

## 模块 15 当前状态

模块 15：检测结果导出和网络发送。

已完成：
- 新增 `DetectionDeliverySettings`，统一描述导出目录、格式和 TCP 发布参数。
- 新增 `DetectionFramePacket`，把一帧检测结果和上下文打包成可传输对象。
- 新增 `DetectionResultDelivery`，支持 JSON Lines / CSV 落盘和异步 TCP 发布。
- `MainWindow` 增加结果导出和网络发送配置区。
- 检测结果回调时会进入导出/发送链路，网络写入由事件循环异步完成。
- 新增 smoke test，覆盖本地文件导出和 TCP 收包。

当前限制：
- 目前是单接收端发布器，还不是 epoll 多客户端服务端。
- TCP 侧当前只发布 JSON Lines，不负责命令控制通道。
- 失败重连和背压策略还比较轻量，后续可以继续增强。

学习重点：
- 为什么结果导出不能绑死在 UI 线程里。
- JSON Lines 和 CSV 各适合什么场景。
- 为什么网络发送和文件落盘要做成同一个“交付”模块。
- 如何用本地 `QTcpServer` 做异步网络烟测。

## 模块 16 当前状态

模块 16：检测服务端与控制协议。

已完成：
- 新增 `modules/control`。
- 新增 `DetectionControlServer`，Linux 下基于 `epoll` 实现多客户端 TCP 服务端。
- 协议采用 JSON Lines，支持 `start`、`stop`、`status`、`ping`。
- Qt 主窗口启动时会初始化控制服务，默认监听 `127.0.0.1:9100`。
- 检测结果产生后会通过控制服务广播给已连接客户端。
- 新增 Linux smoke test，覆盖连接、状态查询、启动命令和停止命令。

当前限制：
- Windows / MinGW demo 环境使用 Qt `QTcpServer` 后端；Linux 环境使用 `epoll` 后端。
- `start` 暂时只恢复已经打开的输入源，不负责远程打开视频或 RTSP。
- 协议暂时没有鉴权、命令序号、ACK 重试和任务配置字段。

学习重点：
- TCP 粘包/半包与 JSON Lines 消息边界。
- Linux `epoll` 服务端的 non-blocking socket 事件循环。
- 服务端线程如何通过 Qt queued signal 安全通知 UI 线程。
- 控制通道为什么要和检测结果导出通道分开。

## 模块 17 当前状态

模块 17：远程检测任务配置协议。

已完成：
- `DetectionControlProtocol` 新增 `DetectionTaskConfig`。
- 控制协议新增 `configure_task` 命令。
- `configure_task` 支持 `request_id`，成功和失败回包都能对账。
- `configure_task` 会先做字段类型、范围和空值校验，再交给 Qt 主线程处理。
- 支持远程下发 `source_type`、`source_url`、`auto_start`。
- 支持远程下发检测后端、置信度阈值、NMS 阈值、输入尺寸、类别数、最大检测数量和模型路径。
- 支持远程下发 `task_id`、`production_line_id`、`batch_id`。
- `MainWindow` 新增远程任务应用流程，复用当前 UI 参数控件和 `VideoPlayer` 链路。
- 检测结果包新增任务上下文字段，JSON Lines、CSV 和控制服务广播都会携带任务归属。
- 非 Linux 环境新增 Qt `QTcpServer` 后端，本地 Qt Creator demo 也可以测试控制协议。

当前限制：
- `configure_task` 返回 accepted 只代表服务端已经接收命令，不代表视频源一定打开成功。
- 当前还没有任务执行完成事件和统一错误码。
- 远程任务仍然运行在 Qt demo 进程内，尚未拆成独立无界面检测服务。
- 协议未做鉴权，只适合本机或可信网络测试。

学习重点：
- 为什么控制协议要区分“命令接收成功”和“任务执行成功”。
- 为什么远程任务字段采用可选覆盖策略。
- 为什么检测结果要带 `task_id`、产线号和批次号。
- Qt `QTcpServer` 本地测试后端与 Linux `epoll` 服务端的职责差异。

## 模块 18 当前状态

模块 18：YoloOpenCVDnnDetector 真实 YOLO 检测闭环。

已完成：
- 新增 `DetectorBackend::OpenCVDnn`。
- 新增 `YoloOpenCVDnnDetector`，通过 OpenCV DNN 加载 ONNX 模型。
- OpenCV DNN 后端复用已有 `YoloPreprocessor` 和 `YoloPostprocessor`。
- Qt 后端选择框新增 `OpenCV DNN`。
- 远程任务协议支持 `detector_backend=opencv_dnn`。
- qmake 在 MSYS2 MinGW/UCRT64 下会自动检测并启用 OpenCV DNN，CMake 仍通过 `IVP_ENABLE_OPENCV_DNN` 选项控制。
- 未检测到或未显式启用 OpenCV DNN 时，会返回清晰错误，不影响 Mock demo 编译运行。

当前限制：
- OpenCV DNN 后端默认使用 CPU。
- 当前只取第一个输出 Tensor。
- 当前仍要求 YOLO 输出布局符合已有后处理支持的格式。
- OpenCV DNN 用于先验证真实检测闭环，最终高性能部署仍建议继续完善 TensorRT。

学习重点：
- ONNX 是模型格式，OpenCV DNN 是推理运行时。
- 为什么真实后端也应该实现统一的 `IDetector`。
- 为什么前处理和后处理要复用，而不是每个后端各写一份。
- 如何先验证真实框，再做 TensorRT 加速。
