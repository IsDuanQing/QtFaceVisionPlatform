# 人脸平台范围说明

## 产品目标

当前项目已经从工业缺陷检测收缩为人脸检测 / 识别平台。

主链路为：

```text
摄像头 / RTSP / 视频文件
  |
  v
视频流处理
  |
  v
人脸检测 / 识别
  |
  v
事件分析
  |
  v
SQLite 存储
  |
  v
告警 / 客户端管理
```

## 当前主线模块

- `apps/viewer`：Qt 客户端界面。
- `modules/common`：公共数据结构，包括检测结果、人脸特征和识别结果。
- `modules/video`：FFmpeg 视频 / RTSP 解码。
- `modules/pipeline`：生产者-消费者队列和帧分发。
- `modules/playback`：播放、暂停、检测预览和识别调度。
- `modules/inference`：OpenCV DNN YOLO 人脸检测。
- `modules/recognition`：人脸特征提取、特征库匹配和识别诊断。
- `modules/tracking`：人脸检测框短期跟踪和稳定轨迹编号。
- `modules/storage`：SQLite 会话、检测记录、人脸库、特征库、记录关联和识别事件。
- `modules/results`：检测结果缓存和统计。
- `modules/control`：检测服务端和远程任务配置协议。
- `modules/network`：检测结果发送。

## 已完成能力

- 本地视频 / RTSP 解码。
- 显示队列和推理队列解耦。
- OpenCV DNN 加载 YOLO ONNX 人脸检测模型。
- Detection Preview 模式，保证图像和检测框同步。
- SQLite 历史检测记录查询。
- Faces 页签支持人员编号、姓名、参考图路径和备注。
- History 页签支持手动绑定 / 取消绑定人员身份。
- 参考图人脸裁剪、特征提取、特征库加载和自动识别。
- 识别可用性诊断。
- 人脸短期跟踪，检测框、History 和 Events 显示轨迹编号。
- 识别事件优先按轨迹去重，无轨迹的旧数据回退到 5 秒冷却。

## 暂时冻结

- `modules/audio`：当前阶段不接音频。
- TensorRT 演示路径：暂作为历史技术储备，不作为当前主链路。
- 图片序列输入：保留测试思路，当前客户端主入口优先视频文件和 RTSP。
- Mock detector：只保留测试用途。
- 工业缺陷检测相关 UI 文案、模型路径和演示资源。

## 下一步

下一阶段优先把“轨迹稳定性”和“事件闭环”做扎实：

1. 增加轨迹参数配置和运行状态。
2. 使用人脸特征辅助轨迹关联，降低多人交叉时的轨迹交换。
3. 增加一人多参考图管理。
4. 增加 unknown face 低置信度复核流程，并完善事件告警策略。
5. 将 SQLite 写入和历史查询迁移到后台线程。
