# 模块 12：检测参数配置界面

模块 12 的目标是把 OpenCV DNN 人脸检测参数从硬编码中抽出来，让 Qt 客户端可以查看、修改和热应用当前检测配置。

当前主线只保留 `OpenCV DNN` 检测后端。TensorRT 和 Mock detector 属于历史文档内容，不再作为当前 Parameters 页签的主要路径。

## 为什么要做配置对象

人脸检测器需要一组稳定参数：

- 置信度阈值。
- NMS 阈值。
- 最大检测数量。
- 检测抽帧间隔。
- YOLO 输入宽高。
- 类别数量。
- ONNX 模型路径。
- labels 文件路径。

如果这些参数散落在 `MainWindow` 或 `VideoPlayer` 内部，后续会出现三个问题：

1. UI 无法明确展示当前运行参数。
2. 文件、RTSP 和远程任务很难复用同一份配置。
3. 识别参考图裁剪时无法保证和视频检测使用同一套检测器参数。

因此本模块让 `VideoPlayer` 持有一份 `DetectorConfig`，UI 在打开输入源或点击 Apply 时把当前控件值收集进去。

## 当前实现范围

`MainWindow` 的 Parameters 区域包含：

- `Confidence`
- `NMS`
- `Max`
- `Every N`
- `Input W`
- `Input H`
- `Classes`
- `ONNX`
- `Labels`
- `Apply`
- `Face`
- `Clear`
- `Reset`

`Face` 预设会设置：

```text
Backend: OpenCV DNN
ONNX:    models/yolov8-face/face.onnx
Labels:  models/yolov8-face/labels.txt
Input:   640 x 640
Classes: 1
Conf:    0.25
NMS:     0.45
Max:     300
```

## 数据流

打开输入源前：

```text
collectDetectorConfig()
  |
  v
VideoPlayer::setDetectorConfig()
  |
  v
VideoPlayer::open() / openRtsp()
  |
  v
VideoPlayer::initializeDetector()
```

运行中点击 `Apply`：

```text
collectDetectorConfig()
  |
  v
VideoPlayer::applyDetectorConfig()
  |
  v
重新初始化 YoloOpenCVDnnDetector
  |
  v
清空当前叠加框和统计状态
```

添加人员参考图时：

```text
collectDetectorConfig()
  |
  v
临时 YoloOpenCVDnnDetector
  |
  v
裁剪参考图中的人脸
  |
  v
FaceRecognizer::extractReferenceFeatures()
```

参考图裁剪复用同一份检测参数，是为了保证“参考图的人脸框”和“视频流中的人脸框”来自同一种检测逻辑。

## 工程边界

`MainWindow` 负责：

- 创建参数控件。
- 收集控件值。
- 选择模型文件路径。
- 应用 `Face` 预设。
- 在打开输入源前应用配置。
- 在点击 `Apply` 时热应用检测器配置。

`VideoPlayer` 负责：

- 保存 `DetectorConfig`。
- 根据配置创建 `YoloOpenCVDnnDetector`。
- 初始化检测器。
- 在推理线程中按 `Every N` 做抽帧。

`YoloOpenCVDnnDetector` 负责：

- 校验 ONNX 和 labels 文件。
- 校验输入尺寸、类别数和最大检测数。
- 执行 OpenCV DNN forward。
- 调用 YOLO 后处理生成 `DetectionResults`。

## 当前限制

- 当前主线只有 `OpenCV DNN` 后端。
- 输入尺寸必须和 ONNX 模型兼容。
- labels 文件和 ONNX 必须来自同一套模型。
- 运行中点击 `Apply` 会重新初始化检测器，短时间内可能没有新检测结果。
- 阈值仍需要结合真实视频和参考图调参。

## Qt Creator 手工验收

1. 启动程序。
2. 点击 Parameters 中的 `Face` 预设。
3. 确认 `Input W/H` 为 `640 x 640`，`Classes` 为 `1`。
4. 点击 `Apply`。
5. 打开人脸测试视频或 RTSP。
6. 切换 Detection Preview，确认人脸框稳定。
7. 修改 `Confidence` 或 `Every N`，点击 `Apply`，观察检测数量和刷新节奏变化。
8. 添加 Faces 参考图，确认参考图裁剪和 Recognition 状态正常。

## 学习顺序

1. 读 `modules/inference/include/inference/IDetector.h` 中的 `DetectorConfig`。
2. 读 `MainWindow::collectDetectorConfig()`，理解 UI 如何生成配置对象。
3. 读 `MainWindow::applyFaceDetectorPreset()`，理解默认人脸模型参数。
4. 读 `VideoPlayer::initializeDetector()`，理解检测器创建和初始化。
5. 读 `YoloOpenCVDnnDetector::initialize()`，理解模型路径和输入形状校验。
