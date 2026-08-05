inference
=========

Model inference layer.

Responsibilities:

- TensorRT engine loading.
- CUDA buffer and execution context ownership.
- Input and output tensor description.
- Batch execution boundaries.
- Backend-neutral inference interfaces.

This module should not depend on Qt UI code.
