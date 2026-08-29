# 模块 8：SQLite 检测与识别结果存储

模块 8 的目标是把运行时产生的结构化数据写入 SQLite，为历史查询、身份追溯、识别事件和后续告警准备持久化基础。

当前存储层不再只保存检测框，还保存人脸库、特征模板、检测记录与人员身份的关联，以及去重后的识别事件。

## 当前数据流

```text
VideoPlayer::inferenceLoop()
  |
  v
DetectionResults + FaceRecognitionResult
  |
  v
MainWindow
  |
  +--> ResultManager              内存缓存与实时统计
  |
  +--> SQLiteDetectionStorage     SQLite 持久化
```

`MainWindow` 只是应用级连线位置，不包含 SQL 建表和查询细节。所有 SQLite 逻辑都在 `modules/storage`。

## 关键文件

- `modules/storage/include/storage/SQLiteDetectionStorage.h`
- `modules/storage/src/SQLiteDetectionStorage.cpp`
- `tests/storage/SQLiteDetectionStorageTest.cpp`
- CMake 目标 `ivp_storage_smoke_test`

## 数据库表设计

### inspection_sessions

一条记录表示一次检测任务。

关键字段：

- `source_id`：视频源或相机标识。
- `input_url`：本地文件路径或 RTSP 地址。
- `started_at_ms`：开始时间。
- `ended_at_ms`：结束时间。

同一个 RTSP 地址会被多次打开，同一个本地视频也可能被重复检测。只用 `source_id` 和 `frame_index` 无法区分不同运行批次，所以需要 `session_id`。

### detection_frames

一条记录表示一次送入推理的帧。

关键字段：

- `session_id`
- `source_id`
- `frame_index`
- `pts_ms`
- `object_count`
- `recorded_at_ms`

即使当前帧没有检测到人脸，也可以写入这一张表。这样以后能统计处理帧数、无人脸帧数和检测覆盖率。

### detection_records

一条记录表示一个检测目标框。当前主线中，这个目标框主要是人脸框。

关键字段：

- `session_id`
- `source_id`
- `frame_index`
- `pts_ms`
- `track_id`：短期人脸轨迹编号，同一会话内用于关联连续检测框。
- `class_id`
- `class_name`
- `confidence`
- `box_x / box_y / box_width / box_height`
- `recorded_at_ms`

这张表负责历史查询和检测明细展示。它不直接等同于“识别事件”，因为同一张人脸可能连续出现在很多帧里。

### face_identities

一条记录表示 Faces 页签中的一个人员身份。

关键字段：

- `id`：SQLite 内部主键，只用于人员身份与特征、检测记录之间的关联，不保证连续，也不会因为删除记录而复用。
- `face_code`：人员编号，唯一。
- `display_name`：人员姓名。
- `reference_image_path`：参考图片路径。
- `notes`：备注。
- `created_at_ms`
- `updated_at_ms`

当前 UI 支持新增、刷新和删除人员身份。
Faces 表格中的 `No.` 是当前列表的显示序号，不是数据库主键；人员业务编号应查看 `Code` 列。

### face_feature_templates

一条记录表示某个人员身份的一份参考特征。

关键字段：

- `face_identity_id`
- `model_name`
- `sample_image_path`
- `feature_values`
- `value_count`
- `created_at_ms`

`feature_values` 使用 BLOB 保存浮点特征数组。这样做可以让识别模块换模型时仍然保留清晰边界：存储层只保存模型名和特征，不理解模型内部算法。

### detection_face_links

一条记录表示“某条检测记录绑定到了哪个人员身份”。

关键字段：

- `detection_record_id`
- `face_identity_id`
- `face_code`
- `face_name`
- `matched_at_ms`
- `distance`
- `similarity`
- `threshold_value`
- `recognizer_name`

这张表同时支持两种来源：

- History 页签中的手动绑定 / 取消绑定。
- 自动识别成功后的绑定。

### face_recognition_events

一条记录表示一次可用于展示、告警或审计的人脸识别事件。

关键字段：

- `detection_record_id`
- `session_id`
- `source_id`
- `frame_index`
- `pts_ms`
- `track_id`
- `event_type`
- `face_identity_id`
- `face_code`
- `face_name`
- `distance`
- `similarity`
- `threshold_value`
- `recognizer_name`
- `created_at_ms`

