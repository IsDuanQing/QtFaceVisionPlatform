# 模块 13：检测参数持久化与启动恢复

模块 12 已经把人脸检测参数放到了界面上，但这些值不能只存在于内存中。程序关闭后，用户设置的阈值、模型路径、导出目录和网络发送参数都应该能恢复。

模块 13 的目标是把客户端设置保存到本地 INI 文件，并在下次启动时自动恢复。

## 为什么要做配置持久化

人脸平台调试时会反复修改：

- 检测阈值。
- NMS 阈值。
- 抽帧间隔。
- ONNX / labels 路径。
- 导出目录。
- 网络发送地址。

如果每次启动都要重新设置，测试过程很容易混乱。配置持久化解决三个问题：

1. 用户设置可以跨启动保留。
2. 程序默认值和用户值有清晰边界。
3. 后续接远程任务模板或客户端配置页面时有统一入口。

## 当前实现范围

新增 `ViewerSettingsStore`：

- 使用 `QSettings`。
- 使用 INI 文件格式。
- 默认保存到 `QStandardPaths::AppLocalDataLocation/settings.ini`。
- 读取失败或配置不存在时回退到程序默认值。
- 对数值范围做基础保护，避免配置文件被手动改坏后直接污染 UI。
- 自动迁移旧缺陷模型路径到当前人脸模型默认路径。

当前保存的内容：

- 置信度阈值。
- NMS 阈值。
- 最大检测数量。
- 检测抽帧间隔。
- YOLO 输入宽高。
- 类别数量。
- ONNX 路径。
- labels 路径。
- 导出开关、目录、格式。
- 网络发送开关、地址、端口。

## 旧配置迁移

旧版本可能保存过：

```text
models/yolo11l/defect.onnx
models/yolo11l/labels.txt
Input: 1088 x 1088
```

当前人脸模型需要：

```text
models/yolov8-face/face.onnx
models/yolov8-face/labels.txt
Input: 640 x 640
Classes: 1
```

`ViewerSettingsStore::load()` 会识别旧模型路径，并回退到默认人脸模型配置。这样可以减少 OpenCV DNN reshape 错误进入推理线程的概率。

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

点击 `Reset` 时：

```text
程序默认 ViewerSettings
  |
  v
回填 UI 控件
  |
  v
VideoPlayer::setDetectorConfig()
  |
  v
立即保存到 settings.ini
```

## UI 交互边界

设置持久化只保存结构化值，不保存控件状态细节。

当前 UI 已经对数值控件做了滚轮保护，避免鼠标滚轮经过输入框时误改阈值、输入尺寸或类别数。这属于界面交互优化，不属于 `ViewerSettingsStore` 的职责。

职责边界是：

- `MainWindow`：控件创建、样式、滚轮保护、值收集和回填。
- `ViewerSettingsStore`：INI 文件读写、默认值兜底、基础范围保护。
- `VideoPlayer`：使用 `DetectorConfig` 初始化运行时检测器。

## 当前限制

- 没有保存最近打开的视频或 RTSP 地址。
- 没有保存 Faces 页签表格选中行。
- 没有保存 History 查询条件。
- 配置文件版本迁移还比较轻量，只处理当前已知旧模型路径。
- 参数热应用仍由 `Apply` 触发，启动恢复只负责回填和设置当前配置。

## Qt Creator 手工验收

1. 启动程序。
2. 点击 `Face` 预设。
3. 修改 `Confidence`、`NMS`、`Every N` 或模型路径。
4. 关闭程序。
5. 重新启动程序，确认这些值被恢复。
6. 点击 `Reset`，确认参数回到默认值。
7. 再次关闭并启动，确认默认值被保存。

## 学习重点

1. 读 `ViewerSettingsStore::load()`，理解默认值如何兜底。
2. 读 `ViewerSettingsStore::save()`，理解配置落盘的时机。
3. 读 `MainWindow::restoreViewerSettings()`，理解启动恢复流程。
4. 读 `MainWindow::collectViewerSettings()`，理解 UI 状态如何变成结构化配置。
5. 思考为什么配置文件逻辑不应该写进 `VideoPlayer` 或 `YoloOpenCVDnnDetector`。
