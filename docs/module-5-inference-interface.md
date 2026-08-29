# 模块 5：推理接口与检测器边界

本模块的目标，是先把“视频帧如何进入检测器、检测结果如何返回”这个边界定义清楚。当前真实实现是 OpenCV DNN YOLO 人脸检测器。

## 为什么先做接口

真实检测会涉及：

- 模型文件路径。
- 输入尺寸。
- 置信度阈值。
- NMS 阈值。
- 类别数量。
- 推理后端。
- 错误诊断。

这些细节不应该扩散到 UI、播放队列或存储模块里。它们只需要知道：

```cpp
DetectionResults detect(const VideoFrame& frame);
```

因此 `IDetector` 是推理模块对外的稳定边界。

## 当前实现

关键文件：

- `modules/inference/include/inference/IDetector.h`
- `modules/inference/include/inference/YoloOpenCVDnnDetector.h`
- `modules/inference/src/YoloOpenCVDnnDetector.cpp`
- `modules/common/include/common/DetectionResult.h`

当前 `DetectorBackend` 主线为：

```cpp
DetectorBackend::OpenCVDnn
```

`DetectorConfig` 包含：

- `confidenceThreshold`
- `nmsThreshold`
- `detectEveryNFrames`
- `inputWidth`
- `inputHeight`
- `classCount`
- `maxDetections`
- `onnxPath`
- `labelsPath`

## 当前数据流

```text
VideoPlayer::inferenceLoop()
  |
  v
IDetector::detect(VideoFrame)
  |
  v
YoloOpenCVDnnDetector
  |
  v
DetectionResults
  |
  +--> VideoDisplayWidget 画框
  |
  +--> FaceRecognizer 身份匹配
  |
  +--> ResultManager 缓存统计
  |
  +--> SQLiteDetectionStorage 持久化
```

`VideoPlayer` 只持有 `IDetector`，不直接依赖 OpenCV DNN 的实现细节。后续如果替换为其他检测器，只要继续实现 `IDetector`，播放和 UI 链路就不需要大改。

## DetectionResult 的职责

`DetectionResult` 放在 `modules/common`，因为它会被多个模块同时使用：

- UI：绘制人脸框。
- 识别：根据人脸框裁剪特征。
- 存储：写入检测记录和人员绑定。
- 统计：统计检测数量和类别。
- 网络：发送检测结果。

当前字段包括：

- `sourceId`
- `frameIndex`
- `ptsMs`
- `trackId`
- `classId`
- `className`
- `confidence`
- `box`
- `face`

其中 `trackId` 由 `modules/tracking` 在检测后分配，用于表示短期连续轨迹；`face` 是 `FaceRecognitionResult`，用于承载自动识别结果。

## 工程边界

- `modules/inference` 只负责人脸检测框。
- `modules/recognition` 负责身份匹配。
- `modules/playback` 负责调度检测器，不负责解释模型输出。
- `apps/viewer` 负责展示参数和错误信息，不负责 OpenCV DNN forward。
- `modules/storage` 只保存结构化结果，不运行检测模型。

## 建议阅读的代码

1. `modules/inference/include/inference/IDetector.h`
2. `modules/inference/src/YoloOpenCVDnnDetector.cpp`
3. `modules/common/include/common/DetectionResult.h`
4. `modules/playback/src/VideoPlayer.cpp` 的 `initializeDetector()` 和 `inferenceLoop()`
5. `modules/recognition/src/FaceRecognizer.cpp` 的 `recognize()`

## 现在应该学什么

1. 为什么 `VideoPlayer` 持有 `IDetector`，而不是直接散落 OpenCV DNN 调用。
2. 为什么 `DetectionResult` 要放在 `common`。
3. 为什么检测框和识别结果要在数据结构上分层。
4. 为什么模型初始化错误应该通过 `lastError()` 返回给 UI。
5. 为什么后续替换人脸检测模型时，应尽量保持 `IDetector` 对外接口不变。
