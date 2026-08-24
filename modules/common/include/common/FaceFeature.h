#ifndef IVP_COMMON_FACEFEATURE_H
#define IVP_COMMON_FACEFEATURE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ivp
{

struct FaceRecognitionConfig
{
    bool enabled = true;
    std::string featureModelPath;
    float similarityThreshold = 0.363F;
    float minSimilarityMargin = 0.05F;
    int minFaceSizePixels = 24;
    int normalizedWidth = 112;
    int normalizedHeight = 112;
    float facePaddingRatio = 0.20F;
    std::string referenceDetectorSignature;
};

struct FaceFeatureVector
{
    std::string modelName;
    std::string featureFingerprint;
    std::vector<float> values;

    bool empty() const
    {
        return modelName.empty() || values.empty();
    }
};

struct FaceFeatureTemplate
{
    std::int64_t featureId = 0;
    std::int64_t faceId = 0;
    std::string faceCode;
    std::string faceName;
    std::string sampleImagePath;
    FaceFeatureVector feature;
    std::int64_t createdAtMs = 0;
};

using FaceFeatureTemplates = std::vector<FaceFeatureTemplate>;

struct FaceRecognitionDiagnostics
{
    bool enabled = false;
    bool available = false;
    bool galleryCompatible = false;
    bool galleryNeedsRebuild = false;
    std::size_t gallerySize = 0;
    std::string modelName;
    std::string modelPath;
    std::string featureFingerprint;
    std::string galleryFingerprint;
    std::string lastError;
};

} // namespace ivp

#endif // IVP_COMMON_FACEFEATURE_H
