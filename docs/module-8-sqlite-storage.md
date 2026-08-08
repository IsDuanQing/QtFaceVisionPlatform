# 模块 8：SQLite 检测结果存储

模块 7 的 `ResultManager` 解决了内存中的缓存、查询和统计，但程序退出后数据会消失。

模块 8 的目标是把结构化检测结果写入 SQLite，为后续历史查询、质量追溯和统计分析准备持久化基础。

## 当前数据流

```text
VideoPlayer::inferenceLoop()
  |
  v
DetectionResults + frameIndex + ptsMs + sourceId
  |
  v
MainWindow
  |
  +--> ResultManager              内存缓存与实时统计
  |
  +--> SQLiteDetectionStorage     SQLite 持久化
```

这里 `MainWindow` 只是应用级的连线位置，不包含 SQL 语句和建表逻辑。所有 SQLite 细节都在 `modules/storage`。

## 新增文件

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

为什么需要会话表？

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

即使当前帧没有缺陷，也会写入这一张表。这样以后能统计处理帧数、无缺陷帧数和检测覆盖率。

### detection_records

一条记录表示一个缺陷目标框。

关键字段：

- `session_id`
- `source_id`
- `frame_index`
- `pts_ms`
- `class_id`
- `class_name`
- `confidence`
- `box_x / box_y / box_width / box_height`

这张表负责历史回放和缺陷明细查询。

## 为什么一帧写入要使用事务

一帧结果可能包含：

1. 一条 `detection_frames`。
2. 零条或多条 `detection_records`。

如果先写入帧、写到一半程序异常，就会留下不完整的历史数据。

因此 `SQLiteDetectionStorage::saveFrameResults()` 的流程是：

```text
BEGIN TRANSACTION
  |
  +--> insert detection_frames
  |
  +--> insert detection_records ...
  |
COMMIT
```

任何一步失败都会执行 `ROLLBACK`。

## 为什么使用 SQLite C API

当前项目需要同时兼顾 Qt 客户端和后续 Linux 服务端。

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
3. 每次收到检测结果，写入对应帧和检测框。
4. 停止、断开或切换输入时，写入会话结束时间。

## 建议阅读顺序

1. `modules/common/include/common/DetectionResult.h`
2. `modules/results/include/results/ResultManager.h`
3. `modules/storage/include/storage/SQLiteDetectionStorage.h`
4. `modules/storage/src/SQLiteDetectionStorage.cpp`
5. `apps/viewer/MainWindow.cpp` 的 `startStorageSession()` 和 `displayDetections()`
6. `tests/storage/SQLiteDetectionStorageTest.cpp`

重点看这些问题：

1. 为什么 `SQLiteDetectionStorage` 不放在 `apps/viewer`？
2. 为什么数据库类要禁用拷贝？
3. 为什么每次 SQL 写入都使用预编译语句和绑定参数？
4. 为什么 SQLite 事务能避免“帧写了、框没写完”的脏数据？
5. 为什么当前直接在 UI 线程写库只是第一版方案？

## 当前限制

当前版本为了先验证数据流，SQLite 写入发生在 Qt UI 线程。

真实工业流长时间运行时，磁盘抖动、数据库 checkpoint 或批量写入都可能造成 UI 卡顿。因此后续应该升级为：

```text
DetectionResults
  |
  v
StorageQueue
  |
  v
SQLite Writer Thread
```

同时可以进一步增加：

- 按时间、类别、置信度查询。
- 缺陷缩略图或原始帧路径。
- 数据库迁移版本。
- 批量提交和写入失败重试。
- 网络模块读取历史检测记录。

## 后续演进

模块 9 已完成历史检测记录查询界面的第一版。

当前查询层已经支持按会话、类别、sourceId 和记录时间筛选，Qt 层通过 `QAbstractTableModel` 展示查询结果。
