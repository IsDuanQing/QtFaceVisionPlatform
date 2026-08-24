#ifndef IVP_RECOGNITION_FACERECOGNIZER_H
#define IVP_RECOGNITION_FACERECOGNIZER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/DetectionResult.h"
#include "common/FaceFeature.h"
#include "common/FaceRecognitionResult.h"
#include "common/VideoFrame.h"

namespace ivp
{

class IDetector;

struct FaceReferenceImage
{
    std::int64_t faceId = 0;
    std::string faceCode;
    std::string faceName;
    std::string imagePath;
};

class FaceRecognizer final
{
public:
    FaceRecognizer();
    ~FaceRecognizer();

    FaceRecognizer(const FaceRecognizer&) = delete;
    FaceRecognizer& operator=(const FaceRecognizer&) = delete;

    bool initialize(const FaceRecognitionConfig& config);
    FaceRecognitionConfig config() const;

    bool setGallery(FaceFeatureTemplates templates);
    std::size_t gallerySize() const;
    std::string modelName() const;
    std::string featureFingerprint() const;
    bool isAvailable() const;
    FaceRecognitionDiagnostics diagnostics() const;

    FaceFeatureTemplates extractReferenceFeatures(
        const FaceReferenceImage& reference,
        IDetector* faceDetector);
    FaceRecognitionResult recognize(
        const VideoFrame& frame,
        const DetectionResult& detection);

    std::string lastError() const;

private:
    struct Impl;

    std::vector<FaceFeatureVector> extractReferenceFeaturesFromImagePath(
        const std::string& imagePath,
        IDetector* faceDetector);
    std::vector<FaceFeatureVector> extractFeatureVariantsFromFrame(
        const VideoFrame& frame,
        const BoundingBox& box);
    FaceFeatureVector extractAlignedFeatureFromFace(
        const VideoFrame& frame,
        const BoundingBox& box,
        float paddingRatio);
    bool initializeFeatureModel();
    bool ensureFeatureModelLoaded();
    static std::string defaultFeatureModelPath();
    static std::string defaultFeatureModelDisplayName();
    void setLastError(const std::string& message);
    void clearLastError();

    FaceRecognitionConfig config_;
    FaceFeatureTemplates gallery_;
    std::string lastError_;
    std::string modelPath_;
    std::string modelName_;
    std::unique_ptr<Impl> impl_;
    bool initialized_;
    bool available_;
    bool galleryCompatible_;
    bool galleryNeedsRebuild_;
    std::string galleryFingerprint_;
};

} // namespace ivp

#endif // IVP_RECOGNITION_FACERECOGNIZER_H
