# 模块 5：推理接口与 MockDetector

本模块的目标，不是马上接 TensorRT，而是先把推理模块的输入、输出和替换边界定义清楚。

## 为什么先做接口

真实 YOLO / TensorRT 推理会涉及：

1. 模型加载。
2. CUDA 显存管理。
3. 图像预处理。
4. Engine 执行。
5. 后处理和 NMS。

如果这些内容一开始就直接写进播放线程，工程会很快变乱。

所以模块 5 先定义一个稳定接口：

```text
InferenceQueue
  |
  v
IDetector::detect(VideoFrame)
  |
  v
DetectionResults
```

后续 `MockDetector` 可以替换为 `YoloTensorRTDetector`，但 `VideoPlayer` 和 `FrameDispatcher` 不需要大改。

## 当前实现

新增公共结果结构：

- `modules/common/include/common/DetectionResult.h`

新增推理模块文件：

- `modules/inference/include/inference/IDetector.h`
- `modules/inference/include/inference/MockDetector.h`
- `modules/inference/src/MockDetector.cpp`

当前核心类型：

- `BoundingBox`
- `DetectionResult`
- `DetectionResults`
- `DetectorConfig`
- `IDetector`
- `MockDetector`

## 当前数据流

```text
FFmpegDecoder
  |
  v
FrameDispatcher
  |
  v
InferenceQueue
  |
  v
VideoPlayer::inferenceLoop()
  |
  v
IDetector::detect()
  |
  v
DetectionResults
```

当前 `VideoPlayer` 持有的是：

```cpp
std::unique_ptr<ivp::IDetector> detector_;
```

构造时先使用：

```cpp
std::make_unique<ivp::MockDetector>()
```

后续接 TensorRT 时，只需要把具体实现替换为真实检测器。

## MockDetector 做了什么

`MockDetector` 用来验证推理链路，不代表真实算法。

它现在做三件事：

1. 根据 `DetectorConfig::simulatedDelayMs` sleep，模拟推理耗时。
2. 根据 `DetectorConfig::detectEveryNFrames` 抽帧生成模拟结果。
3. 返回一个移动位置的 `mock_defect` 检测框。

日志中可以看到类似信息：

```text
MockDetector consumed frames: 30 fps: 24.8 detections: 10 latest frame: 92 pts(ms): 3680
```

这说明推理线程已经独立消费 `InferenceQueue`。

## 为什么 DetectionResult 放在 common

检测结果后面会被多个模块使用：

1. Qt UI：绘制缺陷框。
2. Network：发送 JSON 给客户端。
3. Storage：写入 SQLite。
4. Statistics：统计缺陷数量和类别。

所以它不应该只属于 `modules/inference`。

当前放在：

```text
modules/common/include/common/DetectionResult.h
```

## 建议阅读的代码

- `modules/common/include/common/DetectionResult.h`
- `modules/inference/include/inference/IDetector.h`
- `modules/inference/include/inference/MockDetector.h`
- `modules/inference/src/MockDetector.cpp`
- `modules/playback/src/VideoPlayer.cpp`

## 现在应该学什么

带着这些问题读代码：

1. 为什么 `VideoPlayer` 持有 `IDetector`，而不是直接持有 `MockDetector`？
2. 为什么 `DetectionResult` 不放在 `modules/inference`？
3. 为什么 `detect()` 输入是 `const VideoFrame&`？
4. 为什么推理线程可以慢一点，但不能阻塞视频读取线程？
5. `DetectorConfig` 以后可以扩展哪些 TensorRT 参数？

## 下一步重构方向

下一步建议做模块 6：检测结果回传与 UI 画框。

目标：

```text
DetectionResults
  |
  v
VideoPlayer signal
  |
  v
MainWindow
  |
  v
video frame + defect boxes
```

先把 MockDetector 的框画到 Qt 界面上，再进入真正的 TensorRT 推理。
