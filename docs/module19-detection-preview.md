# 模块 19：Detection Preview 模式

## 目标

解决播放线程和推理线程速度不同步时，检测框与视频画面错帧、闪烁、漂移的问题。

在人脸平台中，这个模式不只是为了“框看起来对”，还会影响后续人脸识别：识别使用的裁剪区域必须来自当前帧，否则人员身份、相似度和历史记录都会出现错位。

## 两种预览模式

### Playback Preview

- 使用 `frameReady` 显示原始播放帧。
- 检测结果通过 `frameIndex` 缓存后叠加。
- 只有检测结果的 `frameIndex` 与当前画面的 `frameIndex` 完全相同时才允许画框。
- 不再把上一帧或附近帧的框复用到当前画面，避免图片序列出现“上一张图的框跑到下一张图上”。
- 优点是播放更流畅。
- 缺点是推理较慢时，当前帧可能暂时没有框。

### Detection Preview

- 推理线程完成一帧检测后，同时发送：
  - 当前帧图像；
  - 当前帧检测结果；
  - `frameIndex`；
  - `ptsMs`。
- UI 通过 `setDetectionFrame()` 一次性显示图像和检测框。
- 这一模式优先保证画面与检测结果严格对应。
- 播放速度由推理速度决定，可能低于原视频 FPS。
- 验证人脸识别、人员绑定和参考图质量时，建议优先使用该模式。

## 代码阅读顺序

1. `modules/playback/include/playback/VideoPlayer.h`
   - 查看 `detectionFrameReady` 信号。
2. `modules/playback/src/VideoPlayer.cpp`
   - 查看 `inferenceLoop()` 如何生成同步检测帧。
   - 查看 `producerLoop()` 如何统一 `ptsMs` 时间基准。
3. `apps/viewer/MainWindow.cpp`
   - 查看 `displayFrame()` 和 `displayDetectionFrame()` 的模式分流。
4. `apps/viewer/VideoDisplayWidget.cpp`
   - 查看 `setDetectionFrame()` 为什么不再依赖异步缓存。

## Qt Creator 测试步骤

1. 使用 Qt6 + `C:\msys64\ucrt64\bin\g++.exe` 构建。
2. 选择 `OpenCV DNN` 后点击 `Face` 预设。
3. 点击顶部 `Open`，选择人脸测试视频。
4. 将 `Preview` 切换为 `Detection Preview`。
5. 观察：
   - `Shown` 和 `Infer` 帧号应接近或相同；
   - `Lag` 应接近 `0`；
   - 画面刷新速度可能低于原视频；
   - 检测框应与当前画面中的人脸对应；
   - History 中自动识别到的人员应与画面中的人员对应。
6. 再切换回 `Playback Preview`，比较两种模式的流畅度和框延迟。
   - 即使切回该模式，也不应再看到上一帧的框出现在下一帧上；
   - 推理尚未完成的当前帧可以暂时没有框。

## 本模块需要学习

- Qt 跨线程信号槽；
- 生产者-消费者模型中的延迟；
- `frameIndex` 与 `ptsMs` 的作用；
- “流畅预览”和“结果严格一致”的工程取舍；
- 为什么人脸识别必须保证图像、检测框、特征裁剪和历史记录严格对应。