事件表和绑定表分开，是为了避免连续视频帧造成事件刷屏。检测记录可以每帧保存，识别事件按轨迹做去重；没有轨迹号的旧数据才回退到 5 秒冷却。

## 一帧写入为什么要使用事务

一帧结果可能包含：

1. 一条 `detection_frames`。
2. 零条或多条 `detection_records`。
3. 零条或多条 `detection_face_links`。
4. 零条或多条 `face_recognition_events`。

如果先写入帧、写到一半程序异常，就会留下不完整的历史数据。

因此 `SQLiteDetectionStorage::saveFrameResults()` 的流程是：

```text
BEGIN TRANSACTION
  |
  +--> insert detection_frames
  |
  +--> insert detection_records ...
  |
  +--> insert detection_face_links ...
  |
  +--> insert face_recognition_events ...  // 先经过去重判断
  |
COMMIT
```

任何一步失败都会执行 `ROLLBACK`。

## 识别事件去重

同一个人连续出现在视频中时，每帧都会产生检测记录，但不应该每帧都生成告警事件。

当前策略在 `SQLiteDetectionStorage` 中实现：

- 同一 `session_id`
- 同一 `source_id`
- 同一 `track_id`
- 同一事件类型
- 同一 `face_identity_id`（未知事件为空）
- 同一轨迹内只写入一次 `face_recognition_events`

旧数据或没有轨迹号时使用冷却时间：

```cpp
static constexpr std::int64_t kRecognitionEventCooldownMs = 5000;
```

注意：去重只影响事件表，不影响 `detection_records` 和 `detection_face_links`。History 仍然可以看到每条检测记录的人员身份。

## 为什么使用 SQLite C API

当前项目需要同时兼顾 Qt 客户端和后续服务端。

SQLite C API 有两个优点：

1. 存储模块不依赖 Qt UI。
2. 后续 TCP 服务端可以复用同一套数据库代码。

Qt 的职责仍然是界面和应用流程，不应该成为底层存储模块的前提。

## 当前 UI 行为

程序启动时：

1. 通过 `QStandardPaths::AppLocalDataLocation` 计算应用数据目录。
2. 打开或创建 `inspection_records.db`。
3. 界面底部的 `Storage` 指标显示 `Ready`。

打开本地视频或 RTSP 时：

1. 创建 `inspection_sessions` 会话。
2. `Storage` 显示 `Recording`。
3. 每次收到检测结果，写入对应帧、检测框、识别绑定和识别事件。
4. 停止、断开或切换输入时，写入会话结束时间。

Faces 页签：

1. 保存人员身份到 `face_identities`。
2. 从参考图裁剪人脸并提取特征。
3. 写入 `face_feature_templates`。
4. 刷新识别特征库和 `Recognition` 状态。

History 页签：

1. 从 `detection_records` 查询检测记录。
2. 左连接 `detection_face_links` 显示人员身份。
3. 支持手动绑定或取消绑定人员。

## 建议阅读顺序

1. `modules/common/include/common/DetectionResult.h`
2. `modules/common/include/common/FaceRecognitionResult.h`
3. `modules/common/include/common/FaceFeature.h`
4. `modules/storage/include/storage/SQLiteDetectionStorage.h`
5. `modules/storage/src/SQLiteDetectionStorage.cpp`
6. `apps/viewer/MainWindow.cpp` 的 `reloadFaceRecognitionGallery()`、`addFaceIdentity()` 和 `displayDetections()`
7. `tests/storage/SQLiteDetectionStorageTest.cpp`

重点看这些问题：

1. 为什么检测记录和识别事件要分表？
2. 为什么自动识别绑定和手动绑定可以复用 `detection_face_links`？
3. 为什么特征模板需要保存 `model_name`？
4. 为什么事件去重不应该删除检测记录？
5. 为什么当前 UI 线程写库只是第一版方案？

## 当前限制

- 当前 SQLite 写入仍发生在 Qt UI 线程。
- 特征模板还没有版本迁移策略。
- 当前轨迹使用 IoU、中心距离和短时漏检容忍，复杂交叉场景仍可能换轨。
- 当前 History 主要展示检测记录，客户端的 Events 页签负责展示经过冷却去重的识别事件。
- 数据量很大时，历史查询应迁移到后台线程或独立数据服务。

后续建议升级为：

```text
DetectionResults
  |
  v
StorageQueue
  |
  v
SQLite Writer Thread
```
