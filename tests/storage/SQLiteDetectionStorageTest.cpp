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
    assert(storage.replaceFaceFeatures(faces.front().faceId, {feature}));
    const ivp::FaceFeatureTemplates storedFeatures = storage.allFaceFeatures();
    assert(storedFeatures.size() == 1);
    assert(storedFeatures.front().feature.featureFingerprint
           == "test-feature-fingerprint-v1");
    assert(storedFeatures.front().feature.values.size() == 3);
    assert(storedFeatures.front().feature.values[1] == 2.0F);

    ivp::DetectionResult recognized;
    recognized.sourceId = "camera_a";
    recognized.frameIndex = 44;
    recognized.ptsMs = 1466;
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
    assert(storage.saveFrameResults(sessionId, "camera_a", 44, 1466, {recognized}));

    ivp::DetectionResult repeatedRecognized = recognized;
    repeatedRecognized.frameIndex = 45;
    repeatedRecognized.ptsMs = 1499;
    repeatedRecognized.face.matchedAtMs = 123456990;
    assert(storage.saveFrameResults(
        sessionId,
        "camera_a",
        45,
        1499,
        {repeatedRecognized}));

    const ivp::DetectionHistoryRows autoHistory = storage.recentHistory(5);
    assert(autoHistory.size() == 3);
    const auto autoRow = std::find_if(
        autoHistory.begin(),
        autoHistory.end(),
        [](const ivp::DetectionHistoryRow& row) {
            return row.className == "face";
        });
    assert(autoRow != autoHistory.end());
    assert(autoRow->faceId.has_value());
    assert(autoRow->faceSimilarity == 0.83F);
    assert(autoRow->faceRecognizerName == "test_lbph");

    const ivp::FaceRecognitionEvents events =
        storage.recentFaceRecognitionEvents(5);
    assert(events.size() == 1);
    assert(events.front().eventType == "face_recognized");
    assert(events.front().faceName == "Alice");

    assert(storage.removeFaceIdentity(faces.front().faceId));
    assert(storage.recentFaceIdentities(5).empty());
    assert(storage.allFaceFeatures().empty());

    storage.close();
    std::remove(databasePath);
    return 0;
}
