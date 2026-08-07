# 模块 6：检测结果回传与 UI 画框

本模块的目标是把模块 5 里 `MockDetector` 产生的检测结果真正显示到 Qt 界面上。

不要急着接 TensorRT。真实推理之前，先要确认这条工程链路是通的：

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
VideoPlayer::detectionResultsReady
  |
  v
MainWindow
  |
  v
VideoDisplayWidget::paintEvent()
```

## 为什么要做这个模块

工业视觉平台最终不是只在后台跑模型，而是要把检测结果稳定地展示、记录、发送出去。

如果推理线程只能打印日志，说明算法链路还没有和业务界面闭环。模块 6 先解决两个问题：

1. 推理结果如何从后台线程回到 Qt 主线程。
2. 检测框如何按视频缩放比例正确画在图像上。

这一步完成后，后面把 `MockDetector` 换成 `YoloTensorRTDetector` 时，UI 绘制逻辑不需要大改。

## 当前实现

新增文件：

- `apps/viewer/VideoDisplayWidget.h`
- `apps/viewer/VideoDisplayWidget.cpp`

修改文件：

- `modules/playback/include/playback/VideoPlayer.h`
- `modules/playback/src/VideoPlayer.cpp`
- `apps/viewer/MainWindow.h`
- `apps/viewer/MainWindow.cpp`
- `IndustrialVisionPlatform.pro`
- `apps/viewer/CMakeLists.txt`

## 关键设计

### 1. 推理线程不直接操作 UI

Qt 的界面对象必须在主线程中更新。

所以 `VideoPlayer::inferenceLoop()` 里拿到 `DetectionResults` 后，不直接访问 `MainWindow` 或控件，而是通过 `QMetaObject::invokeMethod()` 投递回 `VideoPlayer` 所在线程，再发出信号：

```cpp
emit detectionResultsReady(results);
```

这样做的意义是：后台线程只负责计算，UI 线程只负责显示。

### 2. MainWindow 不负责画框

`MainWindow` 现在只做连接和状态管理：

```text
frameReady              -> VideoDisplayWidget::setFrame()
detectionResultsReady   -> VideoDisplayWidget::setDetections()
```

真正的绘制逻辑放在 `VideoDisplayWidget`。

这样后续要增加告警颜色、类别过滤、置信度显示、轨迹绘制，都可以集中改这个控件，而不是把 `MainWindow` 写成一个大杂烩。

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

### 4. 用 frameIndex 过滤过旧结果

推理线程和显示线程速度不一定一致。

当前用 `frameIndex` 判断检测结果是否还适合画在当前帧上：

- 结果来自未来帧：暂时不画。
- 结果太旧：不画。
- 结果和当前帧接近：画框。

这不是最终算法同步方案，但足够支撑当前 demo。后续真实推理接入后，可以继续升级为按时间戳、任务 ID 或多路 sourceId 对齐。

## 建议阅读顺序

1. 先读 `modules/common/include/common/DetectionResult.h`
2. 再读 `modules/playback/src/VideoPlayer.cpp` 里的 `inferenceLoop()`
3. 再读 `apps/viewer/MainWindow.cpp` 里的 `connectSignals()`
4. 最后读 `apps/viewer/VideoDisplayWidget.cpp`

读 `VideoDisplayWidget.cpp` 时重点看三个函数：

- `paintEvent()`：Qt 自绘入口。
- `imageTargetRect()`：计算视频画面在控件里的真实显示区域。
- `scaledDetectionRect()`：把原图检测框坐标转换成屏幕坐标。

## 现在应该学什么

带着这些问题读代码：

1. 为什么推理线程不能直接调用 Qt 控件？
2. 为什么画框逻辑不放在 `MainWindow` 里？
3. 为什么检测框必须基于原图坐标保存，而不是保存显示坐标？
4. 如果推理速度比显示速度慢，检测框可能会出现什么延迟？
5. 后续真实 YOLO 输出的归一化坐标应该在哪里转换成像素坐标？

## 下一步建议

下一步可以开始模块 7：检测结果管理。

建议先不要马上做 TensorRT。先把检测结果沉淀成一个清晰的数据流：

```text
DetectionResults
  |
  v
ResultManager
  |
  |-- UI 展示
  |-- 日志输出
  |-- 后续 SQLite 存储
  |-- 后续 TCP/JSON 发送
```

这样后面接入真实模型后，检测结果不会只停留在界面上，而是能进入存储、网络通信和统计分析链路。
