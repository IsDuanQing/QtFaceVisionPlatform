#ifndef IVP_COMMON_VIDEOFRAME_H
#define IVP_COMMON_VIDEOFRAME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ivp
{

// 图片类型
enum class PixelFormat // 枚举->只用来做类型标记、状态标记、选项判断
{
    Unknown,
    RGB24
}; // 起到标识作用，不参与运算

// 视频帧描述信息
struct VideoFrameMetadata // 结构体->一次性封装可修改的业务数据
{
    int width = 0;
    int height = 0;
    std::int64_t ptsMs = 0; // 显示时间戳->用于播放同步/多路摄像头同步
    std::int64_t frameIndex = 0; // 帧编号->调试：第1000帧检测失败
    std::string sourceId; // 视频来源->多摄像头
};

// 这是由 CPU 管理的视频帧，可以被解码、播放、推理、存储模块共享。
// The first stable format is RGB24 because it is easy to display and easy to
// adapt into OpenCV preprocessing.
struct VideoFrame
{
    VideoFrameMetadata metadata;
    PixelFormat pixelFormat = PixelFormat::Unknown; // 数据格式->RGB24
    int strideBytes = 0; // 图片的一行数据量（width*3），需要CPU对齐
    std::vector<std::uint8_t> data; // 帧数据

    // 判断frame是否有效
    bool empty() const
    {
        return data.empty() || metadata.width <= 0 || metadata.height <= 0
            || strideBytes <= 0 || pixelFormat == PixelFormat::Unknown;
    }

    // 获取指定行首地址
    // data.data()->返回缓冲区首元素指针
    std::uint8_t* rowData(int row) // 行号
    {
        // 整张图片内存的起始地址 + 行号*每行数据量
        return data.data() + static_cast<std::size_t>(row) * strideBytes;
    }

    const std::uint8_t* rowData(int row) const
    {
        return data.data() + static_cast<std::size_t>(row) * strideBytes;
    }
};

} // namespace ivp

#endif // IVP_COMMON_VIDEOFRAME_H
