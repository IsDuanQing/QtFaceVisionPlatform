# 模块 13：检测参数持久化与启动恢复

模块 12 已经把检测参数放到了界面上，但这些值还只存在于内存中。
程序一关闭，用户设置的阈值、模型路径和图片序列 FPS 就会丢失。

模块 13 的目标是把这些参数保存到本地配置文件，并在下次启动时自动恢复。

## 为什么要做配置持久化

工业软件通常不是一次性 demo。现场调试人员会反复打开程序、修改参数、
重启程序、继续验证。如果每次启动都要重新选择模型路径、重新设置阈值，
使用体验会很差，也容易产生测试误差。

本模块解决三个问题：

- 用户设置可以跨启动保留
- 默认值和用户值有清晰边界
- 后续接网络配置、任务模板、产线配方时有统一入口

## 当前实现范围

新增 `ViewerSettingsStore`：

- 使用 `QSettings`
- 使用 INI 文件格式
- 默认保存到 `QStandardPaths::AppLocalDataLocation/settings.ini`
- 读取失败或配置不存在时回退到程序默认值
- 对数值范围做基础保护，避免配置文件被手动改坏后直接污染 UI

当前保存的内容：

- 检测后端：`Mock` / `TensorRT`
- 置信度阈值
- NMS 阈值
- 最大检测数量
- 检测抽帧间隔
- YOLO 输入宽高
- 类别数量
- Mock 推理延迟
- ONNX 路径
- TensorRT Engine 路径
- labels 路径
- 图片文件夹模拟视频 FPS

## 数据流

启动时：

```text
MainWindow 构造
  |
  v
保存一份程序默认 ViewerSettings
  |
  v
ViewerSettingsStore::load(defaults)
  |
  v
回填 UI 控件
  |
  v
VideoPlayer::setDetectorConfig()
```

退出时：

```text
MainWindow 析构
  |
  v
collectViewerSettings()
  |
  v
ViewerSettingsStore::save()
  |
  v
settings.ini
```

点击 `Restore Defaults` 时：

```text
程序默认 ViewerSettings
  |
  v
回填 UI 控件
  |
  v
立即保存到 settings.ini
```

## 工程边界

`ViewerSettingsStore` 属于 Qt 客户端层，而不是 `modules/playback` 或
`modules/inference`。

原因是：

- `VideoPlayer` 只应该知道当前使用哪份 `DetectorConfig`
- `IDetector` 只应该知道如何解释检测配置
- 配置文件路径、INI 格式、UI 默认值都属于客户端程序行为

这样后续如果服务端也需要配置，可以再增加独立的服务端配置模块，
而不是让推理模块依赖 Qt 的 `QSettings`。

## 当前限制

- 目前只保存检测参数和图片序列 FPS。
- 没有保存最近打开的视频、RTSP 地址或图片文件夹。
- 没有做运行中热切换，参数仍然在下一次打开输入源时生效。
- 没有做配置文件版本迁移，只预留了 `schemaVersion` 字段。

## Qt Creator 手工验收

1. 启动程序。
2. 修改 `Confidence`、`NMS`、`Image FPS` 或模型路径。
3. 关闭程序。
4. 重新启动程序，确认这些值被恢复。
5. 点击 `Restore Defaults`，确认参数回到默认值。
6. 再次关闭并启动，确认默认值被保存。

## 学习重点

1. 读 `ViewerSettingsStore::load()`，理解默认值如何兜底。
2. 读 `ViewerSettingsStore::save()`，理解配置落盘的时机。
3. 读 `MainWindow::restoreViewerSettings()`，理解启动恢复流程。
4. 读 `MainWindow::collectViewerSettings()`，理解 UI 状态如何变成结构化配置。
5. 思考为什么配置文件逻辑不应该写进 `VideoPlayer`。
