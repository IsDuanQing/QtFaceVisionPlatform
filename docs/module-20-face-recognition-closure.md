# 模块 20：真实人脸识别闭环

## 模块目标

模块 20 的目标，是在已有“人脸检测框”的基础上，补齐从人员参考图到自动识别事件的第一版工程闭环：

```text
Faces 页签录入人员
  |
  v
参考图加载
  |
  v
用当前人脸检测器裁剪参考图中的人脸
  |
  v
提取参考图特征并写入 face_feature_templates
  |
  v
播放 / RTSP / 视频文件推理
  |
  v
对每个检测框提取查询特征
  |
  v
与特征库做距离匹配
  |
  v
自动绑定检测记录到人员身份
  |
  v
生成去重后的 face_recognition_events
```

这一版已经切到真正的人脸特征模型：`OpenCV FaceRecognizerSF + SFace ONNX`。默认模型是 `models/face-recognition-sface/face_recognition_sface_2021dec.onnx`，也可以用环境变量 `IVP_FACE_FEATURE_ONNX` 覆盖。`similarityThreshold` 默认值为 0.363，`minSimilarityMargin` 默认值为 0.05，分别用于命中判断和相近身份的歧义抑制。

## 关键代码

- `modules/common/include/common/FaceFeature.h`
  - `FaceRecognitionConfig`
  - `FaceFeatureVector`
  - `FaceFeatureTemplate`
  - `FaceRecognitionDiagnostics`
- `modules/common/include/common/FaceRecognitionResult.h`
  - `FaceRecognitionResult`
- `modules/recognition/include/recognition/FaceRecognizer.h`
  - `initialize()`
  - `setGallery()`
  - `diagnostics()`
  - `extractReferenceFeatures()`
  - `recognize()`
- `modules/storage/include/storage/SQLiteDetectionStorage.h`
  - `replaceFaceFeatures()`
  - `allFaceFeatures()`
  - `recentFaceRecognitionEvents()`
- `apps/viewer/MainWindow.cpp`
  - Faces 页签录入人员和参考图特征提取
  - 识别可用性状态显示
  - History 页签人员绑定结果展示

## 识别可用性诊断

`FaceRecognitionDiagnostics` 用来回答 UI 和调试时最常见的四个问题：

- 识别功能是否启用：`enabled`
- 当前识别器是否可用：`available`
- 当前特征库有多少模板：`gallerySize`
- 当前使用的特征模型名、模型路径和错误信息：`modelName` / `modelPath` / `lastError`

Faces 页签中 `Recognition` 指标会显示：

- `Disabled`：识别配置关闭。
- `Unavailable | ...`：识别器不可用，并显示错误摘要。
- `Ready | <model> | <N> templates`：识别器可用，且已加载特征模板。

这类诊断不要只写日志。识别链路经常失败在模型、参考图、检测器或特征库其中一环，直接显示到 UI 可以显著减少排查时间。

## 参考图人脸裁剪

录入人员时，参考图片路径不再直接整图提特征，而是先使用当前 `YoloOpenCVDnnDetector` 做一次人脸检测：

1. 读取参考图片。
2. 转为 `VideoFrame`。
3. 调用 `IDetector::detect()`。
4. 从检测结果中选择面积最大的有效人脸框；置信度作为同面积时的辅助判断。
5. 按该框裁剪人脸区域。
6. 对裁剪后的人脸提取特征。

这样做的原因是：真实参考图往往包含背景、肩膀、多人或大面积空白。如果直接整图提特征，特征会被非人脸区域污染，后续匹配距离会变得不可控。

当前导入的参考图会复制到仓库内的 `data/face-references/<faceCode>/reference.<ext>`，数据库里保存的是相对路径，避免中文桌面路径和换机器后失效的问题。

## 特征库加载

程序启动或 Faces 页签刷新时，会从 SQLite 读取 `face_feature_templates`，再调用：

```cpp
VideoPlayer::setFaceRecognitionGallery(templates)
```

播放链路只依赖已经加载好的内存特征库，不在每帧识别时访问 SQLite。这个边界很重要：数据库适合持久化，实时识别适合内存查询。

## 自动识别和绑定

每个 `DetectionResult` 中带有一个 `FaceRecognitionResult face` 字段。

当 `FaceRecognizer::recognize()` 匹配成功时，会填充：

