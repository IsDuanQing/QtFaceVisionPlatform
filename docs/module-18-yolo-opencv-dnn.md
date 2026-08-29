# 模块 18：OpenCV DNN YOLO 人脸检测闭环

## 模块目标

模块 18 的目标，是在不依赖 TensorRT 的情况下，使用 OpenCV DNN 加载 YOLO ONNX 人脸检测模型，让平台稳定产生真实人脸框。

```text
VideoFrame
  |
  v
YoloPreprocessor
  |
  v
OpenCV DNN ONNX forward
  |
  v
YoloPostprocessor
  |
  v
DetectionResults
  |
  v
UI 画框 / SQLite / 人脸识别 / TCP 发送
```

OpenCV DNN 的价值不是追求最终性能，而是先把模型路径、输入尺寸、类别数、前处理、后处理和框坐标验证清楚。真实检测框稳定后，识别、存储和事件链路才有可靠输入。

## 当前完成范围

已新增：

- `YoloOpenCVDnnDetector`
- `DetectorBackend::OpenCVDnn`
- Qt 界面后端选项 `OpenCV DNN`
- qmake / CMake OpenCV DNN 构建开关
- 远程控制协议可以下发 OpenCV DNN 所需的模型路径、输入尺寸和阈值
- 模型文件、标签文件、输入尺寸和类别数校验
- OpenCV DNN 初始化阶段的模型 / 输入形状验证

`YoloOpenCVDnnDetector` 复用已有的：

- `YoloPreprocessor`
- `YoloPostprocessor`
- `DetectorConfig`
- `IDetector`

这样播放模块和 UI 不需要知道 OpenCV DNN 的内部细节。

## 当前默认人脸模型

Faces 主线使用：

```text
ONNX:   models/yolov8-face/face.onnx
Labels: models/yolov8-face/labels.txt
Input:  640 x 640
Class:  1
```

Qt 界面中点击 `Face` 预设按钮会自动设置这些参数。

如果你从旧版本升级，配置里可能还残留 `models/yolo11l/defect.onnx` 或 `1088 x 1088`。当前设置存储已经会尽量迁移旧路径，但手动测试时仍建议先点击 `Face` 预设，避免旧参数进入 OpenCV DNN。

## 构建方式

默认构建不启用 OpenCV DNN，避免没有 OpenCV 开发库时影响普通 demo。

qmake 启用方式：

```text
qmake "DEFINES+=IVP_ENABLE_OPENCV_DNN" ^
  "MSYS2_PREFIX=C:/msys64/ucrt64"
```

如果你的 OpenCV 库名不同，可以显式传入：

```text
"OPENCV_LIBS=-lopencv_dnn -lopencv_imgproc -lopencv_core"
```

如果你机器上装的是 OpenCV 5，当前脚本会优先识别 `include/opencv5`。

Qt Creator 中可以在 qmake 参数里填写同样的内容。

## Qt 界面测试方式

1. 启用 OpenCV DNN 构建。
2. 打开 Qt 客户端。
3. 在 Parameters 中选择 `OpenCV DNN`。
4. 点击 `Face` 预设。
5. 确认参数：

```text
ONNX:   D:/QtFaceVisionPlatform/models/yolov8-face/face.onnx
Labels: D:/QtFaceVisionPlatform/models/yolov8-face/labels.txt
Input W: 640
Input H: 640
Classes: 1
```

6. 打开本地视频或 RTSP 流。
7. 切换到 Detection Preview。
8. 观察 UI 是否出现稳定的人脸检测框。

## 远程任务测试

`configure_task` 可以这样指定 OpenCV DNN 人脸检测：

```json
{
  "type": "configure_task",
  "task_id": "task-face-opencv-001",
  "source_type": "file",
  "source_url": "D:/QtFaceVisionPlatform/testdata/face-test.mp4",
  "auto_start": true,
  "onnx_path": "D:/QtFaceVisionPlatform/models/yolov8-face/face.onnx",
  "labels_path": "D:/QtFaceVisionPlatform/models/yolov8-face/labels.txt",
  "input_width": 640,
  "input_height": 640,
  "class_count": 1,
  "confidence_threshold": 0.5,
  "nms_threshold": 0.45
}
```

## OpenCV DNN reshape 报错排查

如果出现类似错误：

```text
OpenCV DNN YOLO inference failed:
reshape layer ... outTotal == inpTotal
```

优先检查：

1. 是否使用了人脸模型对应的 `640 x 640` 输入。
2. 是否仍然残留旧缺陷模型的 `1088 x 1088` 参数。
3. ONNX 路径和 labels 路径是否来自同一套模型。
4. `Classes` 是否设置为 `1`，或让程序从 labels 文件推导。
5. OpenCV 版本是否能正确加载该 ONNX。

当前代码在 `YoloOpenCVDnnDetector::initialize()` 中会提前做输入形状验证。这样不兼容的 ONNX 或输入尺寸会尽量在启动检测器时暴露，而不是等到推理线程运行一段时间后才崩溃。

## 当前限制

- 当前 OpenCV DNN 后端默认使用 CPU。
- 还没有做 OpenCV CUDA backend 配置。
- 当前只取第一个输出 Tensor。
- 后处理按 YOLO 常见输出解析，模型输出仍需保持兼容布局。
- OpenCV DNN 推理速度通常不如 TensorRT。

## 学习重点

本模块你需要重点学习：

- ONNX 是模型交换格式，不是推理引擎。
- OpenCV DNN 如何加载 ONNX 并执行 forward。
- 为什么真实推理后端也应该实现 `IDetector`。
- 为什么前处理和后处理要独立于具体推理引擎。
- 为什么检测器只负责人脸框，身份匹配要放在 `modules/recognition`。
