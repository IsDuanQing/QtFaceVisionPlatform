# 模块 7：检测结果管理与统计

模块 6 已经把检测框画到了界面上，但结果不能只停留在 UI。工业视觉平台后续还需要保存、查询、统计、发送检测结果。

因此模块 7 新增一个不依赖 Qt 的 `ResultManager`。

## 当前数据流

```text
VideoPlayer::inferenceLoop()
  |
  v
DetectionResults + frameIndex + ptsMs + sourceId
  |
  v
VideoPlayer::detectionResultsReady
  |
  v
MainWindow
  |
  +--> VideoDisplayWidget
  |
  +--> ResultManager
          |
          +--> 最近检测记录
          +--> 按帧查询
          +--> 累计统计
```

## 为什么单独做 ResultManager

如果把统计代码直接写进 `MainWindow`，以后接 SQLite 或网络发送时，结果处理逻辑会继续堆在 UI 类里。

`ResultManager` 负责结果生命周期和查询，UI 只负责展示：

```cpp
resultManager_.addFrameResults(sourceId, frameIndex, ptsMs, results);
```

后面可以增加不同的消费者：

```text
ResultManager
  |
  +--> Qt UI
  +--> SQLite Storage
  +--> TCP/JSON Publisher
  +--> Statistics
```

## 当前核心类型

### ResultManagerConfig

```cpp
struct ResultManagerConfig
{
    std::size_t maxStoredResults = 1000;
};
```

当前采用有界缓存，避免 RTSP 长时间运行时内存无限增长。

### DetectionSummary

统计内容包括：

- `processedFrames`：ResultManager 接收到的推理帧批次数
- `framesWithDetections`：至少有一个目标的帧数
- `totalObjects`：累计检测目标数量
- `latestFrameIndex`：最近一批结果对应的帧号
- `latestPtsMs`：最近一批结果的时间戳
- `classCounts`：按类别统计目标数量

累计统计不会因为最近缓存淘汰而减少。缓存是查询用的，统计是运行期指标，两者职责不同。

## 为什么要保存 frameIndex、ptsMs、sourceId

检测框只有坐标和置信度还不够。工业场景通常还需要知道：

1. 结果来自哪一路相机。
2. 结果对应哪一帧。
3. 结果在视频时间轴上的位置。

这三个字段会直接影响历史回溯、多相机区分和网络端判断结果是否过期。

## 当前 UI 接入

界面显示一个简化指标：

```text
当前帧检测数 / 累计检测数
```

这不是历史查询功能，只是为了验证 ResultManager 已经收到推理结果。

## 建议阅读顺序

1. `modules/common/include/common/DetectionResult.h`
2. `modules/results/include/results/ResultManager.h`
3. `modules/results/src/ResultManager.cpp`
4. `modules/playback/src/VideoPlayer.cpp` 的 `inferenceLoop()`
5. `apps/viewer/MainWindow.cpp` 的 `displayDetections()`

重点思考：

1. 为什么 `ResultManager` 使用 `std::mutex`？
2. 为什么最近记录需要设置容量上限？
3. 为什么累计统计不能随着缓存淘汰一起减少？
4. 如果多个线程同时查询 `recentResults()`，当前实现是否安全？
5. 如果结果量很大，返回 `DetectionResults` 的复制成本会不会成为问题？

## 当前限制

当前模块还没有：

- SQLite 持久化
- JSON 序列化
- 历史结果界面
- 检测任务 ID
- 多路 sourceId 的独立统计

这些功能应该在结果管理边界稳定后逐步增加。

## 下一步建议

下一步可以进入模块 8：SQLite 检测结果存储。

模块 8 不应该直接读取 UI 控件，而应该把 `ResultManager` 输出的结构化结果写入数据库，并提供按时间、类别、sourceId 查询的接口。
