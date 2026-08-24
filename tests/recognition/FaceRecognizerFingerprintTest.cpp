#include "recognition/FaceRecognizer.h"

#include <cassert>

int main()
{
    ivp::FaceRecognitionConfig baseConfig;
    baseConfig.enabled = false;
    baseConfig.featureModelPath = "models/face-recognition-sface/test.onnx";

    ivp::FaceRecognizer baseRecognizer;
    assert(baseRecognizer.initialize(baseConfig));
    const std::string baseFingerprint = baseRecognizer.featureFingerprint();
    assert(!baseFingerprint.empty());

    ivp::FaceRecognitionConfig thresholdConfig = baseConfig;
    thresholdConfig.similarityThreshold = 0.80F;
    ivp::FaceRecognizer thresholdRecognizer;
    assert(thresholdRecognizer.initialize(thresholdConfig));
    assert(thresholdRecognizer.featureFingerprint() == baseFingerprint);

    ivp::FaceRecognitionConfig paddingConfig = baseConfig;
    paddingConfig.facePaddingRatio = 0.35F;
    ivp::FaceRecognizer paddingRecognizer;
    assert(paddingRecognizer.initialize(paddingConfig));
    assert(paddingRecognizer.featureFingerprint() != baseFingerprint);

    ivp::FaceRecognitionConfig sizeConfig = baseConfig;
    sizeConfig.normalizedWidth = 224;
    ivp::FaceRecognizer sizeRecognizer;
    assert(sizeRecognizer.initialize(sizeConfig));
    assert(sizeRecognizer.featureFingerprint() != baseFingerprint);

    ivp::FaceRecognitionConfig modelConfig = baseConfig;
    modelConfig.featureModelPath = "models/face-recognition-sface/other.onnx";
    ivp::FaceRecognizer modelRecognizer;
    assert(modelRecognizer.initialize(modelConfig));
    assert(modelRecognizer.featureFingerprint() != baseFingerprint);

    ivp::FaceRecognitionConfig detectorConfig = baseConfig;
    detectorConfig.referenceDetectorSignature = "detector-v2";
    ivp::FaceRecognizer detectorRecognizer;
    assert(detectorRecognizer.initialize(detectorConfig));
    assert(detectorRecognizer.featureFingerprint() != baseFingerprint);

    ivp::FaceFeatureTemplate currentTemplate;
    currentTemplate.faceId = 1;
    currentTemplate.faceCode = "person_001";
    currentTemplate.faceName = "Alice";
    currentTemplate.feature.modelName = baseRecognizer.modelName();
    currentTemplate.feature.featureFingerprint = baseFingerprint;
    currentTemplate.feature.values = {0.1F, 0.2F, 0.3F};

    assert(thresholdRecognizer.setGallery({currentTemplate}));
    const ivp::FaceRecognitionDiagnostics thresholdDiagnostics =
        thresholdRecognizer.diagnostics();
    assert(thresholdDiagnostics.galleryCompatible);
    assert(!thresholdDiagnostics.galleryNeedsRebuild);

    assert(baseRecognizer.setGallery({currentTemplate}));
    const ivp::FaceRecognitionDiagnostics compatibleDiagnostics =
        baseRecognizer.diagnostics();
    assert(compatibleDiagnostics.gallerySize == 1);
    assert(compatibleDiagnostics.galleryCompatible);
    assert(!compatibleDiagnostics.galleryNeedsRebuild);

    ivp::FaceFeatureTemplate staleTemplate = currentTemplate;
    staleTemplate.feature.featureFingerprint = "face-feature-v1-stale";
    ivp::FaceRecognizer staleRecognizer;
    assert(staleRecognizer.initialize(baseConfig));
    assert(staleRecognizer.setGallery({staleTemplate}));
    const ivp::FaceRecognitionDiagnostics staleDiagnostics =
        staleRecognizer.diagnostics();
    assert(staleDiagnostics.gallerySize == 0);
    assert(!staleDiagnostics.galleryCompatible);
    assert(staleDiagnostics.galleryNeedsRebuild);

    return 0;
}
