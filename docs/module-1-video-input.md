# 模块 1：视频输入模块重构

本模块的目标，是把视频层从“服务 Qt 显示的辅助代码”，重构成后续人脸检测 / 识别平台可以复用的视频输入组件。

## 目标

`modules/video` 只负责视频输入、解码和基础帧格式转换，并输出平台自己持有的帧数据。

它不应该决定这一帧怎么显示、怎么推理、怎么存储、怎么通过网络发送。这些事情应该交给后面的模块处理。

## 当前设计

```text
VideoInputConfig
  |
  v
FFmpegDecoder
  |
  v
AVPacket -> AVFrame
  |
  v
FrameConverter
  |
  v
ivp::VideoFrame(RGB24 CPU buffer)
```

`modules/playback` 负责把 `ivp::VideoFrame` 适配成 Qt demo 可以显示的 `QImage`。

这样做的好处是：解码器不再知道 Qt 界面怎么显示图片，后续接 OpenCV DNN、人脸识别、网络服务时也不会被 Qt 代码绑住。

## 建议阅读的代码

- `modules/common/include/common/VideoFrame.h`
- `modules/video/include/video/VideoInputConfig.h`
- `modules/video/include/video/FFmpegDecoder.h`
- `modules/video/src/FFmpegDecoder.cpp`
- `modules/video/include/video/FrameConverter.h`
- `modules/video/src/FrameConverter.cpp`
- `modules/playback/src/VideoPlayer.cpp`

## 关键工程点

1. `AVPacket` 保存的是从容器中读出来的压缩数据。
2. `AVFrame` 保存的是 FFmpeg 解码后的图像数据。
3. `ivp::VideoFrame` 保存的是我们平台自己持有的一份 RGB 图像数据。
4. `FrameConverter` 使用 `sws_scale` 把不同来源的像素格式统一转换成 RGB24。
5. `VideoPlayer` 负责显示适配，因为只有播放 demo 需要 `QImage`。
6. `VideoInputConfig` 统一描述本地文件和 RTSP 输入，为后续网络流接入做准备。

## 现在应该学什么

你现在不要急着继续堆功能，先带着下面几个问题读代码：

1. 为什么 `FFmpegDecoder::readFrame()` 里面既要调用 `av_read_frame()`，又要循环调用 `avcodec_receive_frame()`？
2. `av_packet_unref()` 和 `av_frame_unref()` 分别在什么时候调用？它们解决的是什么资源生命周期问题？
3. 为什么 `VideoFrame` 使用 `std::vector<std::uint8_t>` 保存图像数据，而不是直接保存 `AVFrame*`？
4. 为什么 `VideoPlayer::convertFrameToImage()` 里创建 `QImage` 后面还要调用 `.copy()`？
5. 如果 OpenCV DNN 或后续人脸特征模型需要 BGR、RGB planar，或者归一化后的 float 输入，这个模块应该怎么扩展？
6. 为什么现在只加 `VideoInputConfig`，而没有马上抽出 `IVideoSource` 虚接口？

## 当前已完成的小重构

当前已经增加了视频输入配置结构：

```text
VideoInputConfig
  sourceType: File / RTSP
  url: string
  openTimeoutMs: int
  readTimeoutMs: int
```

`FFmpegDecoder::open(QString)` 仍然保留，方便 Qt demo 调用；内部实际会转成 `VideoInputConfig::fromFile()`。

`FFmpegDecoder::open(VideoInputConfig)` 是后续接 RTSP、摄像头或其他视频源的统一入口。

## 后续重构方向

生产者-消费者队列已经作为模块 2 的第一版实现。你可以继续阅读 `docs/module-2-producer-consumer.md`。

等本地文件和 RTSP 两类输入都跑通后，再考虑抽出 `IVideoSource`：

```text
IVideoSource
  open(config)
  readFrame(VideoFrame*)
  close()
```
