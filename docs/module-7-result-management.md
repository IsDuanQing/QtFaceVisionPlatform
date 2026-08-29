# 模块 7：检测结果管理与统计

模块 6 已经把人脸框画到了界面上，但结果不能只停留在 UI。人脸平台后续还需要缓存、查询、统计、存储、发送和识别关联。

因此模块 7 提供一个不依赖 Qt 的 `ResultManager`。

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

如果把统计和最近结果缓存直接写进 `MainWindow`，以后接 SQLite、网络发送和事件分析时，结果处理逻辑会继续堆在 UI 类里。

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
  +--> Event Analysis
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

- `processedFrames`：ResultManager 接收到的推理帧批次数。
- `framesWithDetections`：至少有一个目标的帧数。
- `totalObjects`：累计检测目标数量。
- `latestFrameIndex`：最近一批结果对应的帧号。
- `latestPtsMs`：最近一批结果的时间戳。
- `classCounts`：按类别统计目标数量。

累计统计不会因为最近缓存淘汰而减少。缓存是查询用的，统计是运行期指标，两者职责不同。

## 为什么要保存 frameIndex、ptsMs、sourceId 和 trackId

人脸框只有坐标和置信度还不够。平台还需要知道：

1. 结果来自哪一路视频源或相机。
2. 结果对应哪一帧。
3. 结果在视频时间轴上的位置。
4. 结果是否属于同一条短期人脸轨迹。

这些字段会直接影响历史回溯、多相机区分、轨迹级识别事件去重和网络端判断结果是否过期。

## 与识别结果的关系

`DetectionResult` 现在包含 `FaceRecognitionResult face` 字段。

`ResultManager` 不负责识别算法，也不直接生成事件。它只缓存已经归一化好的检测结果。识别成功后，结果可以继续进入：

- UI：显示人员姓名和相似度。
- SQLite：写入 `detection_face_links`。
- 事件层：写入去重后的 `face_recognition_events`。

这样设计可以保持职责清晰：缓存是缓存，识别是识别，事件是事件。

## 当前 UI 接入

界面显示简化指标：

```text
当前帧检测数 / 累计检测数
```

这不是历史查询功能，只是为了验证 `ResultManager` 已经收到推理结果。完整历史查询由模块 9 和 SQLite 负责。

## 建议阅读顺序

1. `modules/common/include/common/DetectionResult.h`
2. `modules/common/include/common/FaceRecognitionResult.h`
3. `modules/results/include/results/ResultManager.h`
4. `modules/results/src/ResultManager.cpp`
5. `modules/playback/src/VideoPlayer.cpp` 的 `inferenceLoop()`
6. `apps/viewer/MainWindow.cpp` 的 `displayDetections()`

重点思考：

1. 为什么 `ResultManager` 使用 `std::mutex`？
2. 为什么最近记录需要设置容量上限？
3. 为什么累计统计不能随着缓存淘汰一起减少？
4. 如果多个线程同时查询 `recentResults()`，当前实现是否安全？
5. 为什么 `ResultManager` 不应该直接访问 SQLite？

## 当前限制

- 多路 sourceId 的独立统计还比较轻量。
- 没有按人员身份做实时统计。
- 没有独立事件统计。
- 结果量很大时，返回 `DetectionResults` 的复制成本可能需要优化。

模块 8 已经完成 SQLite 检测与识别结果存储，模块 20 已经补上识别闭环和事件去重。
