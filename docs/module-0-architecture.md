# 模块 0：工程架构骨架

当前仓库已经从单体 Qt demo 拆分为应用层和功能模块层，主线定位为人脸检测 / 识别平台。

```text
apps/viewer              Qt 可视化客户端
modules/common           公共数据结构
modules/video            FFmpeg 视频解码与帧格式转换
modules/pipeline         帧分发与流水线队列
modules/playback         播放流程协调、检测预览、识别调度
modules/inference        OpenCV DNN YOLO 人脸检测
modules/recognition      人脸特征提取、特征库匹配、识别诊断
modules/results          检测结果缓存、查询与统计
modules/storage          SQLite 检测记录、人脸库、特征库、识别事件
modules/control          控制服务端与远程任务配置协议
modules/network          检测结果发送
```

## 主数据流

```text
FFmpegDecoder
  |
  v
VideoFrame
  |
  v
FrameDispatcher
  |-----------------> DisplayQueue   -> MainWindow / VideoDisplayWidget
  |
  |-----------------> InferenceQueue -> YoloOpenCVDnnDetector
                                      -> FaceRecognizer
                                      -> DetectionResults
                                      -> ResultManager
                                      -> SQLiteDetectionStorage
```

`apps/viewer` 负责把这些模块连接起来，但不应该承载底层解码、推理、识别或 SQL 细节。

## 当前边界

- `modules/video` 只负责把输入源解码成中立的 `ivp::VideoFrame`。
- `modules/inference` 只负责人脸检测框，不负责身份匹配。
- `modules/recognition` 只依赖检测框和特征库，输出 `FaceRecognitionResult`。
- `modules/storage` 负责会话、检测记录、人脸身份、特征模板、记录关联和识别事件。
- `modules/playback` 是运行时调度层，协调解码、显示、推理和识别。
- `apps/viewer` 是 UI 和应用流程层，负责参数、按钮、表格和状态展示。

## 当前学习重点

1. 理解为什么 UI 层依赖 `ivp_playback`，而不是直接依赖 FFmpeg。
2. 理解为什么 `VideoFrame` 要作为跨模块的中立帧结构。
3. 理解为什么显示链路和推理链路要用不同消费队列。
4. 理解为什么检测和识别要拆成 `modules/inference` 与 `modules/recognition`。
5. 理解为什么实时识别使用内存特征库，而不是每帧访问 SQLite。
6. 理解为什么识别事件需要和检测记录分开存储。

## 冻结内容

以下内容不是当前主线：

- `modules/audio`
- TensorRT 文档和旧测试。
- 图片序列输入。
- 工业缺陷检测相关资源和文案。
