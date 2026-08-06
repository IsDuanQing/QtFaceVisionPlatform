# 测试问题记录与解决方案

本文记录当前 Qt demo 在测试过程中出现过的问题、原因分析、已经采取的解决方案，以及后续排查方向。

## 1. 打开 MP4 报 Invalid argument (-22)

### 现象

打开 `.mp4` 文件时弹出错误：

```text
Video Error
Could not open the video: Invalid argument (-22)
```

### 原因分析

这类问题通常不是视频解码失败，而是 FFmpeg 打开输入路径失败。

常见原因：

1. 文件路径被当成 URL 解析。
2. 路径中包含空格、中文或特殊字符，直接传给 FFmpeg 时解析失败。
3. 本地文件和网络流没有区分处理，导致本地文件也走了不合适的 URL 逻辑。

### 解决方案

当前代码在 `FFmpegDecoder::open()` 中对本地文件做了单独处理：

1. 使用 `QFileInfo` 检查本地文件是否存在。
2. 优先传入本地绝对路径。
3. 如果打开失败，再使用 `QUrl::fromLocalFile()` 生成 file URL 作为 fallback。

对应代码：

- `modules/video/src/FFmpegDecoder.cpp`
- `FFmpegDecoder::open(const VideoInputConfig& config)`

## 2. 视频播放后没有声音

### 现象

本地视频可以正常播放画面，但没有声音。

### 原因分析

最初的视频链路只实现了视频解码和显示，没有实现音频流解析、音频解码、重采样和播放。

在 FFmpeg 中，视频流和音频流是两条独立的 stream：

```text
MP4 / MKV
  |-- video stream -> AVPacket -> AVFrame -> RGB -> Qt 显示
  |
  |-- audio stream -> AVPacket -> AVFrame -> PCM -> Qt 音频输出
```

只读取视频流时，画面正常但不会有声音。

### 解决方案

当前工程新增了 `modules/audio`：

1. 使用 FFmpeg 打开音频流。
2. 解码音频 `AVPacket`。
3. 使用 `swresample` 转换为 Qt 可播放的 PCM 格式。
4. 使用 Qt 音频输出播放。

对应代码：

- `modules/audio/include/audio/AudioPlayer.h`
- `modules/audio/src/AudioPlayer.cpp`
- `modules/playback/src/VideoPlayer.cpp`

说明：

RTSP 输入当前仍然暂时禁用音频，因为工业视觉检测主流程优先关注视频帧、检测结果和实时性。

## 3. 本地视频音画不同步

### 现象

视频播放过程中，声音和画面不同步。

### 原因分析

如果视频只按固定 FPS 播放，而音频按声卡实际播放进度走，两者很容易产生偏移。

常见原因：

1. 视频帧没有严格按照 PTS 显示。
2. 音频播放时钟和视频播放时钟不是同一个主时钟。
3. 解码、转换、UI 显示耗时导致视频落后。
4. 队列缓存过多，旧帧被延迟显示。

### 解决方案

当前本地文件播放采用音频时钟作为主时钟：

1. 有音频时，使用 `AudioPlayer::positionMs()` 作为 master clock。
2. 视频帧根据 `best_effort_timestamp` 转换后的 PTS 决定显示时机。
3. 视频帧太早则等待。
4. 视频帧明显落后音频时丢帧追赶。
5. 没有音频时，使用 `QElapsedTimer` 作为 fallback clock。

对应代码：

- `modules/playback/src/VideoPlayer.cpp`
- `VideoPlayer::masterClockMs()`
- `VideoPlayer::normalizedFramePositionMs()`
- `VideoPlayer::consumeFileFrame()`

## 4. RTSP 测试没有工业相机

### 现象

当前没有工业相机，不确定如何测试 RTSP 输入。

### 原因分析

RTSP 是网络视频流协议。没有工业相机时，仍然可以在本地搭建 RTSP 服务，把本地 MP4 模拟成实时流。

### 解决方案

推荐测试方式：

1. 本地启动一个 RTSP server。
2. 使用 FFmpeg 将本地 MP4 按实时速度推流到 RTSP server。
3. Qt demo 使用 `Open RTSP` 打开这个地址。
4. 同时用 `ffplay` 或 VLC 打开同一个地址做对照。

对照测试很重要：

1. 如果 `ffplay` / VLC 也花屏，优先检查 RTSP 服务、推流参数、网络和原始视频。
2. 如果 `ffplay` / VLC 正常，但 Qt demo 花屏，优先检查本项目的 FFmpeg options、解码逻辑和帧转换逻辑。

## 5. RTSP 播放画面花屏、马赛克、区块错乱

### 现象

Qt 播放 RTSP 时，画面出现：

1. 方块马赛克。
2. 图像区块破碎错位。
3. 像素块错乱失真。
4. 局部画面残留或拖影。

### 原因分析

这类现象通常发生在压缩码流层，而不是 Qt 绘制层。

