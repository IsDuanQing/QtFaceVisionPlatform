# Face Recognition Platform

基于 `C++17 / Qt 6 / FFmpeg / OpenCV DNN / SQLite` 的人脸检测、识别与事件管理平台。

主流程：

`摄像头 / RTSP / 视频文件 -> 视频流处理 -> 人脸检测/识别 -> 事件分析 -> 存储 -> 告警 -> 客户端管理`

## 当前功能

- 视频输入：支持本地视频文件和 RTSP 视频流。
- 视频处理：基于 FFmpeg 解码，并通过生产者-消费者队列解耦读取、检测和显示。
- 人脸检测：基于 OpenCV DNN 加载 YOLO ONNX 模型，输出人脸框、类别和置信度。
- 检测预览：支持检测帧预览模式，保证画面和检测框同步显示。
- 历史记录：检测结果写入 SQLite，支持按会话、来源、类别和时间范围查询。
- 人脸库：支持维护人员编号、姓名、参考图片路径和备注，并可把历史检测记录手动关联到人员身份。
- 远程控制：提供检测服务控制协议，可用于任务启动、停止、状态查询和远程任务参数配置。

## 工程结构

- `apps/viewer`：Qt 客户端界面、视频预览、参数配置、历史记录和人脸库管理。
- `modules/common`：公共数据结构、运行状态和线程队列。
- `modules/video`：FFmpeg 视频解码和帧转换。
- `modules/pipeline`：帧分发和生产者-消费者调度。
- `modules/playback`：视频打开、播放、暂停、停止和检测预览闭环。
- `modules/inference`：OpenCV DNN YOLO 人脸检测。
- `modules/results`：检测结果汇总和统计。
- `modules/storage`：SQLite 会话、检测记录、人脸库和记录关联存储。
- `modules/network`：检测结果网络发送。
- `modules/control`：检测服务端和控制协议。
- `tests`：模块级 smoke test。

## 当前模型

- `models/yolov8-face/face.onnx`
- `models/yolov8-face/labels.txt`

## 说明

- 当前项目已聚焦到人脸检测/识别业务链路，旧的音频、图片序列、Mock 推理、TensorRT 演示和旧 `src` 目录已移除。
- 当前“人脸库”阶段已经完成身份信息管理和检测记录手动关联；真正的特征提取、相似度匹配和自动识别是下一阶段工作。
