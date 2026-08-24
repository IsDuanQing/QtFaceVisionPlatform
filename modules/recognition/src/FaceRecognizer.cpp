#include "recognition/FaceRecognizer.h"

#include "inference/IDetector.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>
#endif

namespace
{

std::int64_t currentTimeMs()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string hashFingerprintInput(const std::string& input)
{
    // FNV-1a is sufficient here: the fingerprint detects configuration drift,
    // it is not used as a security digest.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : input)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    }

    std::ostringstream output;
    output << "face-feature-v1-"
           << std::hex
           << std::setw(16)
           << std::setfill('0')
           << hash;
    return output.str();
}

std::string lowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return extension;
}

bool isImageFile(const std::filesystem::path& path)
{
    const std::string extension = lowerExtension(path);
    return extension == ".png"
        || extension == ".jpg"
        || extension == ".jpeg"
        || extension == ".bmp"
        || extension == ".webp"
        || extension == ".tif"
        || extension == ".tiff";
}

std::vector<std::string> referenceImagePaths(const std::string& path)
{
    std::vector<std::string> paths;
    if (path.empty())
    {
        return paths;
    }

    std::error_code error;
    const std::filesystem::path inputPath = std::filesystem::u8path(path);
    if (std::filesystem::is_regular_file(inputPath, error))
    {
        if (isImageFile(inputPath))
        {
            paths.push_back(path);
        }
        return paths;
    }

    if (!std::filesystem::is_directory(inputPath, error))
    {
        return paths;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(inputPath, error))
    {
        if (error)
        {
            break;
        }
        if (entry.is_regular_file(error) && isImageFile(entry.path()))
        {
            paths.push_back(entry.path().u8string());
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

bool looksLikeFaceDetection(const ivp::DetectionResult& detection)
{
    if (detection.box.width <= 0.0F || detection.box.height <= 0.0F)
    {
        return false;
    }

    if (detection.className.empty())
    {
        return true;
    }

    std::string name = detection.className;
    std::transform(
        name.begin(),
        name.end(),
        name.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return name.find("face") != std::string::npos;
}

#if defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)

cv::Mat readImageWithUnicodePath(const std::string& path)
{
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input)
    {
        return {};
    }

    const std::vector<uchar> encoded{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (encoded.empty())
    {
        return {};
    }

    return cv::imdecode(encoded, cv::IMREAD_COLOR);
}

cv::Mat featureMat(const ivp::FaceFeatureVector& feature)
{
    if (feature.values.empty())
    {
        return {};
    }

    return cv::Mat(
        1,
        static_cast<int>(feature.values.size()),
        CV_32F,
        const_cast<float*>(feature.values.data()));
}

#endif

} // namespace

namespace ivp
{

struct FaceRecognizer::Impl
{
#if defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    cv::Ptr<cv::FaceRecognizerSF> featureModel;
#endif
};

FaceRecognizer::FaceRecognizer()
    : config_(),
      gallery_(),
      lastError_(),
      modelPath_(),
      modelName_(),
      impl_(std::make_unique<Impl>()),
      initialized_(false),
      available_(false),
      galleryCompatible_(false),
      galleryNeedsRebuild_(false),
      galleryFingerprint_()
{
}

FaceRecognizer::~FaceRecognizer() = default;

bool FaceRecognizer::initialize(const FaceRecognitionConfig& config)
{
    config_ = config;
    gallery_.clear();
    lastError_.clear();
    modelPath_ = config_.featureModelPath.empty()
        ? defaultFeatureModelPath()
        : config_.featureModelPath;
    modelName_ = defaultFeatureModelDisplayName();
    galleryCompatible_ = false;
    galleryNeedsRebuild_ = false;
    galleryFingerprint_.clear();
    initialized_ = true;
    available_ = false;
#if defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    impl_->featureModel.release();
#endif

    if (!config_.enabled)
    {
        return true;
    }

    if (config_.similarityThreshold < -1.0F
        || config_.similarityThreshold > 1.0F
        || config_.minSimilarityMargin < 0.0F
        || config_.minSimilarityMargin > 1.0F
        || config_.minFaceSizePixels <= 0
        || config_.normalizedWidth <= 0
        || config_.normalizedHeight <= 0
        || config_.facePaddingRatio < 0.0F
        || config_.facePaddingRatio > 1.0F)
    {
        lastError_ = "Face recognition parameters are invalid.";
        return false;
    }

    return initializeFeatureModel();
}

FaceRecognitionConfig FaceRecognizer::config() const
{
    return config_;
}

bool FaceRecognizer::setGallery(FaceFeatureTemplates templates)
{
    if (!initialized_)
    {
        lastError_ = "Face recognizer is not initialized.";
        return false;
    }

    gallery_.clear();
    galleryFingerprint_.clear();
    galleryCompatible_ = templates.empty();
    galleryNeedsRebuild_ = false;

    const std::string activeFingerprint = featureFingerprint();
    std::string firstFingerprint;
    bool mixedFingerprints = false;
    bool sawModelMismatch = false;
    bool sawFingerprintMismatch = false;
    FaceFeatureTemplates filtered;
    filtered.reserve(templates.size());
    for (FaceFeatureTemplate& item : templates)
    {
        if (item.faceId <= 0 || item.feature.values.empty())
        {
            continue;
        }

        if (item.feature.modelName != modelName())
        {
            sawModelMismatch = true;
            continue;
        }

        if (item.feature.featureFingerprint != activeFingerprint)
        {
            sawFingerprintMismatch = true;
            if (firstFingerprint.empty())
            {
                firstFingerprint = item.feature.featureFingerprint;
            }
            else if (firstFingerprint != item.feature.featureFingerprint)
            {
                mixedFingerprints = true;
            }
            continue;
        }

        if (firstFingerprint.empty())
        {
            firstFingerprint = item.feature.featureFingerprint;
        }
        else if (firstFingerprint != item.feature.featureFingerprint)
        {
            mixedFingerprints = true;
        }
        filtered.push_back(std::move(item));
    }

    gallery_ = std::move(filtered);
    if (!firstFingerprint.empty())
    {
        galleryFingerprint_ = mixedFingerprints
            ? "mixed"
            : firstFingerprint;
    }

    galleryNeedsRebuild_ = sawModelMismatch || sawFingerprintMismatch;
    galleryCompatible_ = !galleryNeedsRebuild_
        && (templates.empty() || !gallery_.empty());

    if (templates.empty())
    {
        if (isAvailable())
        {
            lastError_.clear();
        }
        return true;
    }

    if (galleryNeedsRebuild_ && isAvailable())
    {
        lastError_ = sawFingerprintMismatch
            ? "Loaded face features do not match the current feature configuration. "
              "Reference features must be rebuilt."
            : "Loaded face features do not match the current model. "
              "Reference features must be rebuilt.";
        return true;
    }

    if (gallery_.empty() && isAvailable())
    {
        lastError_ =
            "No usable face feature templates were loaded.";
        return true;
    }

    if (isAvailable())
    {
        lastError_.clear();
    }
    return true;
}

std::size_t FaceRecognizer::gallerySize() const
{
    return gallery_.size();
}

std::string FaceRecognizer::modelName() const
{
    return modelName_.empty() ? defaultFeatureModelDisplayName() : modelName_;
}

std::string FaceRecognizer::featureFingerprint() const
{
    const std::filesystem::path normalizedPath =
        std::filesystem::u8path(modelPath_).lexically_normal();
    std::error_code fileError;
    const std::uintmax_t modelFileSize =
        std::filesystem::is_regular_file(normalizedPath, fileError)
        ? std::filesystem::file_size(normalizedPath, fileError)
        : 0;
    fileError.clear();
    const auto modelWriteTime =
        std::filesystem::last_write_time(normalizedPath, fileError);
    std::ostringstream canonical;
    canonical << "model_name=" << modelName()
              << "|model_path=" << normalizedPath.u8string()
              << "|model_size=" << modelFileSize
              << "|model_time=" << modelWriteTime.time_since_epoch().count()
              << "|normalized_width=" << config_.normalizedWidth
              << "|normalized_height=" << config_.normalizedHeight
              << "|min_face_size=" << config_.minFaceSizePixels
              << "|padding=" << std::fixed << std::setprecision(4)
              << config_.facePaddingRatio
              << "|reference_detector=" << config_.referenceDetectorSignature;
    return hashFingerprintInput(canonical.str());
}

bool FaceRecognizer::isAvailable() const
{
    return initialized_ && config_.enabled && available_;
}

FaceRecognitionDiagnostics FaceRecognizer::diagnostics() const
{
    FaceRecognitionDiagnostics result;
    result.enabled = config_.enabled;
    result.available = isAvailable();
    result.galleryCompatible = galleryCompatible_;
    result.galleryNeedsRebuild = galleryNeedsRebuild_;
    result.gallerySize = gallery_.size();
    result.modelName = modelName();
    result.modelPath = modelPath_;
    result.featureFingerprint = featureFingerprint();
    result.galleryFingerprint = galleryFingerprint_;
    result.lastError = lastError_;
    return result;
}

FaceFeatureTemplates FaceRecognizer::extractReferenceFeatures(
    const FaceReferenceImage& reference,
    IDetector* faceDetector)
{
    FaceFeatureTemplates templates;
    if (reference.faceId <= 0 || reference.imagePath.empty())
    {
        lastError_ = "Face reference image path or identity is invalid.";
        return templates;
    }

    if (!isAvailable())
    {
        return templates;
    }
    if (faceDetector == nullptr)
    {
        lastError_ = "Face detector is unavailable for reference image cropping.";
        return templates;
    }

    const std::vector<std::string> imagePaths =
        referenceImagePaths(reference.imagePath);
    if (imagePaths.empty())
    {
        lastError_ = "No reference face images were found: " + reference.imagePath;
        return templates;
    }

    const std::int64_t timestamp = currentTimeMs();
    for (const std::string& imagePath : imagePaths)
    {
        const std::vector<FaceFeatureVector> features =
            extractReferenceFeaturesFromImagePath(imagePath, faceDetector);
        for (const FaceFeatureVector& feature : features)
        {
            if (feature.empty())
            {
                continue;
            }

            FaceFeatureTemplate item;
            item.faceId = reference.faceId;
            item.faceCode = reference.faceCode;
            item.faceName = reference.faceName;
            item.sampleImagePath = imagePath;
            item.feature = feature;
            item.createdAtMs = timestamp;
            templates.push_back(std::move(item));
        }
    }

    if (templates.empty() && lastError_.empty())
    {
        lastError_ =
            "Could not extract face features. Each reference image must contain a detectable face.";
    }
    else if (!templates.empty())
    {
        clearLastError();
    }
    return templates;
}

FaceRecognitionResult FaceRecognizer::recognize(
    const VideoFrame& frame,
    const DetectionResult& detection)
{
    FaceRecognitionResult result;
    result.threshold = config_.similarityThreshold;
    result.recognizerName = modelName();

    if (!config_.enabled)
    {
        result.decision = "disabled";
        return result;
    }
    if (!isAvailable())
    {
        result.decision = "unavailable";
        return result;
    }
    if (gallery_.empty())
    {
        result.decision = "no_templates";
        return result;
    }
    if (frame.empty())
    {
        result.decision = "empty_frame";
        return result;
    }
    if (!looksLikeFaceDetection(detection))
    {
        result.decision = "not_face_detection";
        return result;
    }
    if (detection.box.width < static_cast<float>(config_.minFaceSizePixels)
        || detection.box.height < static_cast<float>(config_.minFaceSizePixels))
    {
        result.decision = "face_too_small";
        return result;
    }

    const std::vector<FaceFeatureVector> queryFeatures =
        extractFeatureVariantsFromFrame(frame, detection.box);
    if (queryFeatures.empty())
    {
        result.decision = "no_query_feature";
        return result;
    }

#if !defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    return result;
#else
    struct IdentityCandidate
    {
        const FaceFeatureTemplate* sample = nullptr;
        float similarity = -1.0F;
    };

    std::map<std::int64_t, IdentityCandidate> bestByIdentity;
    try
    {
        for (const FaceFeatureVector& queryFeature : queryFeatures)
        {
            const cv::Mat query = featureMat(queryFeature);
            if (query.empty())
            {
                continue;
            }

            for (const FaceFeatureTemplate& candidate : gallery_)
            {
                if (candidate.feature.modelName != queryFeature.modelName
                    || candidate.feature.featureFingerprint
                        != queryFeature.featureFingerprint
                    || candidate.faceId <= 0)
                {
                    continue;
                }

                const cv::Mat reference = featureMat(candidate.feature);
                if (reference.empty())
                {
                    continue;
                }

                const float similarity = static_cast<float>(
                    impl_->featureModel->match(
                        query,
                        reference,
                        cv::FaceRecognizerSF::FR_COSINE));

                IdentityCandidate& identity = bestByIdentity[candidate.faceId];
                if (identity.sample == nullptr || similarity > identity.similarity)
                {
                    identity.sample = &candidate;
                    identity.similarity = similarity;
                }
            }
        }
    }
    catch (const cv::Exception& error)
    {
        setLastError(
            "OpenCV SFace matching failed: " + std::string(error.what()));
        return result;
    }

    std::vector<IdentityCandidate> ranked;
    ranked.reserve(bestByIdentity.size());
    for (const auto& entry : bestByIdentity)
    {
        if (entry.second.sample != nullptr)
        {
            ranked.push_back(entry.second);
        }
    }
    std::sort(
        ranked.begin(),
        ranked.end(),
        [](const IdentityCandidate& lhs, const IdentityCandidate& rhs) {
            return lhs.similarity > rhs.similarity;
        });

    if (ranked.empty())
    {
        result.decision = "no_candidates";
        return result;
    }

    const IdentityCandidate& best = ranked.front();
    const float bestSimilarity = std::clamp(best.similarity, -1.0F, 1.0F);
    const float secondSimilarity = ranked.size() > 1
        ? std::clamp(ranked[1].similarity, -1.0F, 1.0F)
        : -1.0F;
    const bool ambiguous = ranked.size() > 1
        && bestSimilarity >= config_.similarityThreshold
        && secondSimilarity >= config_.similarityThreshold
        && bestSimilarity - secondSimilarity
            < config_.minSimilarityMargin;

    result.similarity = std::clamp(bestSimilarity, 0.0F, 1.0F);
    result.distance = 1.0F - result.similarity;
    if (best.sample == nullptr)
    {
        result.decision = "no_candidates";
        return result;
    }
    if (bestSimilarity < config_.similarityThreshold)
    {
        result.decision = "low_similarity";
        return result;
    }
    if (ambiguous)
    {
        result.decision = "ambiguous";
        return result;
    }

    result.matched = true;
    result.faceId = best.sample->faceId;
    result.faceCode = best.sample->faceCode;
    result.faceName = best.sample->faceName;
    result.matchedAtMs = currentTimeMs();
    result.decision = "matched";
    return result;
#endif
}

std::string FaceRecognizer::lastError() const
{
    return lastError_;
}

std::vector<FaceFeatureVector> FaceRecognizer::extractReferenceFeaturesFromImagePath(
    const std::string& imagePath,
    IDetector* faceDetector)
{
    std::vector<FaceFeatureVector> features;
#if !defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    (void)imagePath;
    (void)faceDetector;
    setLastError("OpenCV SFace support is not enabled.");
    return features;
#else
    clearLastError();
    const cv::Mat image = readImageWithUnicodePath(imagePath);
    if (image.empty())
    {
        setLastError("Could not read face reference image: " + imagePath);
        return features;
    }

    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous())
    {
        rgb = rgb.clone();
    }

    VideoFrame frame;
    frame.metadata.width = rgb.cols;
    frame.metadata.height = rgb.rows;
    frame.metadata.sourceId = imagePath;
    frame.pixelFormat = PixelFormat::RGB24;
    frame.strideBytes = rgb.cols * 3;
    frame.data.assign(
        rgb.data,
        rgb.data + static_cast<std::size_t>(frame.strideBytes) * rgb.rows);

    const DetectionResults detections = faceDetector->detect(frame);
    const std::string detectorError = faceDetector->lastError();
    if (!detectorError.empty())
    {
        setLastError(
            "Could not detect a face in reference image "
            + imagePath + ": " + detectorError);
        return features;
    }

    const DetectionResult* selected = nullptr;
    float selectedArea = -1.0F;
    for (const DetectionResult& detection : detections)
    {
        if (!looksLikeFaceDetection(detection)
            || detection.box.width < static_cast<float>(config_.minFaceSizePixels)
            || detection.box.height < static_cast<float>(config_.minFaceSizePixels))
        {
            continue;
        }

        const float area = detection.box.width * detection.box.height;
        if (selected == nullptr
            || area > selectedArea
            || (area == selectedArea && detection.confidence > selected->confidence))
        {
            selected = &detection;
            selectedArea = area;
        }
    }

    if (selected == nullptr)
    {
        setLastError("No detectable face was found in reference image: " + imagePath);
        return features;
    }

    return extractFeatureVariantsFromFrame(frame, selected->box);
#endif
}

std::vector<FaceFeatureVector> FaceRecognizer::extractFeatureVariantsFromFrame(
    const VideoFrame& frame,
    const BoundingBox& box)
{
    std::vector<FaceFeatureVector> features;
    clearLastError();
#if !defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    (void)frame;
    (void)box;
    setLastError("OpenCV SFace support is not enabled.");
    return features;
#else
    if (!ensureFeatureModelLoaded())
    {
        return features;
    }
    if (frame.empty() || frame.pixelFormat != PixelFormat::RGB24)
    {
        setLastError("Face recognition requires RGB24 video frames.");
        return features;
    }

    const int frameWidth = frame.metadata.width;
    const int frameHeight = frame.metadata.height;
    if (frameWidth <= 0
        || frameHeight <= 0
        || frame.strideBytes < frameWidth * 3
        || frame.data.size()
            < static_cast<std::size_t>(frame.strideBytes) * frameHeight)
    {
        setLastError("Face frame buffer dimensions are invalid.");
        return features;
    }

    // A detector box is not a landmark-aligned face. Generate a small set of
    // crop variants so moderate box tightness and pose changes do not make a
    // valid identity look completely different to SFace.
    std::vector<float> paddingRatios;
    paddingRatios.push_back(0.0F);
    paddingRatios.push_back(config_.facePaddingRatio);
    if (config_.facePaddingRatio > 0.05F)
    {
        paddingRatios.push_back(std::min(
            0.35F,
            config_.facePaddingRatio * 1.5F));
    }

    for (std::size_t index = 0; index < paddingRatios.size(); ++index)
    {
        const float paddingRatio = paddingRatios[index];
        if (std::find(
                paddingRatios.begin(),
                paddingRatios.begin() + index,
                paddingRatio)
            != paddingRatios.begin() + index)
        {
            continue;
        }

        FaceFeatureVector feature =
            extractAlignedFeatureFromFace(frame, box, paddingRatio);
        if (!feature.empty())
        {
            features.push_back(std::move(feature));
        }
    }

    if (!features.empty())
    {
        clearLastError();
    }
    else if (lastError_.empty())
    {
        setLastError("Could not extract a face feature from the detected crop.");
    }
    return features;
#endif
}

FaceFeatureVector FaceRecognizer::extractAlignedFeatureFromFace(
    const VideoFrame& frame,
    const BoundingBox& box,
    float paddingRatio)
{
    FaceFeatureVector feature;
#if !defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    (void)frame;
    (void)box;
    return feature;
#else
    const int frameWidth = frame.metadata.width;
    const int frameHeight = frame.metadata.height;
    const int left = std::clamp(
        static_cast<int>(std::floor(box.x)),
        0,
        std::max(0, frameWidth - 1));
    const int top = std::clamp(
        static_cast<int>(std::floor(box.y)),
        0,
        std::max(0, frameHeight - 1));
    const int right = std::clamp(
        static_cast<int>(std::ceil(box.x + box.width)),
        0,
        frameWidth);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(box.y + box.height)),
        0,
        frameHeight);

    const int faceWidth = right - left;
    const int faceHeight = bottom - top;
    if (faceWidth < config_.minFaceSizePixels
        || faceHeight < config_.minFaceSizePixels)
    {
        return feature;
    }

    cv::Mat rgb(
        frameHeight,
        frameWidth,
        CV_8UC3,
        const_cast<std::uint8_t*>(frame.data.data()),
        static_cast<std::size_t>(frame.strideBytes));

    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    const float centerX = static_cast<float>(left) + faceWidth * 0.5F;
    const float centerY = static_cast<float>(top) + faceHeight * 0.5F;
    const float paddedSide = static_cast<float>(std::max(faceWidth, faceHeight))
        * (1.0F + 2.0F * paddingRatio);
    const int cropSide = std::max(
        1,
        static_cast<int>(std::round(paddedSide)));

    cv::Mat crop;
    cv::getRectSubPix(
        bgr,
        cv::Size(cropSide, cropSide),
        cv::Point2f(centerX, centerY),
        crop);
    if (crop.empty())
    {
        setLastError("Could not crop detected face.");
        return feature;
    }

    cv::Mat aligned;
    cv::resize(
        crop,
        aligned,
        cv::Size(config_.normalizedWidth, config_.normalizedHeight),
        0.0,
        0.0,
        cv::INTER_LINEAR);

    cv::Mat embedding;
    try
    {
        impl_->featureModel->feature(aligned, embedding);
    }
    catch (const cv::Exception& error)
    {
        setLastError(
            "OpenCV SFace feature extraction failed: "
            + std::string(error.what()));
        return feature;
    }

    if (embedding.empty())
    {
        setLastError("OpenCV SFace returned an empty face feature.");
        return feature;
    }
    if (embedding.type() != CV_32F)
    {
        embedding.convertTo(embedding, CV_32F);
    }
    embedding = embedding.reshape(1, 1);
    if (!embedding.isContinuous())
    {
        embedding = embedding.clone();
    }

    const float* begin = embedding.ptr<float>(0);
    feature.modelName = modelName();
    feature.featureFingerprint = featureFingerprint();
    feature.values.assign(
        begin,
        begin + static_cast<std::size_t>(embedding.total()));
    return feature;
#endif
}

bool FaceRecognizer::initializeFeatureModel()
{
#if !defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    lastError_ =
        "This build does not include OpenCV FaceRecognizerSF support. "
        "Enable IVP_ENABLE_OPENCV_FACE_RECOGNITION and link opencv_objdetect.";
    return false;
#else
    std::error_code error;
    const std::filesystem::path modelPath =
        std::filesystem::u8path(modelPath_);
    if (!std::filesystem::is_regular_file(modelPath, error))
    {
        lastError_ =
            "SFace feature model was not found: " + modelPath_;
        return false;
    }

    try
    {
        impl_->featureModel = cv::FaceRecognizerSF::create(modelPath_, "");
    }
    catch (const cv::Exception& exception)
    {
        lastError_ =
            "Could not load SFace feature model: "
            + std::string(exception.what());
        return false;
    }

    if (impl_->featureModel.empty())
    {
        lastError_ = "OpenCV returned an empty SFace feature model.";
        return false;
    }

    available_ = true;
    return true;
#endif
}

bool FaceRecognizer::ensureFeatureModelLoaded()
{
#if !defined(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    return false;
#else
    if (impl_ != nullptr && !impl_->featureModel.empty())
    {
        return true;
    }

    available_ = false;
    if (lastError_.empty())
    {
        lastError_ = "SFace feature model is not loaded.";
    }
    return false;
#endif
}

std::string FaceRecognizer::defaultFeatureModelPath()
{
    return "models/face-recognition-sface/face_recognition_sface_2021dec.onnx";
}

std::string FaceRecognizer::defaultFeatureModelDisplayName()
{
    return "opencv_sface_2021dec_cosine";
}

void FaceRecognizer::setLastError(const std::string& message)
{
    lastError_ = message;
}

void FaceRecognizer::clearLastError()
{
    lastError_.clear();
}

} // namespace ivp
