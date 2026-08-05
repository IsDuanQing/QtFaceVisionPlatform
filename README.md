IndustrialVisionPlatform
========================

A minimal C++17/CMake project template for an industrial vision platform.

Layout
------

- `CMakeLists.txt`: top-level build entry.
- `apps/vision_app`: runnable demo application.
- `apps/service_app`: headless service entry placeholder.
- `apps/benchmark_app`: benchmark entry placeholder.
- `cmake`: custom CMake helper modules.
- `modules/core`: core platform abstractions and data types.
- `modules/common`: reusable common library.
- `modules/capture`: camera, RTSP, file, and image source acquisition.
- `modules/decode`: FFmpeg-based video decoding.
- `modules/preprocess`: model input preprocessing.
- `modules/inference`: TensorRT and model inference boundaries.
- `modules/postprocess`: YOLO decode, NMS, ROI, and rule checks.
- `modules/pipeline`: inspection workflow orchestration.
- `modules/storage`: image, result, video, and traceability persistence.
- `modules/ui`: Qt user interface layer.
- `modules/device`: PLC, IO, light, trigger, and camera SDK control.
- `configs`: runtime configuration files.
- `models`: ONNX, TensorRT engine, and label artifacts.
- `resources`: icons, styles, fonts, images, and Qt resources.
- `third_party`: third-party dependency notes or vendored sources.
- `tests`: top-level integration, performance, and system tests.
- `tools`: model, video, calibration, and benchmark helper tools.
- `scripts`: build, deploy, environment, and maintenance scripts.
- `docs`: architecture, design, operations, and troubleshooting docs.
- `deploy`: systemd, Docker, package, and runtime deployment assets.

Build
-----

```bash
cmake -S . -B build
cmake --build build
```

Run
---

```bash
./build/apps/vision_app/ivp_vision_app configs/app.conf
```
