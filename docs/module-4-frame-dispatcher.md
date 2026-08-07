# 模块 4：帧分发 FrameDispatcher

本模块的目标，是把“解码产生一帧”之后的消费链路拆开，让显示和推理不再抢同一个队列。

## 为什么需要 FrameDispatcher

工业视觉检测平台里，显示和推理的目标不一样：

1. Qt 显示追求低延迟，允许丢旧帧。
2. TensorRT 推理追求稳定吞吐，可以抽帧，也可以丢旧任务。
3. 视频读取不能被 UI 拖动、推理耗时或数据库写入拖住。

如果显示和推理直接抢同一个 `BlockingQueue<VideoFrame>`，会出现两个问题：

1. 显示拿走的帧，推理就拿不到。
2. 推理慢时，显示也可能被间接拖慢。

所以需要中间分发层：

```text
Producer Thread
  |
  v
FFmpegDecoder -> VideoFrame
  |
  v
FrameDispatcher
  |-----------------> DisplayQueue   -> Qt 显示
  |
  |-----------------> InferenceQueue -> 模拟推理 / TensorRT
```

## 当前实现

新增模块：

- `modules/pipeline/include/pipeline/FrameDispatcher.h`
- `modules/pipeline/src/FrameDispatcher.cpp`

核心类型：

- `ivp::VideoFramePtr`
- `ivp::FrameQueuePolicy`
- `ivp::FrameDispatcher`

当前分发策略：

1. 生产线程解码得到 `VideoFrame`。
2. `FrameDispatcher::dispatch()` 将它封装成 `shared_ptr<const VideoFrame>`。
3. 同一个只读帧引用分别进入显示队列和推理队列。
4. 显示队列：
   - 本地文件：满了阻塞，保持播放顺序。
   - RTSP：满了丢最旧帧，保持低延迟。
5. 推理队列：满了丢最旧帧，避免推理慢时拖住视频读取。

## 为什么使用 shared_ptr<const VideoFrame>

如果一帧 1920x1080 的 RGB 图像被复制两份，单帧大约是：

```text
1920 * 1080 * 3 ≈ 6 MB
```

30fps 时，复制成本会非常明显。

所以当前版本使用：

```cpp
using VideoFramePtr = std::shared_ptr<const VideoFrame>;
```

显示和推理共享同一份只读帧数据，避免大图像重复拷贝。

## 当前模拟推理线程

`VideoPlayer` 中新增了一个模拟推理线程：

```text
InferenceQueue -> inferenceLoop()
```

它现在不做真实 YOLO 推理，只做三件事：

1. 从推理队列阻塞取帧。
2. sleep 几毫秒，模拟推理耗时。
3. 每消费 30 帧打印一次帧号、PTS 和消费 FPS。

这样可以先验证管线结构是否正确：

1. 视频读取是否继续稳定。
2. UI 显示是否仍然流畅。
3. 推理消费慢一点是否不会阻塞显示。

## 建议阅读的代码

- `modules/pipeline/include/pipeline/FrameDispatcher.h`
- `modules/pipeline/src/FrameDispatcher.cpp`
- `modules/playback/include/playback/VideoPlayer.h`
- `modules/playback/src/VideoPlayer.cpp`
- `modules/common/include/common/BlockingQueue.h`

## 现在应该学什么

带着这些问题读代码：

1. 为什么显示和推理不能直接抢同一个队列？
2. 为什么显示队列和推理队列的丢帧策略不一样？
3. 为什么这里用 `shared_ptr<const VideoFrame>`，而不是复制两份 `VideoFrame`？
4. `FrameDispatcher::close()` 为什么要同时关闭两条队列？
5. 如果推理速度比视频 FPS 慢，当前代码会发生什么？
6. 后面接 TensorRT 时，模拟推理线程应该替换成哪个类？

## 下一步重构方向

下一步可以开始模块 5：推理模块占位。

目标不是立刻上 TensorRT，而是先定义推理接口：

```text
InferenceQueue
  |
  v
IDetector / MockDetector
  |
  v
DetectionResult
```

先把推理输入、输出、线程边界定义清楚，再逐步接 YOLO / TensorRT。
