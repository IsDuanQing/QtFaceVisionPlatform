# 模块 11：图片文件夹模拟视频输入

> 历史文档：当前客户端主入口优先视频文件和 RTSP，图片序列只作为测试思路保留。

模块 1 到模块 10 已经把 MP4、RTSP、Mock 检测、SQLite 和历史查询打通。
模块 11 要做的是让一个图片文件夹也能像视频一样播放，作为没有工业相机
时的稳定输入源。

## 为什么要做这一层

工业视觉项目里，输入源并不只有视频文件和 RTSP：

- 产线现场可能直接喂工业相机
- 测试阶段经常只有一批缺陷图片
- 算法调试时需要对固定图片集反复回放

如果把“图片文件夹”做成独立输入源，后面的显示、推理、存储、历史查询
都不用改，只要继续消费统一的 `VideoFrame`。

## 当前实现范围

本模块新增了 `ImageSequenceReader`：

- 扫描目录中的图片文件
- 按文件名排序
- 把每张图片转换成 RGB24 `VideoFrame`
- 为每帧生成 `frameIndex` 和 `ptsMs`
- 用固定 10 FPS 模拟视频节奏

当前支持的扩展名：

```text
jpg / jpeg / png / bmp / tif / tiff
```

当前只扫描所选目录的第一层，不递归读取子目录。

## 数据流

```text
Image directory
  |
  v
ImageSequenceReader
  |  QImage -> RGB24 VideoFrame
  v
VideoPlayer
  |  display queue / inference queue
  v
UI / Detector / SQLite
```

## 设计要点

1. 图片序列不是视频文件，所以不要塞进 `FFmpegDecoder`。
2. 输入源的差异要收敛在 `VideoInputConfig` 和 `VideoPlayer`。
3. 上层只依赖 `VideoFrame`，不要知道数据来自 JPG 还是 MP4。
4. 图片序列的时间戳由固定 FPS 决定，不依赖真实媒体时间基。

## 模块边界

`ImageSequenceReader` 负责：

- 扫描目录
- 排序
- 图片读取
- RGB24 转换
- 帧元数据填充

`VideoPlayer` 负责：

- 根据输入类型选择读取器
- 启动生产者线程
- 把读取到的帧送入显示和推理队列

`MainWindow` 负责：

- 提供“打开图片文件夹”入口
- 显示当前目录路径
- 复用已有播放和检测界面

## 当前测试

`tests/video/ImageSequenceReaderTest.cpp` 覆盖：

- 目录扫描
- 文件名排序
- RGB24 转换
- 帧号与时间戳生成
- 回到起点的行为

## Qt Creator 手工验收

1. 编译并运行 Qt demo。
2. 点击 `Open Images`。
3. 选择：

```text
F:\DataSet\guangdong1_round1_testA_20190818
```

4. 预期看到：

- 状态变为 `Playing`
- FPS 显示 `10.0 fps`
- Audio 显示 `No Audio`
- 图片按文件名顺序连续显示
- Mock 检测框、SQLite 记录和历史查询继续工作

5. 再测试暂停、继续、停止和从头播放。

## 学习顺序

1. 先看 `VideoInputConfig`，理解输入源抽象。
2. 再看 `ImageSequenceReader`，理解图片如何变成视频帧。
3. 然后看 `VideoPlayer`，理解不同输入源如何统一进入同一条播放管线。
4. 最后看 UI 层，理解按钮只是入口，真正的业务不应该堆在窗口类里。
