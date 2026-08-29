# Documentation Index

The repository mainline is a face detection, recognition, tracking, and event
management platform.

```text
Camera / RTSP / video file
  -> video stream processing
  -> face detection
  -> face tracking
  -> face recognition
  -> event analysis
  -> SQLite storage
  -> alerts and client management
```

## Recommended Reading Order

1. `face_platform_scope.md`: product boundary and current priorities.
2. `module-0-architecture.md`: application, video, inference, recognition,
   and storage layers.
3. `module-18-yolo-opencv-dnn.md`: OpenCV DNN face detection.
4. `module19-detection-preview.md`: synchronized image and detection display.
5. `module-20-face-recognition-closure.md`: SFace recognition, reference
   cropping, and recognition events.
6. `module-21-face-tracking.md`: tracking parameters, lifecycle, duration,
   first/last recognition state, and event deduplication.
7. `module-8-sqlite-storage.md`: detection records, face gallery, feature
   templates, tracks, and events.
8. `module-9-history-query.md`: History queries and identity association.
9. `test-resources-face-and-mot20.md`: test resources.
10. `test-issues-and-solutions.md`: common troubleshooting steps.

## Current Completion

- FFmpeg local video and RTSP decoding.
- Separate display and inference queues.
- OpenCV DNN YOLO face detection.
- Synchronized Detection Preview.
- Faces page with identity records, reference images, and notes.
- OpenCV `FaceRecognizerSF` with SFace ONNX recognition.
- Feature fingerprints and automatic template rebuild after model/parameter
  changes.
- Recognition availability diagnostics and Active Configuration status.
- Lightweight face tracking with hot-applied, persisted parameters.
- Track duration, first recognition state, and last recognition state.
- Track data in History, Events, JSON, CSV, and the control protocol.
- SQLite frame records, track summaries, and track-level event deduplication.
- History and Events filter reset, confirmed deletion, and transactional cleanup
  of linked records.

## Deferred

- Audio integration.
- TensorRT as the primary inference backend.
- Industrial defect detection models and demo flow.
