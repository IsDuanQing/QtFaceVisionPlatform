# 模块 6：检测结果回传与 UI 画框

本模块的目标是把 `YoloOpenCVDnnDetector` 产生的人脸检测结果稳定显示到 Qt 界面上，并为后续识别、存储和历史查询提供正确的画面对应关系。

```text
InferenceQueue
  |
  v
IDetector::detect()
  |
  v
DetectionResults
  |
  v
VideoPlayer::detectionResultsReady / detectionFrameReady
  |
  v
MainWindow
  |
  v
VideoDisplayWidget::paintEvent()
```

## 为什么要做这个模块

人脸平台不是只在后台跑模型，还需要把检测结果稳定地展示、记录、发送出去。

模块 6 解决三个问题：

1. 推理结果如何从后台线程回到 Qt 主线程。
2. 人脸框如何按视频缩放比例正确画在图像上。
3. 图像、检测框和后续识别结果如何避免错帧。

## 当前实现

关键文件：

- `apps/viewer/VideoDisplayWidget.h`
- `apps/viewer/VideoDisplayWidget.cpp`
- `modules/playback/include/playback/VideoPlayer.h`
- `modules/playback/src/VideoPlayer.cpp`
- `apps/viewer/MainWindow.cpp`

## 关键设计

### 1. 推理线程不直接操作 UI

Qt 的界面对象必须在主线程中更新。

所以 `VideoPlayer::inferenceLoop()` 里拿到 `DetectionResults` 后，不直接访问 `MainWindow` 或控件，而是通过信号把结果交回 UI 层。

这样做的意义是：后台线程只负责计算，UI 线程只负责显示。

### 2. MainWindow 不负责画框

`MainWindow` 只做连接、状态管理和数据分发：

```text
frameReady              -> displayFrame()
detectionFrameReady     -> displayDetectionFrame()
detectionResultsReady   -> displayDetections()
```

真正的绘制逻辑放在 `VideoDisplayWidget`。

这样后续要增加不同颜色、识别姓名、置信度、轨迹或告警提示，都可以集中改这个控件，而不是把 `MainWindow` 写成一个大杂烩。

### 3. 检测框坐标需要缩放

`DetectionResult::box` 保存的是原始图像坐标。

Qt 窗口中的视频画面会按比例缩放，所以画框前要做坐标映射：

```text
原图坐标 x/y/width/height
  |
  v
根据当前显示区域 targetRect 计算 scaleX / scaleY
  |
  v
显示坐标
```

这部分逻辑在：

```cpp
VideoDisplayWidget::scaledDetectionRect()
```

### 4. Detection Preview 保证结果对应

播放线程和推理线程速度不一致时，单独缓存检测结果容易出现错帧。

当前提供两种模式：

- Playback Preview：优先流畅播放，只在帧号严格对应时画框。
- Detection Preview：推理完成后把当前帧图像和检测结果一起发送，优先保证图像和框严格对应。

验证人脸识别时建议使用 Detection Preview，因为识别依赖当前帧的人脸裁剪。

## 建议阅读顺序

1. `modules/common/include/common/DetectionResult.h`
2. `modules/playback/src/VideoPlayer.cpp` 的 `inferenceLoop()`
3. `apps/viewer/MainWindow.cpp` 的信号连接和显示函数
4. `apps/viewer/VideoDisplayWidget.cpp`
5. `docs/module19-detection-preview.md`

读 `VideoDisplayWidget.cpp` 时重点看三个函数：

- `paintEvent()`：Qt 自绘入口。
- `imageTargetRect()`：计算视频画面在控件里的真实显示区域。
- `scaledDetectionRect()`：把原图检测框坐标转换成屏幕坐标。

## 现在应该学什么

1. 为什么推理线程不能直接调用 Qt 控件？
2. 为什么画框逻辑不放在 `MainWindow` 里？
3. 为什么检测框必须基于原图坐标保存，而不是保存显示坐标？
4. 为什么人脸识别比普通预览更需要结果严格对齐？
5. 后续如果显示姓名、相似度或告警颜色，应该改哪个模块？

## 后续演进

模块 7 已经把检测结果进入 `ResultManager`，模块 8 已经把检测记录和识别结果写入 SQLite。

当前结果已经沉淀成更清晰的数据流：

```text
DetectionResults
  |
  v
ResultManager
  |
  |-- UI 展示
  |-- SQLite 存储
  |-- 人脸识别关联
  |-- TCP/JSON 发送
  |-- 统计分析
```
