ui
==

Qt user interface layer.

Responsibilities:

- Live image display.
- Detection overlay rendering.
- Parameter pages.
- Device and pipeline state display.
- Alarm and history views.

UI should communicate through pipeline/application services instead of calling FFmpeg or TensorRT directly.
