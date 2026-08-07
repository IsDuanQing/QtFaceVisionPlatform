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
    network/                 epoll / TCP 通信模块占位
    storage/                 SQLite / 检测记录存储模块占位

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
- `docs/test-issues-and-solutions.md`：测试问题记录与解决方案

## 模块职责

### apps/viewer

Qt 可视化客户端。

负责：
- 创建主窗口和交互界面
- 打开本地视频文件
- 输入并打开 RTSP 视频流
- 显示视频画面
- 叠加显示检测框和标签
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

后续目标：
- 支持 MP4、RTSP
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
- 使用 Qt `QAudioOutput` 播放声音
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

### modules/inference

AI 推理模块。

负责：
- 定义推理接口
- 定义推理参数
- 输出结构化检测结果
- 提供 `MockDetector` 验证推理链路

当前类：
- `IDetector`
- `MockDetector`
- `DetectorConfig`

后续负责：
- 加载 ONNX / TensorRT Engine
- 管理 CUDA / TensorRT 资源
- 执行 YOLO 推理
- 图像预处理
- 检测结果解析
- 支持 FP16 加速

计划类：
- `TensorRTEngine`
- `YoloDetector`
- `Preprocessor`
- `Postprocessor`

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

网络通信模块占位。

后续负责：
- 基于 Linux socket / epoll 实现 TCP 服务端
- 管理多客户端连接
- 接收检测任务控制命令
- 推送检测结果和运行状态

计划功能：
- 启动检测
- 停止检测
- 查询状态
- 配置模型参数
- 传输检测结果 JSON

### modules/storage

数据存储模块占位。

后续负责：
- 使用 SQLite 存储检测记录
- 管理缺陷类别、置信度、目标框、时间戳
- 支持历史查询
- 支持统计分析

计划表：
- `detection_records`
- `defect_statistics`
- `runtime_events`

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
  |                  ResultManager -> UI 统计 / 后续存储与网络发送
  |
  |-----------------> AudioPlayer
                          |
                          v
                    AudioFrame -> PCM -> QAudioOutput
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
