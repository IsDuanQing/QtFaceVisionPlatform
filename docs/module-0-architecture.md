# 模块 0：工程架构骨架

当前仓库已经从单体 Qt demo 拆分为应用层和功能模块层。

```text
apps/viewer              Qt 可视化客户端
modules/common           公共数据结构
modules/pipeline         帧分发与流水线队列
modules/video            FFmpeg 视频解码与帧格式转换
modules/audio            demo 阶段的音频播放支持
modules/playback         播放流程协调
modules/inference        推理接口、MockDetector、后续 TensorRT
modules/results          检测结果缓存、查询与统计
modules/network          epoll TCP 服务模块占位
modules/storage          SQLite 检测结果存储模块占位
```

当前学习重点：

1. 理解 `CMakeLists.txt` 如何把多个小模块组合成一个完整程序。
2. 理解为什么 UI 层依赖 `ivp_playback`，而不是直接依赖 FFmpeg。
3. 理解为什么 `modules/video` 要输出中立的 `ivp::VideoFrame`。
4. 理解为什么 `modules/pipeline` 要把显示和推理拆成两条消费链路。
5. 理解为什么先用 `IDetector` 和 `MockDetector` 打通推理链路。
6. 理解检测结果为什么需要先进入 `modules/results`，再进入 SQLite、网络发送或统计分析。
