# 模块 14：真实 TensorRT 推理闭环

模块 10 到模块 13 已经把 YOLO 前后处理、TensorRT 引擎封装、UI 参数配置
和参数持久化准备好了。模块 14 的目标是把这些零件真正连成一条可验证的
推理闭环。

## 这个模块到底在补什么

前面的代码已经可以做到：

- 读到配置
- 选择 `Mock` 或 `TensorRT`
- 加载 engine
- 做预处理和后处理

但工程上还差两件事：

1. 真实推理失败时，错误要能回传到播放层和 UI。
2. 真实后端的帧节流行为要和 `MockDetector` 对齐。

如果这两点不补，切换到 TensorRT 后，用户会看到：

- 明明引擎报错了，界面却只是在空转
- Mock 和 TensorRT 的结果节奏不一致
- 后续排查问题时，不知道到底是预处理、引擎还是后处理出错

## 当前实现范围

### 1. `YoloTensorRTDetector`

真实后端现在承担三件事：

- 用 `TensorRTEngine` 加载 engine
- 用 `YoloPreprocessor` 做 letterbox + CHW
- 用 `YoloPostprocessor` 解析 `output0`

同时它现在也支持：

- `detectEveryNFrames`
- 初始化失败时返回明确错误
- 检测失败时返回明确错误

### 2. `VideoPlayer`

推理线程里现在会检查 `detector_->lastError()`。
如果真实后端在预处理、执行或后处理阶段失败，`VideoPlayer`
会把错误变成 `errorOccurred`，而不是静默丢掉。

### 3. Smoke Test

新增 `ivp_yolo_tensorrt_detector_smoke_test`：

- 先初始化 detector
- 再对一帧合成 RGB 图像做推理
- 再验证 `detectEveryNFrames` 跳帧逻辑

这个测试适合在支持 TensorRT 的 MSVC / Linux 环境里跑。

## 数据流

```text
UI 选择 TensorRT
  |
  v
VideoPlayer::setDetectorConfig()
  |
  v
YoloTensorRTDetector::initialize()
  |
  v
TensorRTEngine::loadFromFile()
  |
  v
VideoPlayer 推理线程
  |
  v
YoloTensorRTDetector::detect()
  |
  +--> YoloPreprocessor
  |
  +--> TensorRTEngine::infer()
  |
  +--> YoloPostprocessor
  |
  v
DetectionResultsReady / errorOccurred
```

## 当前限制

- 当前 Qt Creator 的 MinGW demo 仍建议使用 `MockDetector`。
- 真实 TensorRT 更适合在支持的 MSVC Kit 或 Linux 环境验证。
- 现在只支持单输入、单输出、FLOAT Tensor 的 YOLO engine。
- 如果 labels 数量和模型类别数不一致，初始化会直接失败。

## 学习重点

1. 读 `YoloTensorRTDetector::initialize()`，理解模型加载前的参数校验。
2. 读 `YoloTensorRTDetector::detect()`，理解预处理、推理、后处理的顺序。
3. 读 `VideoPlayer::inferenceLoop()`，理解错误如何从 detector 回传到 UI。
4. 读 `tests/inference/YoloTensorRTDetectorSmokeTest.cpp`，理解如何离线验证真实推理链路。
