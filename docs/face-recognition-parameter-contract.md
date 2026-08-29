# Face Recognition Parameter Contract

## Purpose

The face gallery stores feature vectors extracted by a specific feature
pipeline. A vector is usable only when the active pipeline is compatible with
the pipeline that produced it.

## Feature fingerprint

`FaceRecognizer::featureFingerprint()` identifies the feature pipeline. It
includes:

- SFace model name and normalized model path
- Model file size and last write time
- Feature crop padding
- Minimum accepted face size
- Feature normalization width and height
- Reference detector configuration signature

The similarity threshold and minimum margin are intentionally excluded. They
change the matching decision, not the feature vector, so changing either one
does not rebuild the gallery.

## Automatic rebuild

When templates are loaded from SQLite:

1. The active recognizer calculates its fingerprint.
2. Templates with a different model or fingerprint are rejected.
3. Diagnostics mark the gallery as `galleryNeedsRebuild`.
4. The Qt client extracts new templates from each stored reference image.
5. New templates replace the old templates in one SQLite transaction.
6. The gallery is loaded again and the active and stored fingerprints are
   displayed in the recognition status tooltip.

If a reference image cannot be processed, the existing database templates are
left untouched for that identity.

## UI verification

The Parameters page shows `Pending Apply` when a recognition control differs
from the configuration currently owned by `VideoPlayer`. After Apply, the
status is calculated from the active player configuration rather than from the
input widgets.

Changing threshold or margin should keep the feature fingerprint unchanged.
Changing padding, minimum face size, feature model, or detector settings should
cause the reference gallery to be rebuilt.
