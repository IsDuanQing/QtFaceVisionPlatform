decode
======

Video decoding layer.

Responsibilities:

- FFmpeg integration.
- H264/H265 decoding.
- Timestamp handling.
- Hardware decode extension points such as NVDEC.

If a camera source already outputs raw images, the pipeline can bypass this module.
