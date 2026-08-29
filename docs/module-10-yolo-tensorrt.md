# 模块 10：YOLO TensorRT 推理准备

> 历史文档：当前主线已经切到人脸检测 / 识别平台。本文保留为 TensorRT 技术储备参考，不作为当前 Parameters 页签或默认运行路径。

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
- 程序会优先读取环境变量中的模型路径；如果没有设置，则尝试使用
  `models/yolo11l` 目录下的默认资源。
- 新增 `TensorRTEngine`，负责 TensorRT Engine 反序列化、I/O Tensor
  元信息读取、CUDA Buffer 管理、CUDA Stream 和 `enqueueV3` 执行。
- `YoloTensorRTDetector::detect()` 已串起
  `YoloPreprocessor -> TensorRTEngine -> YoloPostprocessor`。

当前真实 TensorRT 执行代码使用条件编译保护。普通 Qt Demo 构建不会
链接 TensorRT；只有定义 `IVP_ENABLE_TENSORRT` 并链接 `nvinfer/cudart`
后，才会启用 Engine 执行。

## 当前本地模型资源

当前本地已放置：

```text
models/yolo11l/defect.onnx
models/yolo11l/labels.txt
```

`labels.txt` 当前包含 20 个类别：

```text
hole
water_oil_stain
foreign_fiber
knot
pattern_skip
baijiao
nep
coarse_warp
loose_warp
broken_warp
hanging_warp
coarse_weft
weft_shrinkage
starch_spot
warping_knot
star_skip_jump
broken_spandex
density_wave_color_bar
mark_rolling_repair_singe
wrinkle_cloud_double_yarn_bad_weft
```

测试图片目录：

```text
F:\DataSet\guangdong1_round1_testA_20190818
```

当前目录下有 1000 张 `.jpg` 测试图片。

ONNX 元信息：

```text
Inputs:
  name=images dtype=FLOAT shape=[1, 3, 1088, 1088]

Outputs:
  name=output0 dtype=FLOAT shape=[1, 24, 24276]
```

这个输出中 `24 = 4 + 20`，说明当前模型输出不包含单独的 objectness。
后处理应按 `centerX, centerY, width, height + classScores` 解析。

注意：`.onnx`、`.engine` 属于本地模型资源，不应该直接提交到 GitHub。
仓库只保留代码、文档和必要的轻量配置。

## 数据流

```text
VideoFrame (RGB24)
  |
  v
YoloPreprocessor
  |  RGB -> letterbox -> float CHW
  v
TensorRT Engine
  |  load defect.engine
  |  cudaMemcpyAsync H2D
  |  enqueueV3
  |  cudaMemcpyAsync D2H
  v
output0 tensor
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

假设原图是 `1920 x 1080`，模型输入是 `1088 x 1088`：

```text
scale = min(1088 / 1920, 1088 / 1080)
resized = 1088 x 612
padding = top 238, bottom 238
```

模型输出的框先处在 `1088 x 1088` 坐标系中。必须减去 padding，再除以
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
IVP_YOLO_ONNX=/absolute/path/model.onnx
IVP_YOLO_CLASS_COUNT=20
IVP_YOLO_INPUT_WIDTH=1088
IVP_YOLO_INPUT_HEIGHT=1088
```

这里的环境变量只是当前没有参数设置界面时的临时入口。后续应该改为
应用配置文件或模型管理界面，而不是长期依赖环境变量。

如果不设置这些路径，程序会尝试使用：

```text
models/yolo11l/defect.onnx
models/yolo11l/defect.engine
models/yolo11l/labels.txt
```

当 `IVP_YOLO_CLASS_COUNT` 没有设置时，程序会从 `labels.txt` 自动推导
类别数量。

## 如何编译 TensorRT 后端

默认构建不开 TensorRT，原因是你当前仍需要在普通 Qt Creator 环境下
稳定运行视频播放、Mock 检测、SQLite 和历史查询。

CMake 打开方式：

```text
cmake -S . -B build-tensorrt -DIVP_ENABLE_TENSORRT=ON
cmake --build build-tensorrt
```

如果 CMake 找不到 TensorRT，可以额外指定：

```text
cmake -S . -B build-tensorrt ^
  -DIVP_ENABLE_TENSORRT=ON ^
  -DTENSORRT_INCLUDE_DIR=/path/to/TensorRT/include ^
  -DTENSORRT_NVINFER_LIBRARY=/path/to/libnvinfer.so ^
  -DTENSORRT_PLUGIN_LIBRARY=/path/to/libnvinfer_plugin.so ^
  -DCUDAToolkit_ROOT=/path/to/cuda
```

qmake 打开方式：

```text
qmake "DEFINES+=IVP_ENABLE_TENSORRT" ^
  "TENSORRT_INCLUDE_DIR=/path/to/TensorRT/include" ^
  "TENSORRT_LIB_DIR=/path/to/TensorRT/lib" ^
  "CUDA_INCLUDE_DIR=/path/to/CUDA/include" ^
  "CUDA_LIB_DIR=/path/to/CUDA/lib"
```