H264/H265 是帧间压缩格式。很多 P 帧、B 帧依赖前后的参考帧。如果网络丢包、包顺序异常、关键帧缺失、SPS/PPS 参数不完整，解码器可能只能用错误参考帧继续解码，于是画面会表现为马赛克和区块错乱。

常见原因：

1. RTSP 默认走 UDP，网络丢包导致码流损坏。
2. 本地 RTSP 推流使用了 `-c copy`，原文件时间戳或关键帧结构不适合实时推流。
3. 推流 GOP 太长，关键帧间隔太大，丢一帧后恢复很慢。
4. B 帧重排序增加延迟，实时预览更容易堆积。
5. 解码器收到了 FFmpeg 标记为 corrupted 的帧，但应用层仍然显示出来。

### 解决方案

当前代码已经做了两类防御：

1. RTSP 打开时优先使用 TCP：

```cpp
av_dict_set(&options, "rtsp_transport", "tcp", 0);
```

2. 设置读写超时，避免网络异常时长时间卡死：

```cpp
av_dict_set(&options, "timeout", readTimeoutUs.constData(), 0);
av_dict_set(&options, "rw_timeout", readTimeoutUs.constData(), 0);
```

3. 要求 FFmpeg 尽量丢弃损坏包：

```cpp
av_dict_set(&options, "fflags", "+discardcorrupt", 0);
```

4. 解码后再次检查损坏标记，发现坏帧就丢弃：

```cpp
if ((decodedFrame_->flags & AV_FRAME_FLAG_CORRUPT) != 0
    || decodedFrame_->decode_error_flags != 0)
{
    av_frame_unref(decodedFrame_);
    continue;
}
```

对应代码：

- `modules/video/src/FFmpegDecoder.cpp`

注意：

应用层只能丢弃坏帧，不能修复已经损坏的压缩码流。真正稳定的方案仍然是保证 RTSP 源端和网络传输质量。

## 6. 接收 RTSP 视频流时拖动 Qt 窗口卡顿

### 现象

播放 RTSP 视频流时，拖动 Qt 窗口明显卡顿，窗口响应不流畅。

### 原因分析

虽然视频读取已经放到生产者线程，但 UI 线程仍然负责：

1. 从队列取帧。
2. 将 `VideoFrame` 包装成 `QImage`。
3. 将 `QImage` 转成 `QPixmap`。
4. 按窗口尺寸缩放画面。
5. 更新 QLabel。

RTSP 如果持续 25fps 或 30fps 推流，UI 线程每秒都要处理大量图像显示任务。窗口拖动、重绘和视频刷新抢同一个 UI 线程，就会出现卡顿。

之前的主要性能压力点：

1. `QImage.copy()` 每帧复制一整张 RGB 图。
2. `Qt::SmoothTransformation` 每帧做高质量缩放，CPU 开销较高。
3. RTSP 队列满时仍然保留旧帧，实时预览容易积压。
4. UI 消费定时器使用 `start(0)`，会尽可能快地追帧。

### 解决方案

当前代码已经针对 RTSP 预览做低延迟优化：

1. 本地文件播放和 RTSP 预览分开处理：

- 本地文件：按 PTS 和音频时钟同步，尽量完整播放。
- RTSP：按实时预览策略，只显示最新帧。

2. RTSP 队列满时丢弃最旧帧，避免延迟持续累积：

```cpp
frameQueue_.pushDropOldest(std::move(frame));
```

3. RTSP UI 消费端每次取最新帧，丢弃队列里的旧帧：

```cpp
while (frameQueue_.tryPop(&latestFrame))
{
    hasFrame = true;
}
```

4. RTSP 预览限制 UI 刷新频率，当前约为 30fps：

```cpp
static constexpr int kLivePreviewIntervalMs = 33;
```

5. `QImage` 接管移动出来的帧内存，避免每帧 `copy()`：

```cpp
auto* imageBuffer = new std::vector<std::uint8_t>(std::move(frame.data));
```

6. UI 缩放改为快速缩放：

```cpp
Qt::FastTransformation
```

对应代码：

- `modules/common/include/common/BlockingQueue.h`
- `modules/playback/include/playback/VideoPlayer.h`
- `modules/playback/src/VideoPlayer.cpp`
- `apps/viewer/MainWindow.cpp`

## 后续建议

下一步应该继续把显示链路和推理链路拆开：

```text
Producer Thread
  |
  v
FrameDispatcher
  |-----------------> DisplayQueue   -> Qt 显示，允许丢帧
  |
  |-----------------> InferenceQueue -> TensorRT 推理，按检测策略丢帧或抽帧
```

学习重点：

1. RTSP 和本地文件的播放策略不同。
2. 实时预览追求低延迟，不追求每一帧都显示。
3. UI 线程不能承担重计算。
4. 工业检测平台中，显示、推理、存储、网络发送应该逐步拆成独立链路。
