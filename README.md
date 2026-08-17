# Industrial Vision Platform

基于 C++17 / Qt / FFmpeg / OpenCV DNN / TensorRT 的工业视觉检测平台。

项目面向工业缺陷检测和视频流分析场景，提供视频输入、实时预览、YOLO 推理、检测结果管理、历史查询、结果导出和远程控制协议等基础能力。

## 核心能力

- 支持本地视频、RTSP 视频流和图片文件夹模拟视频输入。
- 基于 FFmpeg 完成视频解码、帧转换和播放控制。
- 基于生产者-消费者模型拆分视频读取、显示和推理链路。
- 支持 Mock、OpenCV DNN、TensorRT 三种检测后端接口。
- 支持 YOLO ONNX 模型真实检测闭环。
- 支持 Detection Preview 模式，保证图像与检测框严格同步。
- 支持检测结果缓存、SQLite 历史记录查询、JSON Lines / CSV 导出。
- 支持 TCP 结果发送和检测控制服务协议。

## 技术栈

- C++17
- Qt6 / Qt Widgets
- FFmpeg
- OpenCV DNN
- TensorRT / CUDA
- SQLite
- TCP / epoll
- CMake / qmake

## 工程结构

```text
IndustrialVisionPlatform/
  apps/
    viewer/        Qt 可视化客户端

  modules/
    common/        公共数据结构、线程安全队列
    video/         视频文件、RTSP、图片序列输入
    audio/         音频解码与播放
    pipeline/      帧分发和消费队列
    playback/      播放、同步、推理调度
    inference/     YOLO / OpenCV DNN / TensorRT 推理接口与实现
    results/       检测结果缓存与统计
    storage/       SQLite 检测记录存储
    network/       检测结果导出与 TCP 发送
    control/       检测服务端与远程控制协议

  tests/           模块级 smoke tests
  tools/           开发辅助脚本
  CMakeLists.txt
  IndustrialVisionPlatform.pro
```

## 本地资源

模型、TensorRT engine、测试视频、图片数据集、构建产物和本地学习文档不提交到 GitHub。

默认忽略：

- `models/**/*.onnx`
- `models/**/*.engine`
- `videos/`
- `build-*/`
- `docs/`

运行真实 YOLO 检测时，需要在本地准备对应的 ONNX / labels 文件，并在 Qt 界面中配置模型路径。

## 当前状态

当前版本重点验证桌面端检测闭环：

- Qt6 + UCRT64 可视化 demo
- FFmpeg 视频播放
- 图片序列输入
- OpenCV DNN 加载 YOLO ONNX
- 检测框同步显示
- 检测结果存储、查询、导出和远程控制

TensorRT 后端接口已经预留，后续可继续完善真实 GPU 推理部署。