- `faceId`
- `faceCode`
- `faceName`
- `distance`
- `similarity`
- `threshold`
- `matchedAtMs`
- `recognizerName`

随后 `SQLiteDetectionStorage::saveFrameResults()` 会：

1. 写入 `detection_frames`。
2. 写入 `detection_records`。
3. 如果识别成功，写入 `detection_face_links`。
4. 如果没有命中轨迹级去重规则，再写入 `face_recognition_events`。

其中 `detection_face_links` 表示“某条检测记录是谁”，`face_recognition_events` 表示“系统产生了一次可供告警 / 展示 / 审计使用的识别事件”。两者不是同一件事。

## 事件去重

视频中同一个人会连续出现在很多帧里，如果每帧都生成事件，History 和后续告警会被刷屏。

当前策略是：

- 同一 `session_id`
- 同一 `source_id`
- 同一 `track_id`
- 对已识别人员：同一 `face_identity_id` 和 `face_recognized`
- 对未知或不确定结果：同一事件类型且 `face_identity_id` 为空
- 以上条件在同一条轨迹内只保留一次事件

当旧数据或异常结果没有轨迹号，即 `track_id == 0` 时，才回退到原来的 5 秒冷却：

```cpp
static constexpr std::int64_t kRecognitionEventCooldownMs = 5000;
```

注意：去重只抑制事件表，不会抑制检测记录和人员绑定。也就是说，History 中的每条检测记录仍然可以看到识别结果，但事件列表不会被连续帧撑爆。

## Events 页面

客户端的 `Events` 页签查询 `face_recognition_events`，与 `History` 的职责不同：

- `History`：展示每一帧的人脸检测记录，可以用于逐帧回溯和手动绑定。
- `Events`：展示经过业务去重的识别事件，适合告警、审计和快速查看。

当前会写入以下事件类型：

- `face_recognized`：识别成功，并且有有效人员身份。
- `face_low_similarity`：有候选特征，但最佳相似度低于 `Similarity` 阈值。
- `face_ambiguous`：最佳候选人与第二候选人的差距小于 `Margin`。
- `face_unknown`：没有可用候选身份。

模型不可用、没有查询特征、人脸过小等状态属于技术诊断，不会直接生成陌生人事件。

## Qt Creator 测试步骤

1. 在 Parameters 中选择 `OpenCV DNN`，点击 `Face` 预设。
2. 在 Faces 页签添加人员编号、姓名和参考图片路径。
3. 确认 `Library` 显示人员数和模板数。
4. 确认 `Recognition` 显示 `Ready`，并且模板数大于 0。
5. 打开包含该人员的人脸视频、RTSP 或测试视频。
6. 切到 Detection Preview，观察检测框和画面是否严格对应。
7. 在 History 页签查看 Face 列是否自动出现人员名称和相似度。
8. 打开 Events 页签，确认能够看到 `Recognized` 等事件。
9. 连续播放同一个人时，检测框上的 `T编号` 应保持稳定，Events 中同一轨迹只保留一次同类事件，而 History 仍然保留逐帧记录。
10. 调高 `Similarity`，再次播放视频，低于阈值的结果应出现 `Low similarity` 事件。

## 当前限制

- 当前使用的是通用 SFace embedding 模型，不是业务定制训练模型，极端光照、口罩和侧脸场景仍会掉精度。
- 参考图中如果没有被检测到人脸，不会写入新的特征模板。
- 如果参考图特征提取失败，不会覆盖已有特征库，避免误删旧模板。
- 旧的 LBPH 模板不会自动迁移到 SFace，需要重新保存参考图重新提特征。
- 识别阈值仍需要结合真实样本调参。
- 单张清晰正脸通常已经够用，但多张参考图会提高跨姿态和跨光照稳定性。
- 未知人脸现在可以按短期轨迹去重；长时间遮挡、快速交叉或跟踪失败时仍可能产生新轨迹。
- 事件页面目前支持查询和过滤，还没有独立的告警策略和事件详情回放。

## 后续演进

下一阶段建议按这个顺序推进：

1. 增加一人多参考图的 UI 管理能力。
2. 引入带关键点的检测器，使用 `FaceRecognizerSF::alignCrop()` 做几何对齐。
3. 使用人脸特征辅助几何跟踪，降低多人交叉时的轨迹交换。
4. 增加 unknown face 低置信度复核队列和告警策略。
5. 将 SQLite 写入迁移到后台线程，减少 UI 线程压力。
