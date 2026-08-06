# 模块 2：生产者-消费者队列

本模块的目标，是把“视频读取速度”和“显示/推理消费速度”解耦。

在工业视觉检测里，解码、推理、显示、存储的速度通常不一致。如果所有步骤都串在一个线程里，任何一个慢环节都会拖住整条流水线。

## 当前设计

```text
VideoPlayer
  |
  |-- Producer Thread
  |     |
  |     v
  |   FFmpegDecoder::readFrame()
  |     |
  |     v
  |   BlockingQueue<VideoFrame>
  |
  |-- UI Consumer Timer
        |
        v
      VideoFrame -> QImage -> MainWindow
```

当前第一版只有一个消费者：Qt 播放显示。

后续如果要同时显示和推理，不应该让两个消费者直接抢同一个队列，而应该增加分发层或两条消费队列：

```text
Producer Thread
  |
  v
FrameDispatcher
  |-----------------> DisplayQueue -> UI 显示
  |
  |-----------------> InferenceQueue -> TensorRT 推理
```

## 建议阅读的代码

- `modules/common/include/common/BlockingQueue.h`
- `modules/playback/include/playback/VideoPlayer.h`
- `modules/playback/src/VideoPlayer.cpp`
- `modules/video/include/video/FFmpegDecoder.h`
- `modules/video/src/FFmpegDecoder.cpp`

## 关键工程点

1. `BlockingQueue<T>` 是一个通用有界阻塞队列，不绑定视频业务。
2. 队列容量有限，当前 `VideoPlayer` 使用 8 帧容量，防止延迟无限增长。
3. 生产线程只负责调用 `FFmpegDecoder::readFrame()` 并把帧放入队列。
4. UI 线程只负责从队列取帧、按时间戳同步、转换成 `QImage`。
5. `close()` 会唤醒正在等待的生产者和消费者，避免线程退出时卡死。
6. `FFmpegDecoder` 增加了 interrupt callback，用来打断阻塞式 FFmpeg 读取。
7. 多消费者场景要做帧分发，不能让显示和推理直接抢同一个队列。

## 现在应该学什么

先带着这些问题读代码：

1. 为什么队列要有 `close()`，而不是只靠一个 `bool running`？
2. `condition_variable::wait(lock, predicate)` 的 predicate 解决了什么问题？
3. 为什么视频队列要有容量上限？
4. 如果生产者比消费者快，当前代码会发生什么？
5. 如果消费者比生产者快，当前代码会发生什么？
6. 为什么不能在 UI 线程里调用阻塞式 `pop()`？
7. 为什么停止线程时要同时做三件事：设置停止标志、关闭队列、请求 FFmpeg 中断？
8. 为什么后续显示和推理需要两条队列，或者一个 `FrameDispatcher`？

## 下一步重构方向

RTSP 输入入口已经作为模块 3 的第一版实现。你可以继续阅读 `docs/module-3-rtsp-input.md`。

之后再把队列消费端拆成两条链路：

```text
VideoFrame
  |-----------------> DisplayQueue -> 显示
  |
  |-----------------> InferenceQueue -> 推理
```
