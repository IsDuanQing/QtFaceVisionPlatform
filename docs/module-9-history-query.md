# 模块 9：历史检测记录查询界面

模块 8 已经把检测会话、检测框、人员绑定和识别事件写入 SQLite。模块 9 负责把历史检测记录重新查询出来，并在 Qt 的 History 页签中展示。

## 设计目标

本模块完成一个可用的历史查询闭环：

```text
SQLiteDetectionStorage
  |
  |-- recentSessions()
  |-- queryHistory()
  v
DetectionHistoryTableModel
  |
  v
QTableView
```

`MainWindow` 负责收集筛选条件、刷新人员下拉框、触发查询以及执行手动绑定 / 取消绑定。SQL 细节仍然留在 `modules/storage`。

## 查询条件

当前支持：

- 按检测会话筛选。
- 按 sourceId 或输入地址模糊筛选。
- 按检测类别模糊筛选。
- 按记录时间的起止范围筛选。
- 限制返回记录数量。

查询结果按记录时间倒序排列，默认最多显示 200 条。

检测会话运行期间，客户端会以约 500 ms 的节流间隔检查是否有新的检测记录或
轨迹状态写入。只有数据发生变化时才重新查询 History，并同步刷新 Events，避免
每个视频帧都查询 SQLite。停止播放或会话结束时会执行最后一次完整刷新。

## 页面操作

History 页签的查询操作分为三类：

- `Refresh`：按照当前筛选条件重新查询。
- `Reset Filters`：只重置筛选条件，不修改数据库中的任何数据。
- `Delete Records`：删除所有符合当前筛选条件的检测记录，不受 `Limit`
  显示数量限制影响。

删除 History 记录时，存储层会在一个 SQLite 事务中同步删除：

- 关联的人脸身份记录 `detection_face_links`。
- 关联的识别事件 `face_recognition_events`。
- 已经没有检测记录的轨迹摘要 `face_tracks`。

删除操作不会删除 Faces 页签中的人员身份和参考特征。当前播放会话进行中时，
客户端会拒绝删除操作，必须先停止播放，避免正在写入的检测结果与删除操作交叉。

Events 页签的操作规则类似：

- `Refresh`：按照当前筛选条件重新查询事件。
- `Reset Filters`：只重置事件筛选条件。
- `Delete Events`：删除所有符合当前事件筛选条件的事件，不修改 History
  检测记录，也不修改 Faces 人员库。

两个删除按钮都会先显示确认对话框。没有设置筛选条件时，删除范围是对应表中的
全部记录，因此应优先使用会话、来源、类型或时间条件缩小范围。

## 新增接口

### `InspectionSessionSummary`

表示一次检测会话：

- `sessionId`
- `sourceId`
- `inputUrl`
- `startedAtMs`
- `endedAtMs`
- `frameCount`
- `objectCount`

### `DetectionHistoryQuery`

表示查询条件。可选条件使用 `std::optional` 表达，没有勾选的条件不会拼接到 SQL 中。

### `DetectionHistoryRow`

表示一条历史检测记录，同时带有会话、帧、目标框和人员身份信息。

轨迹相关字段包括：

- `trackId`

同一视频会话中，连续关联到同一张人脸的检测记录会共享 Track 编号。Track 只用于短期轨迹关联，不替代人员身份；人员身份仍来自 `detection_face_links`。

与识别相关的字段包括：

- `faceId`
- `faceCode`
- `faceName`
- `faceMatchedAtMs`
- `faceDistance`
- `faceSimilarity`
- `faceThreshold`
- `faceRecognizerName`

这些字段来自 `detection_face_links`，因此既可以展示自动识别结果，也可以展示手动绑定结果。

## Qt Model/View

`DetectionHistoryTableModel` 继承自 `QAbstractTableModel`，负责：

- 把 `DetectionHistoryRow` 转换为表格单元格。
- 提供列标题。
- 格式化时间、PTS、置信度、目标框和 Face 列。
- 提供工具提示显示完整输入地址和人员识别信息。
- 显示 Track 列，用于检查连续检测是否保持同一轨迹。

当前 History 表格包含 `Face` 列：

- 未绑定人员时显示 `--`。
- 手动绑定后显示姓名或人员编号。
- 自动识别成功且有相似度时显示姓名和相似度百分比。

`QTableView` 只负责表格展示，不负责 SQL 和业务查询。

## 手动绑定 / 取消绑定

History 页签支持把某条检测记录绑定到 Faces 页签中的人员身份：

```text
选中 History 记录
  |
  v
选择人员
  |
  v
Bind Face
  |
  v
detection_face_links
```

取消绑定会删除该检测记录对应的 `detection_face_links` 记录，但不会删除原始检测记录，也不会删除人脸库中的人员身份。

手动绑定主要用于：

- 修正自动识别错误。
- 给旧历史记录补充人员身份。
- 在正式识别模型接入前验证人脸库和历史记录的关联流程。

## 自动识别结果

自动识别成功时，`SQLiteDetectionStorage::saveFrameResults()` 会把 `DetectionResult::face` 写入 `detection_face_links`。因此 History 页签刷新后可以直接看到自动关联结果。

注意：识别事件去重只影响 `face_recognition_events`，不影响 History 表格中的检测记录和人员绑定。

## 学习顺序

1. SQLite `SELECT`、`JOIN`、`LEFT JOIN`、`LIKE`、`ORDER BY`、`LIMIT`。
2. 参数绑定为什么仍然适用于动态筛选条件。
3. Qt `QAbstractTableModel` 的 `rowCount()`、`columnCount()`、`data()`。
4. `QTableView` 与 Model/View 的职责边界。
5. `QSignalBlocker` 如何避免刷新控件时产生递归信号。
6. 自动识别结果和手动绑定结果为什么可以共用一张链接表。

## 当前限制

- 当前查询由 UI 线程同步执行，记录量很大时可能短暂影响界面。
- 当前主要展示检测框记录，没有单独展示“无人脸帧”。
- 客户端已经增加独立的 `Events` 页签，用于查询 `face_recognition_events`。
- 当前还没有分页、导出 CSV、点击记录回放原始帧等功能。

Events 页签当前支持按会话、事件类型、视频源和人员关键字筛选，并显示事件时间、轨迹、人员、相似度、阈值、来源和帧信息。它展示的是经过轨迹级去重的业务记录，不替代 History 的逐帧检测记录。

后续应把 SQLite 写入和历史查询都逐步迁移到独立的数据服务或后台线程，避免 UI 线程承担高频磁盘操作。