Windows 下路径按你的实际安装位置替换；Linux 下通常是 CUDA/TensorRT
安装目录中的 `include` 和 `lib`。

你当前 Windows 路径对应的 qmake 参数示例：

```text
qmake "DEFINES+=IVP_ENABLE_TENSORRT" ^
  "TENSORRT_INCLUDE_DIR=D:/TensorRT/TensorRT-8.6.1.6/include" ^
  "TENSORRT_LIB_DIR=D:/TensorRT/TensorRT-8.6.1.6/lib" ^
  "CUDA_INCLUDE_DIR=E:/CUDA Toolkit 12.5/include" ^
  "CUDA_LIB_DIR=E:/CUDA Toolkit 12.5/lib/x64"
```

注意：这个配置只表示 qmake 能找到头文件和库。Windows 下真正运行
TensorRT C++ 后端时，建议使用 Qt MSVC 64-bit Kit，不建议使用当前
MinGW Kit 强行链接 TensorRT。

## TensorRTEngine 职责

`TensorRTEngine` 只负责通用推理执行，不负责 YOLO 语义：

- 读取 `.engine` 二进制文件。
- 创建 TensorRT `IRuntime`、`ICudaEngine`、`IExecutionContext`。
- 根据输入 shape 设置 `images` Tensor。
- 查找输入输出 Tensor 名称和 shape。
- 分配输入输出 GPU Buffer。
- 拷贝输入、执行 `enqueueV3`、拷贝输出。

`YoloTensorRTDetector` 才负责 YOLO 语义：

- 把 `VideoFrame` 前处理成 `images` 输入。
- 把 `output0` 封装成 `YoloTensorOutput`。
- 调用后处理生成 `DetectionResults`。

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

1. 先读 `YoloPreprocessor`，手算一个 `16:9` 画面如何变成 `1088 x 1088`。
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
- YOLO11 `[1, 24, N]` 输出布局自动识别。

`tests/inference/TensorRTEngineDisabledTest.cpp` 覆盖：

- 默认未启用 TensorRT 时，Engine 不会假装加载成功。
- 错误信息可用于提示用户需要打开 `IVP_ENABLE_TENSORRT`。

`tests/inference/TensorRTEngineSmokeTest.cpp` 覆盖：

- 启用 TensorRT 后加载 `defect.engine`。
- 使用一张全 0 输入执行一次推理。
- 打印输入输出 Tensor 名称、shape 和输出数值范围。

这个测试只在 `IVP_ENABLE_TENSORRT=ON` 时编译。

## 当前 Windows 验证命令

你当前本地环境：

```text
TensorRT: D:\TensorRT\TensorRT-8.6.1.6
CUDA:     E:\CUDA Toolkit 12.5
cuDNN:    D:\cudnn-windows-x86_64-8.9.0.131_cuda12-archive
Engine:   models/yolo11l/defect.engine
```

运行 TensorRT 程序前，当前命令行必须能找到 TensorRT、CUDA 和 cuDNN 的
DLL。至少应包含：

```text
D:\TensorRT\TensorRT-8.6.1.6\lib
D:\TensorRT\TensorRT-8.6.1.6\bin
E:\CUDA Toolkit 12.5\bin
cuDNN 的 bin 目录
```

按你当前路径，可以临时设置：

```text
set "PATH=D:\cudnn-windows-x86_64-8.9.0.131_cuda12-archive\bin;D:\TensorRT\TensorRT-8.6.1.6\lib;D:\TensorRT\TensorRT-8.6.1.6\bin;E:\CUDA Toolkit 12.5\bin;%PATH%"
```

如果运行程序时出现退出码 `-1073741515`，通常表示仍然缺少某个 DLL。
先用：

```text
where nvinfer.dll
where nvinfer_plugin.dll
where cudnn64_8.dll
```

确认这些 DLL 都能被当前命令行找到。

当前已确认：

```text
D:\cudnn-windows-x86_64-8.9.0.131_cuda12-archive\bin\cudnn64_8.dll
```

`trtexec` 已成功生成：

```text
models/yolo11l/defect.engine
```

但使用当前 Qt Creator 的 MinGW Kit 编译 TensorRT C++ smoke test 时，
程序在 `ICudaEngine::createExecutionContext()` 阶段出现 Windows 访问
冲突。由于 `trtexec` 可以成功生成 Engine，问题更像是 Windows 下
MinGW 调用 NVIDIA TensorRT C++ 接口的 ABI 兼容问题，而不是模型或
Engine 本身错误。

后续真实 TensorRT C++ 验证建议：

1. Windows 下切换到 Qt MSVC 64-bit Kit，并使用 MSVC 编译器链接
   TensorRT。
2. 或者在最终目标 Linux 环境下使用 GCC + CUDA + TensorRT 验证。
3. 当前 MinGW Qt Demo 继续保持默认 `MockDetector`，不要在该 Kit 中
   强行启用 TensorRT 后端。
