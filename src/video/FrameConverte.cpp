#include "FrameConverter.h"



FrameConverter::FrameConverter()
{
    swsCtx = nullptr;

    width = 0;

    height = 0;
}



FrameConverter::~FrameConverter()
{

    if(swsCtx)
    {
        sws_freeContext(swsCtx);
    }

}



QImage FrameConverter::convert(AVFrame* frame)
{

    if(frame == nullptr)
    {
        return QImage();
    }


    /*
        第一次调用创建转换器
    */

    if(swsCtx == nullptr ||
       width != frame->width ||
       height != frame->height)
    {

        width = frame->width;

        height = frame->height;


        sws_freeContext(swsCtx);


        swsCtx =
            sws_getContext(

                width,
                height,
                static_cast<AVPixelFormat>(frame->format),


                width,
                height,
                AV_PIX_FMT_RGB24,


                SWS_BILINEAR,

                nullptr,
                nullptr,
                nullptr
            );

    }



    if(swsCtx == nullptr)
    {
        return QImage();
    }



    QImage image(
        width,
        height,
        QImage::Format_RGB888
    );



    uint8_t* dstData[4];

    int dstLinesize[4];



    av_image_fill_arrays(

        dstData,

        dstLinesize,

        image.bits(),

        AV_PIX_FMT_RGB24,

        width,

        height,

        1
    );



    sws_scale(

        swsCtx,


        frame->data,

        frame->linesize,


        0,

        height,


        dstData,

        dstLinesize

    );



    return image;

}
