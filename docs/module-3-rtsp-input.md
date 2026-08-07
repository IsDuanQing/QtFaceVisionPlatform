# 模块 3：RTSP 输入入口

本模块的目标，是让 Qt demo 不只支持本地 MP4，也能从 UI 输入 RTSP 地址并打开网络视频流。

## 当前设计

```text
MainWindow
  |
  v
VideoPlayer::openRtsp(url)
  |
  v
VideoInputConfig::fromRtsp(url)
  |
  v
FFmpegDecoder::open(config)
  |
  v
Producer Thread -> BlockingQueue<VideoFrame> -> UI Consumer
```

UI 层只知道“打开 RTSP”，不直接接触 FFmpeg 的 `AVFormatContext`、`AVDictionary` 或解码细节。

## 建议阅读的代码

- `apps/viewer/MainWindow.h`
- `apps/viewer/MainWindow.cpp`
- `modules/playback/include/playback/VideoPlayer.h`
- `modules/playback/src/VideoPlayer.cpp`
- `modules/video/include/video/VideoInputConfig.h`
- `modules/video/src/FFmpegDecoder.cpp`

## 关键工程点

1. 本地文件输入可以用 `QFileInfo` 检查是否存在，RTSP 不能这样检查。
2. RTSP 是网络流，停止时通常关闭连接，而不是 seek 回开头。
3. 当前 RTSP 入口暂时禁用音频，工业检测主流程只依赖视频帧。
4. RTSP 仍然复用生产者-消费者队列，避免网络读取阻塞 UI。
5. `FFmpegDecoder` 使用 `rtsp_transport=tcp` 优先保证稳定性。
6. `stimeout` 和 `rw_timeout` 用来限制连接和读写阻塞时间。

## 现在应该学什么

先带着这些问题读代码：

1. 为什么 `MainWindow` 调用的是 `VideoPlayer::openRtsp()`，而不是直接创建 `VideoInputConfig`？
2. 为什么 RTSP 输入不能走 `AudioPlayer::open()` 当前这条路径？
3. 为什么 RTSP 的 `stop()` 要关闭连接，而不是调用 `seekToStart()`？
4. `FFmpegDecoder::open()` 里为什么只有 RTSP 才设置 `AVDictionary` options？
5. 如果 RTSP 断流，生产线程如何把错误传回 UI 线程？

## 下一步重构方向

下一步建议先用真实 RTSP 地址验证：

1. 正确地址能否打开并播放。
2. 错误地址是否能在超时后报错。
3. 播放过程中点击 Stop 是否能及时停止。
4. 断开网络或关闭 RTSP 服务后，UI 是否能收到错误。

验证通过后，已经进入 `FrameDispatcher`。你可以继续阅读 `docs/module-4-frame-dispatcher.md`，理解显示和 TensorRT 推理为什么要拆成两条消费队列。
