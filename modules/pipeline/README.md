pipeline
========

Inspection workflow orchestration layer.

Responsibilities:

- Connect capture, decode, preprocess, inference, postprocess, storage, and device modules.
- Thread model ownership.
- Queue and backpressure strategy.
- Start, stop, pause, and error recovery.
- Runtime statistics such as FPS, latency, and dropped frames.
