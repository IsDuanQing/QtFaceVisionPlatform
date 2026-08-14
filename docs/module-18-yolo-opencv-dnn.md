# 模块 18：YoloOpenCVDnnDetector 真实 YOLO 检测闭环

## 模块目标

模块 18 的目标，是在不依赖 TensorRT 的情况下，先用 OpenCV DNN 加载
`defect.onnx`，让平台真正产生 YOLO 检测框。

这一模块不是为了追求最高性能，而是为了验证完整真实检测链路：

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
UI 画框 / SQLite / 导出 / TCP 发送
```

## 为什么先做 OpenCV DNN

TensorRT 是工业部署里很重要的性能优化方案，但它会同时引入 CUDA、
Engine、动态库、ABI 和显存生命周期问题。

OpenCV DNN 的价值是先把“模型是否可用、前处理是否正确、后处理是否正确、
类别顺序是否正确、框坐标是否正确”这些算法闭环问题验证清楚。

等真实检测框稳定之后，再切 TensorRT，问题范围就会小很多。

## 当前完成范围

已新增：

- `YoloOpenCVDnnDetector`
- `DetectorBackend::OpenCVDnn`
- Qt 界面后端选项 `OpenCV DNN`
- qmake 可选 OpenCV DNN 构建开关
- CMake 可选 OpenCV DNN 构建开关
- 远程控制协议支持 `detector_backend=opencv_dnn`
- 默认未启用 OpenCV DNN 时的清晰错误提示

`YoloOpenCVDnnDetector` 复用已有的：

- `YoloPreprocessor`
- `YoloPostprocessor`
- `DetectorConfig`
- `IDetector`

这样播放模块不需要知道 OpenCV DNN 的细节，UI 也只是在选择不同 detector。

## 构建方式

默认构建不启用 OpenCV DNN，避免没有 OpenCV 开发库时影响普通 demo。

启用方式：

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

1. 启用 OpenCV DNN 构建并重新构建项目。
2. 打开 Qt demo。
3. 在 `Detection Parameters` 中选择 `OpenCV DNN`。
4. 确认路径：

```text
ONNX:   D:/IndustrialVisionPlatform/models/yolo11l/defect.onnx
Labels: D:/IndustrialVisionPlatform/models/yolo11l/labels.txt
```

5. `Input W` 和 `Input H` 保持 `1088`。
6. `Classes` 可以填 `0`，程序会从 labels 文件推导类别数量。
7. 打开图片文件夹、视频文件或 RTSP 流。
8. 观察 UI 是否出现真实检测框。

## 远程任务测试

`configure_task` 可以这样指定真实 ONNX 检测：

```json
{
  "type": "configure_task",
  "task_id": "task-opencv-001",
  "source_type": "image_sequence",
  "source_url": "F:/DataSet/guangdong1_round1_testA_20190818",
  "auto_start": true,
  "detector_backend": "opencv_dnn",
  "onnx_path": "D:/IndustrialVisionPlatform/models/yolo11l/defect.onnx",
  "labels_path": "D:/IndustrialVisionPlatform/models/yolo11l/labels.txt",
  "input_width": 1088,
  "input_height": 1088,
  "class_count": 0,
  "confidence_threshold": 0.5,
  "nms_threshold": 0.45
}
```

## 当前限制

- 当前 OpenCV DNN 后端默认使用 CPU。
- 还没有做 OpenCV CUDA backend 配置。
- 只取第一个输出 Tensor。
- 当前后处理按 YOLO 常见输出解析，模型输出仍需保持 `[1, 24, 24276]`
  或等价布局。
- OpenCV DNN 推理速度通常不如 TensorRT。

## 学习重点

本模块你需要重点学习：

- ONNX 是模型交换格式，不是推理引擎。
- OpenCV DNN 如何加载 ONNX 并执行 forward。
- 为什么真实推理后端也应该实现 `IDetector`。
- 为什么前处理和后处理要独立于具体推理引擎。
- 为什么先验证真实框，再做 TensorRT 加速。
