# 模块 12：检测参数配置界面

模块 12 的目标是把检测参数从硬编码和环境变量中抽出来，让 Qt 客户端
可以编辑当前检测配置。这样打开 MP4、RTSP 或图片文件夹时，系统都会
使用同一份 `DetectorConfig` 初始化检测器。

## 为什么要做配置对象

模块 10 中，TensorRT 和 YOLO 参数已经进入工程：

- 检测后端
- 置信度阈值
- NMS 阈值
- 输入尺寸
- 类别数量
- Engine 路径
- labels 路径

如果这些参数长期散落在环境变量或 `VideoPlayer::initializeDetector()`
里，后续会出现三个问题：

1. UI 无法明确展示当前运行参数。
2. 同一套参数很难复用于文件、RTSP 和图片序列。
3. 后续增加配置文件或网络配置接口时，需要到处改代码。

因此本模块让 `VideoPlayer` 持有一份 `DetectorConfig`，UI 在启动输入源
前把当前控件值收集进去。

## 当前实现范围

`MainWindow` 增加了检测参数面板：

- Backend：`Mock` / `TensorRT`
- Confidence
- NMS
- Max detections
- Detect every N frames
- YOLO input width / height
- Class count
- Mock delay
- Image FPS
- ONNX / Engine / Labels 路径

`VideoPlayer` 增加：

- `setDetectorConfig()`
- `applyDetectorConfig()`
- `detectorConfig()`

打开输入源前，UI 调用：

```text
collectDetectorConfig()
  |
  v
VideoPlayer::setDetectorConfig()
  |
  v
VideoPlayer::open/openRtsp/openImageSequence()
  |
  v
VideoPlayer::initializeDetector()
```

## 生效时机

打开输入源前，UI 会自动应用当前配置。

视频播放过程中，修改检测参数后需要点击 `Apply Parameters`。这个按钮会
通过 `VideoPlayer::applyDetectorConfig()` 重新初始化检测器，并清空当前
画面上的检测框和统计值。

注意：`Image FPS` 属于图片序列输入源参数，不属于检测器参数。修改它之后
需要重新打开图片目录。

## 工程边界

`MainWindow` 负责：

- 创建参数控件
- 收集控件值
- 选择模型文件路径
- 在打开输入源前应用配置
- 在点击 `Apply Parameters` 时热应用检测器配置

`VideoPlayer` 负责：

- 保存 `DetectorConfig`
- 根据配置创建 `MockDetector` 或 `YoloTensorRTDetector`
- 初始化检测器

`IDetector` 实现负责：

- 理解配置中与自己相关的参数
- 返回初始化错误
- 执行检测

## 当前限制

- 检测器参数可以通过 `Apply Parameters` 运行中应用。
- 图片序列 FPS 仍需要重新打开图片目录后生效。
- TensorRT 后端在 MinGW Qt demo 下不建议真实运行。
- 当前没有做参数合法性跨字段校验，例如 labels 类别数和模型输出类别数。

模块 13 已经继续完成检测参数持久化与启动恢复。

## Qt Creator 手工验收

1. 使用默认 `Mock` 后端。
2. 修改 `Image FPS`，点击 `Open Images` 打开图片目录。
3. 观察 FPS 显示是否跟设置一致。
4. 修改 `Mock ms` 或 `Every N`，点击 `Apply Parameters`。
5. 确认画面、Mock 检测框和历史记录仍正常。
6. 切换到 `TensorRT` 后端时，当前 MinGW 构建应能给出清晰错误，而不是崩溃。

## 学习顺序

1. 读 `DetectorConfig`，理解每个字段属于哪个模块。
2. 读 `MainWindow::collectDetectorConfig()`，理解 UI 如何生成配置对象。
3. 读 `VideoPlayer::initializeDetector()`，理解检测器选择和初始化。
4. 读 `VideoPlayer::applyDetectorConfig()`，理解为什么热应用检测器需要加锁。
