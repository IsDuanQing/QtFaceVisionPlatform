# 模块 9：历史检测记录查询界面

模块 8 已经把检测会话和检测框写入 SQLite。模块 9 负责把这些数据重新查询出来，并在 Qt 界面中展示。

## 设计目标

本模块先完成一个可用的历史查询闭环：

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

`MainWindow` 负责收集筛选条件和触发查询，SQL 细节仍然留在 `modules/storage`。

## 查询条件

当前支持：

- 按检测会话筛选
- 按 sourceId 或输入地址模糊筛选
- 按缺陷类别模糊筛选
- 按记录时间的起止范围筛选
- 限制返回记录数量

查询结果按记录时间倒序排列，默认最多显示 200 条。

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

表示一条历史缺陷记录，同时带有会话、帧和目标框信息，避免 Qt 层再访问数据库。

## Qt Model/View

`DetectionHistoryTableModel` 继承自 `QAbstractTableModel`，负责：

- 把 `DetectionHistoryRow` 转换为表格单元格
- 提供列标题
- 格式化时间、PTS、置信度和目标框
- 提供工具提示显示完整输入地址

`QTableView` 只负责表格展示，不负责 SQL 和业务查询。

## 学习顺序

1. SQLite `SELECT`、`JOIN`、`LIKE`、`ORDER BY`、`LIMIT`
2. 参数绑定为什么仍然适用于动态筛选条件
3. Qt `QAbstractTableModel` 的 `rowCount()`、`columnCount()`、`data()`
4. `QTableView` 与 Model/View 的职责边界
5. `QSignalBlocker` 如何避免刷新控件时产生递归信号

## 当前限制

- 当前查询由 UI 线程同步执行，记录量很大时可能短暂影响界面。
- 当前只展示检测框记录，没有单独展示“无缺陷帧”。
- 当前还没有分页、导出 CSV、点击记录回放原始帧等功能。

后续应把 SQLite 写入和历史查询都逐步迁移到独立的数据服务或后台线程，避免 UI 线程承担高频磁盘操作。
