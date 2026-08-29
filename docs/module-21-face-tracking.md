# Module 21: Face Tracking and Track-Level Events

## Goals

This module adds short-term face tracks above frame-by-frame detection and
recognition:

- Keep a stable `trackId` while the same face remains visible.
- Recover a track after a short miss or occlusion when possible.
- Deduplicate repeated recognition events inside one track.
- Store track duration, first recognition state, and last recognition state.
- Create a new track and new event when a face leaves and later re-enters.

Current processing order:

```text
YOLO face detection
  |
  v
FaceTracker assigns trackId
  |
  v
SFace feature extraction and identity matching
  |
  v
Track first/last recognition state update
  |
  v
SQLite detection records, track summaries, and events
```

## Module Boundaries

The tracking implementation is located at:

- `modules/tracking/include/tracking/FaceTracker.h`
- `modules/tracking/src/FaceTracker.cpp`
- `modules/common/include/common/FaceTrack.h`

`FaceTracker` is a pure C++17 module. It does not depend on Qt, OpenCV, or
SQLite. It owns geometric association and track lifecycle. `VideoPlayer`
connects detection, tracking, and recognition in order. SQLite owns
persistence.

## Matching Algorithm

Each active track stores the latest detection box, a simple velocity estimate,
the last observed timestamp, and consecutive missed-update count. For each new
detection batch, the tracker:

1. Predicts the current box from the previous position and velocity.
2. Calculates IoU between the predicted box and detection box.
3. Calculates normalized center distance.
4. Rejects candidates whose IoU is too low and center distance is too large.
5. Performs greedy one-to-one matching by `0.70 * IoU + 0.30 * center score`.
6. Reuses the old `trackId` for matched detections.
7. Creates a new `trackId` for unmatched detections.
8. Closes tracks that exceed the miss count or lost duration.

This is a lightweight short-term tracker, not a full ReID tracker. Face
crossing, fast motion, and long occlusion can still cause track switches.

## Parameters

The Face Tracking section in Parameters contains:

| Parameter | Default | Meaning | Tuning direction |
| --- | ---: | --- | --- |
| Min IoU | 0.12 | Minimum overlap considered useful for association | Higher reduces false association but can break tracks |
| Max Center | 0.75 | Maximum normalized center distance | Higher tolerates faster movement but can cause identity switches |
| Max Misses | 8 | Consecutive actual detection updates allowed without a match | Higher tolerates short occlusion but keeps stale tracks longer |
| Lost ms | 1500 | Maximum time since the last matched detection | Higher tolerates longer occlusion but delays new-track creation |

Click Parameters Apply after changing values. The current active tracks are
closed cleanly, then new tracks use the new configuration. Values are written
to `settings.ini` and restored on the next launch.

Active Configuration displays the values currently used by the player:

```text
Tracking: IoU 0.12 | Center 0.75 | Max Misses 8 | Lost 1500 ms
```

Mouse-wheel changes are disabled for these numeric controls to prevent
accidental edits while scrolling the panel.

## Track State

`FaceTrackSnapshot` stores:

- First and last frame index and PTS.
- `durationMs`, calculated as last detection PTS minus first detection PTS.
- Detection count and consecutive misses at close time.
- Whether the track is active.
- First recognition state and last recognition state.

Each recognition state contains:

- Whether a recognition result is available.
- Whether a gallery identity was matched.
- Identity ID, code, and display name.
- Decision such as `matched`, `unknown`, or `low_similarity`.
- Similarity, threshold used at that observation, and observation PTS.

Video labels show the track number and current duration in seconds. History
and Events include Duration, First State, and Last State columns. Hovering a
row shows the full track summary.

## SQLite

The `face_tracks` table uniquely identifies a track with:

```text
session_id + source_id + track_id
```

The active summary is updated with each detected frame. A final
`active = 0` snapshot is written when a track expires, playback stops, the
video ends, the source changes, or detector/tracker parameters are applied.

`recentResults()`, `resultsForFrame()`, History, and Events all join
`face_tracks`, so frame queries and UI lists use the same duration and first/
last recognition state.

## Event Deduplication

When `trackId > 0`, the event deduplication key is:

```text
session_id
source_id
track_id
event_type
face_identity_id
```

Repeated recognition of the same identity within one track creates one
`face_recognized` event. Repeated Unknown results create one `face_unknown`
event. A transition from Unknown to Recognized, or a change of identity,
creates a new event.

When `trackId == 0`, legacy data and abnormal paths fall back to the original
time-cooldown strategy. Per-frame `detection_records` are still preserved.

## Qt Creator Verification

1. Open a video containing two faces and enter Detection Preview.
2. Confirm that each face keeps a stable `T<number>` label.
3. Change a tracking value in Parameters and click Apply.
4. Confirm Active Configuration displays the new value.
5. Briefly occlude one face; within Lost ms it should keep its track when
   association succeeds.
6. Keep the face away longer than Lost ms; re-entry should create a new track.
7. Stop playback, then refresh History and Events.
8. Check Duration, First State, and Last State.
9. Verify that one track, one event type, and one identity produce one
   deduplicated event.

## Current Limitations and Next Step

The current tracker uses geometry only. SFace similarity can be added as a
secondary association signal to reduce track swaps when faces cross. A
ByteTrack or ReID-based tracker should be evaluated only after multi-person
scenarios require it.
