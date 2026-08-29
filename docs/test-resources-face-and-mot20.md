# 人脸平台测试资源

## 已准备的资源

当前仓库本地建议准备两类资源：

```text
models/yolov8-face/
  face.onnx      YOLOv8 face ONNX 权重，本地测试用，不提交 GitHub
  labels.txt     类别文件，内容为 face

videos/face-detection/
  face-test.mp4  人脸检测和识别闭环测试视频，本地测试用

videos/mot20-02/
  seqinfo.ini
  img1/
    000001.jpg
    ...
    002782.jpg
```

`MOT20-02` 是固定摄像头视角的多人密集场景，25 FPS，2782 帧，时长约 111 秒。它适合测试多人目标、推理吞吐、队列缓存和长时间播放稳定性，但不适合做人脸识别精度验证，因为画面里人脸太小。

## Qt Creator 测试步骤

### 人脸检测

1. 启动 Qt 客户端。
2. 在 Parameters 中选择 `OpenCV DNN`。
3. 点击 `Face` 预设。
4. 点击顶部 `Open`，选择：

```text
D:/IndustrialVisionPlatform/videos/face-detection/face-test.mp4
```

5. 切换到 Detection Preview。
6. 观察视频中的人脸框是否稳定贴合。

`Face` 预设会自动设置：

- `OpenCV DNN`
- `ONNX: D:/IndustrialVisionPlatform/models/yolov8-face/face.onnx`
- `Labels: D:/IndustrialVisionPlatform/models/yolov8-face/labels.txt`
- `Input: 640 x 640`
- `Classes: 1`
- `Conf: 0.25`
- `NMS: 0.45`

### 人脸库与识别闭环

1. 切到 Faces 页签。
2. 输入人员编号，例如 `person_001`。
3. 输入姓名。
4. 选择一张清晰的单人参考图，或一个参考图目录。
5. 点击 `Add`。
6. 确认 `Library` 显示人员数和模板数。
7. 确认 `Recognition` 显示 `Ready`，并且模板数大于 0。
8. 打开包含该人员的视频。
9. 在 History 页签确认 Face 列是否出现人员姓名和相似度。

参考图建议：

- 人脸清晰，分辨率不要太低。
- 尽量单人正脸或轻微侧脸。
- 避免多人合照、强遮挡、强背光。
- 如果使用目录，先放少量质量较高的图片，不要一次性塞入大量低质量图。

### 识别事件去重

1. 使用同一个人连续出现的视频。
2. 播放一段时间。
3. 刷新 History，确认检测记录仍然连续保存。
4. 查询识别事件时，同一人员在 5 秒内不应重复生成大量 `face_recognized` 事件。

### 多人拥挤场景压力测试

当前客户端主入口优先视频文件和 RTSP。`MOT20-02` 是图片序列资源，如果要用于当前主线测试，建议先转成 MP4 或推成 RTSP。

转换 MP4 示例：

```powershell
ffmpeg -framerate 25 -i D:/IndustrialVisionPlatform/videos/mot20-02/img1/%06d.jpg ^
  -c:v libx264 -pix_fmt yuv420p D:/IndustrialVisionPlatform/videos/mot20-02/mot20-02.mp4
```

然后在 Qt 客户端中点击 `Open` 打开：

```text
D:/IndustrialVisionPlatform/videos/mot20-02/mot20-02.mp4
```

注意：MOT20 适合压测检测框数量、队列稳定性和 UI 刷新，不适合验证人脸识别准确率。

## 重新下载 MOT20-02

如果 `videos/mot20-02/img1` 不完整，可以执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/download_mot20_02.ps1
```

这个脚本支持断点续传，已经存在且大小正常的图片会跳过。

## 工程注意点

- `models/**/*.onnx`、`videos/mot20-02/` 和 `videos/**/*.mp4` 已被 `.gitignore` 忽略。
- 大模型和大视频只适合本机测试，不建议上传 GitHub。
- 验证识别准确率时，优先使用清晰人脸视频和清晰参考图。
- 验证系统压力时，再使用 MOT20 这类多人密集场景。
