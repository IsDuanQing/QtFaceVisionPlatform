#include <cassert>
#include <cstdio>
#include <algorithm>

#include "storage/SQLiteDetectionStorage.h"

int main()
{
    const char* databasePath = "ivp_storage_smoke_test.db";
    std::remove(databasePath);

    ivp::SQLiteDetectionStorage storage;
    assert(storage.open(databasePath));

    const std::int64_t sessionId =
        storage.startSession("camera_a", "rtsp://127.0.0.1:8554/test");
    assert(sessionId > 0);

    ivp::DetectionResult defect;
    defect.sourceId = "camera_a";
    defect.frameIndex = 42;
    defect.ptsMs = 1400;
    defect.trackId = 17;
    defect.classId = 3;
    defect.className = "scratch";
    defect.confidence = 0.91F;
    defect.box = ivp::BoundingBox{12.0F, 18.0F, 50.0F, 26.0F};

    assert(storage.saveFrameResults(sessionId, "camera_a", 42, 1400, {defect}));
    assert(storage.saveFrameResults(sessionId, "camera_a", 43, 1433, {}));
    assert(storage.finishSession(sessionId));

    const ivp::DetectionResults frameResults =
        storage.resultsForFrame(sessionId, 42);
    assert(frameResults.size() == 1);
    assert(frameResults.front().className == "scratch");
    assert(frameResults.front().trackId == 17);

    const ivp::DetectionResults recentResults = storage.recentResults(5);
    assert(recentResults.size() == 1);
    assert(recentResults.front().frameIndex == 42);

    const ivp::InspectionSessionSummaries sessions = storage.recentSessions(5);
    assert(sessions.size() == 1);
    assert(sessions.front().sessionId == sessionId);
    assert(sessions.front().frameCount == 2);
    assert(sessions.front().objectCount == 1);
    assert(sessions.front().endedAtMs.has_value());

    const ivp::DetectionHistoryRows recentHistory = storage.recentHistory(5);
    assert(recentHistory.size() == 1);
    assert(recentHistory.front().sessionId == sessionId);
    assert(recentHistory.front().frameIndex == 42);
    assert(recentHistory.front().trackId == 17);
    assert(recentHistory.front().frameObjectCount == 1);

    ivp::DetectionHistoryQuery query;
    query.sessionId = sessionId;
    query.sourceLike = std::string("camera");
    query.classLike = std::string("SCR");
    query.limit = 5;
    const ivp::DetectionHistoryRows filteredHistory = storage.queryHistory(query);
    assert(filteredHistory.size() == 1);
    assert(filteredHistory.front().className == "scratch");

    ivp::FaceIdentityEntry face;
    face.faceCode = "person_001";
    face.displayName = "Alice";
    face.referenceImagePath = "faces/person_001.jpg";
    face.notes = "Test identity";
    assert(storage.saveFaceIdentity(face));

    const ivp::FaceIdentityEntries faces = storage.recentFaceIdentities(5);
    assert(faces.size() == 1);
    assert(faces.front().faceCode == "person_001");
    assert(faces.front().displayName == "Alice");
    assert(faces.front().faceId > 0);

    const ivp::DetectionHistoryRows historyBeforeBinding = storage.recentHistory(5);
    assert(historyBeforeBinding.size() == 1);
    assert(historyBeforeBinding.front().recordId > 0);
    assert(!historyBeforeBinding.front().faceId.has_value());

    assert(storage.bindFaceIdentity(
        historyBeforeBinding.front().recordId,
        faces.front().faceId));

    const ivp::DetectionHistoryRows historyAfterBinding = storage.recentHistory(5);
    assert(historyAfterBinding.size() == 1);
    assert(historyAfterBinding.front().faceId.has_value());
    assert(*historyAfterBinding.front().faceId == faces.front().faceId);
    assert(historyAfterBinding.front().faceCode == "person_001");
    assert(historyAfterBinding.front().faceName == "Alice");

    assert(storage.clearFaceIdentity(historyAfterBinding.front().recordId));
    const ivp::DetectionHistoryRows historyAfterClear = storage.recentHistory(5);
    assert(historyAfterClear.size() == 1);
    assert(!historyAfterClear.front().faceId.has_value());

    ivp::FaceFeatureTemplate feature;
    feature.faceId = faces.front().faceId;
    feature.faceCode = faces.front().faceCode;
    feature.faceName = faces.front().displayName;
    feature.sampleImagePath = "faces/person_001.jpg";
    feature.feature.modelName = "test_lbph";
    feature.feature.featureFingerprint = "test-feature-fingerprint-v1";
    feature.feature.values = {1.0F, 2.0F, 3.0F};
    ivp::FaceFeatureTemplate secondFeature = feature;
    secondFeature.sampleImagePath = "faces/person_001_side.jpg";
    secondFeature.feature.values = {4.0F, 5.0F, 6.0F};
    ivp::FaceFeatureTemplate thirdFeature = feature;
    thirdFeature.sampleImagePath = "faces/person_001_low_light.jpg";
    thirdFeature.feature.values = {7.0F, 8.0F, 9.0F};
    assert(storage.replaceFaceFeatures(
        faces.front().faceId,
        {feature, secondFeature, thirdFeature}));
    const ivp::FaceFeatureTemplates storedFeatures = storage.allFaceFeatures();
    assert(storedFeatures.size() == 3);
    assert(storedFeatures.front().feature.featureFingerprint
           == "test-feature-fingerprint-v1");
    assert(storedFeatures.front().feature.values.size() == 3);
    assert(storedFeatures.front().feature.values[1] == 2.0F);
    assert(storedFeatures[1].sampleImagePath == "faces/person_001_side.jpg");
    assert(storedFeatures[2].sampleImagePath
           == "faces/person_001_low_light.jpg");

    ivp::DetectionResult recognized;
    recognized.sourceId = "camera_a";
    recognized.frameIndex = 44;
    recognized.ptsMs = 1466;
    recognized.trackId = 501;
    recognized.classId = 0;
    recognized.className = "face";
    recognized.confidence = 0.97F;
    recognized.box = ivp::BoundingBox{20.0F, 24.0F, 80.0F, 80.0F};
    recognized.face.matched = true;
    recognized.face.faceId = faces.front().faceId;
    recognized.face.faceCode = faces.front().faceCode;
    recognized.face.faceName = faces.front().displayName;
    recognized.face.distance = 12.5F;
    recognized.face.similarity = 0.83F;
    recognized.face.threshold = 75.0F;
    recognized.face.matchedAtMs = 123456789;
    recognized.face.recognizerName = "test_lbph";
    recognized.face.decision = "matched";
    recognized.trackState.trackId = recognized.trackId;
    recognized.trackState.sourceId = recognized.sourceId;
    recognized.trackState.classId = recognized.classId;
    recognized.trackState.className = recognized.className;
    recognized.trackState.firstFrameIndex = 44;
    recognized.trackState.firstPtsMs = 1466;
    recognized.trackState.lastFrameIndex = 44;
    recognized.trackState.lastPtsMs = 1466;
    recognized.trackState.durationMs = 0;
    recognized.trackState.detectionCount = 1;
    recognized.trackState.firstRecognition.available = true;
    recognized.trackState.firstRecognition.matched = true;
    recognized.trackState.firstRecognition.faceId = faces.front().faceId;
    recognized.trackState.firstRecognition.faceName = faces.front().displayName;
    recognized.trackState.firstRecognition.decision = "matched";
    recognized.trackState.lastRecognition =
        recognized.trackState.firstRecognition;
    assert(storage.saveFrameResults(sessionId, "camera_a", 44, 1466, {recognized}));

    ivp::DetectionResult repeatedRecognized = recognized;
    repeatedRecognized.frameIndex = 45;
    repeatedRecognized.ptsMs = 1499;
    repeatedRecognized.face.matchedAtMs = 123456990;
    repeatedRecognized.trackState.lastFrameIndex = 45;
    repeatedRecognized.trackState.lastPtsMs = 1499;
    repeatedRecognized.trackState.durationMs = 33;
    repeatedRecognized.trackState.detectionCount = 2;
    assert(storage.saveFrameResults(
        sessionId,
        "camera_a",
        45,
        1499,
        {repeatedRecognized}));

    ivp::FaceTrackSnapshot endedTrack = repeatedRecognized.trackState;
    endedTrack.active = false;
    endedTrack.missedUpdates = 2;
    assert(storage.saveFaceTrackSnapshots(sessionId, {endedTrack}));

    const ivp::DetectionResults trackedFrameResults =
        storage.resultsForFrame(sessionId, 45);
    assert(trackedFrameResults.size() == 1);
    assert(trackedFrameResults.front().trackState.trackId == 501);
    assert(trackedFrameResults.front().trackState.durationMs == 33);
    assert(!trackedFrameResults.front().trackState.active);
    assert(trackedFrameResults.front().trackState.firstRecognition.decision
           == "matched");
    assert(trackedFrameResults.front().trackState.lastRecognition.faceName
           == "Alice");

    ivp::DetectionResult reenteredRecognized = recognized;
    reenteredRecognized.frameIndex = 46;
    reenteredRecognized.ptsMs = 1532;
    reenteredRecognized.trackId = 502;
    reenteredRecognized.face.matchedAtMs = 123457100;
    reenteredRecognized.trackState.trackId = reenteredRecognized.trackId;
    reenteredRecognized.trackState.firstFrameIndex = 46;
    reenteredRecognized.trackState.firstPtsMs = 1532;
    reenteredRecognized.trackState.lastFrameIndex = 46;
    reenteredRecognized.trackState.lastPtsMs = 1532;
    reenteredRecognized.trackState.durationMs = 0;
    reenteredRecognized.trackState.detectionCount = 1;
    assert(storage.saveFrameResults(
        sessionId,
        "camera_a",
        46,
        1532,
        {reenteredRecognized}));

    const ivp::DetectionHistoryRows autoHistory = storage.recentHistory(5);
    assert(autoHistory.size() == 4);
    const auto autoRow = std::find_if(
        autoHistory.begin(),
        autoHistory.end(),
        [](const ivp::DetectionHistoryRow& row) {
            return row.className == "face";
        });
    assert(autoRow != autoHistory.end());
    assert(autoRow->faceId.has_value());
    assert(autoRow->trackId > 0);
    assert(autoRow->faceSimilarity == 0.83F);
    assert(autoRow->faceRecognizerName == "test_lbph");
    assert(autoRow->trackDurationMs == 0);
    assert(autoRow->trackFirstDecision == "matched");
    assert(autoRow->trackLastFaceName == "Alice");

    const ivp::FaceRecognitionEvents events =
        storage.recentFaceRecognitionEvents(5);
    assert(events.size() == 2);
    assert(events.front().eventType == "face_recognized");
    assert(events.front().faceName == "Alice");
    assert(events.front().trackId == 502);
    assert(events.back().trackId == 501);
    assert(events.back().trackDurationMs == 33);
    assert(!events.back().trackActive);
    assert(events.back().trackFirstDecision == "matched");
    assert(events.back().trackLastFaceName == "Alice");

    ivp::FaceRecognitionEventQuery eventDeleteQuery;
    eventDeleteQuery.sessionId = sessionId;
    eventDeleteQuery.eventType = std::string("face_recognized");
    eventDeleteQuery.limit = 1;
    std::size_t deletedEventCount = 0;
    assert(storage.deleteRecognitionEvents(
        eventDeleteQuery,
        &deletedEventCount));
    assert(deletedEventCount == 2);
    assert(storage.recentFaceRecognitionEvents(5).empty());
    assert(storage.recentHistory(5).size() == 4);

    ivp::DetectionHistoryQuery historyDeleteQuery;
    historyDeleteQuery.sessionId = sessionId;
    historyDeleteQuery.limit = 1;
    std::size_t deletedHistoryCount = 0;
    assert(storage.deleteHistoryRecords(
        historyDeleteQuery,
        &deletedHistoryCount));
    assert(deletedHistoryCount == 4);
    assert(storage.recentHistory(5).empty());
    assert(storage.recentResults(5).empty());
    assert(storage.recentFaceRecognitionEvents(5).empty());
    assert(storage.allFaceFeatures().size() == 3);

    assert(storage.removeFaceIdentity(faces.front().faceId));
    assert(storage.recentFaceIdentities(5).empty());
    assert(storage.allFaceFeatures().empty());

    storage.close();
    std::remove(databasePath);
    return 0;
}
