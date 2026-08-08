# 模块 10：YOLO TensorRT 推理准备

模块 1 到模块 9 已经把视频输入、异步推理队列、结果显示、SQLite
存储和历史查询打通。模块 10 开始替换 `MockDetector`，但必须先把
YOLO 模型输入输出的约定写成独立代码，避免把 TensorRT、图像处理和
业务结果混进 `VideoPlayer`。

## 当前完成范围

本阶段完成的是 TensorRT 接入前的基础层：

- `DetectorConfig` 增加模型输入尺寸、类别数、置信度、NMS 阈值、
  Engine 路径和标签路径。
- `YoloPreprocessor` 将平台内部的 RGB24 `VideoFrame` 转为
  `float` 类型的 RGB/CHW 输入。
- 前处理使用 letterbox 缩放，并保存缩放比例和边距。
- `YoloPostprocessor` 支持常见 YOLO 输出布局，完成置信度筛选、
  分类和按类别 NMS。
- `YoloPreprocessor::restoreBox()` 将模型坐标还原到原始视频帧。
- `YoloTensorRTDetector` 已经实现 `IDetector` 接口和标签加载校验。
- `VideoPlayer` 默认继续使用 `MockDetector`；只有显式选择 TensorRT
  后端时才尝试创建 `YoloTensorRTDetector`。

当前还没有执行真正的 CUDA/TensorRT Engine。这个部分必须以实际 ONNX
模型的输入输出信息为准，不能根据不同 YOLO 版本盲目猜测。

## 数据流

```text
VideoFrame (RGB24)
  |
  v
YoloPreprocessor
  |  RGB -> letterbox -> float CHW
  v
TensorRT Engine
  |  output tensor
  v
YoloPostprocessor
  |  threshold -> class selection -> NMS -> restoreBox
  v
DetectionResults
```

`DetectionResults` 的输出格式没有改变，因此模块 6 的画框、模块 8
的 SQLite 写入和模块 9 的历史查询都不需要因接入真实模型而重写。

## 前处理

当前视频模块输出 `RGB24`。YOLO 前处理的第一版约定为：

1. 等比例缩放到模型输入区域。
2. 其余位置填充 `114 / 255`。
3. 将像素值归一化到 `[0, 1]`。
4. 从 HWC 排列转换为 CHW 排列。

假设原图是 `1920 x 1080`，模型输入是 `640 x 640`：

```text
scale = min(640 / 1920, 640 / 1080)
resized = 640 x 360
padding = top 140, bottom 140
```

模型输出的框先处在 `640 x 640` 坐标系中。必须减去 padding，再除以
`scale`，才能映射回原始帧。这里处理错误时，通常会出现检测框整体
偏移或大小不正确。

## 后处理

第一版支持两种常见输出形状：

- `[1, N, 4 + C]` 或 `[1, N, 5 + C]`
- `[1, 4 + C, N]` 或 `[1, 5 + C, N]`

其中：

- 前 4 个值约定为 `centerX, centerY, width, height`。
- `5 + C` 形式包含 objectness，最终置信度为
  `objectness * classScore`。
- `4 + C` 形式不包含 objectness，最终置信度为最大类别分数。

不同导出脚本可能输出已经做过 NMS 的 Tensor，或使用 `xyxy` 坐标。
拿到模型后必须先确认实际输出，不能直接套用当前约定。

## 如何选择后端

默认行为：

```text
MockDetector
```

尝试 TensorRT 时，需要设置：

```text
IVP_DETECTOR_BACKEND=tensorrt
IVP_YOLO_ENGINE=/absolute/path/model.engine
IVP_YOLO_LABELS=/absolute/path/labels.txt
IVP_YOLO_CLASS_COUNT=3
IVP_YOLO_INPUT_WIDTH=640
IVP_YOLO_INPUT_HEIGHT=640
```

这里的环境变量只是当前没有参数设置界面时的临时入口。后续应该改为
应用配置文件或模型管理界面，而不是长期依赖环境变量。

## 下一阶段需要的外部资源

继续实现真正的 TensorRT Engine 执行前，请准备：

1. 训练完成并导出的 YOLO ONNX 文件。
2. 与训练类别顺序完全一致的 `labels.txt`，每行一个类别。
3. ONNX 导出命令、YOLO 版本和模型输入尺寸。
4. 用于验收的一张或一段已知缺陷样本，以及期望类别和目标框大致位置。
5. 目标部署设备上的 TensorRT、CUDA、GPU 型号和版本信息。

`.engine` 与 GPU 架构、TensorRT 和 CUDA 版本有关，通常不应作为跨环境
复用的通用模型文件。推荐提交 ONNX 和导出说明，在目标设备上构建 Engine。

## 学习顺序

1. 先读 `YoloPreprocessor`，手算一个 `16:9` 画面如何变成 `640 x 640`。
2. 再读 `YoloPostprocessor`，理解 `[N, 4 + C]` 与 `[4 + C, N]` 的
   内存访问差异。
3. 阅读 IoU 和 class-aware NMS。
4. 学习 TensorRT 中 Runtime、Engine、ExecutionContext、CUDA Stream
   和 Device Buffer 的职责。
5. 拿到 ONNX 后，用 Netron 或 ONNX Runtime 确认真实输入输出名称和形状。

## 当前测试

`tests/inference/YoloPrePostprocessorTest.cpp` 覆盖：

- RGB24 帧生成 CHW 输入。
- 4:3 原图进入正方形模型时的上下 padding。
- YOLO 候选框的类别与帧元数据回填。
- 两个重叠候选框经 NMS 后只保留高置信度结果。
