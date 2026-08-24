#ifndef IVP_COMMON_FACERECOGNITIONRESULT_H
#define IVP_COMMON_FACERECOGNITIONRESULT_H

#include <cstdint>
#include <optional>
#include <string>

namespace ivp
{

struct FaceRecognitionResult
{
    bool matched = false;
    std::optional<std::int64_t> faceId;
    std::string faceCode;
    std::string faceName;
    float distance = 0.0F;
    float similarity = 0.0F;
    float threshold = 0.0F;
    std::int64_t matchedAtMs = 0;
    std::string recognizerName;
    std::string decision;
};

} // namespace ivp

#endif // IVP_COMMON_FACERECOGNITIONRESULT_H
