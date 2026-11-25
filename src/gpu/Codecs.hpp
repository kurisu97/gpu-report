#pragma once

#include <string>
#include <vector>

namespace gpu_report {

namespace codecs {

inline constexpr const char* kH264 = "H.264/AVC";
inline constexpr const char* kHEVC = "HEVC/H.265";
inline constexpr const char* kVP8 = "VP8";
inline constexpr const char* kVP9 = "VP9";
inline constexpr const char* kAV1 = "AV1";
inline constexpr const char* kMPEG2 = "MPEG-2";
inline constexpr const char* kVC1 = "VC-1";
inline constexpr const char* kMJPEG = "JPEG/MJPEG";

const std::vector<std::string>& mandatoryCodecs();

// FFmpeg codec token used in encoder/decoder names (h264, hevc, ...).
std::string ffmpegCodecName(const std::string& codec);

// Maps an FFmpeg codec token back to the canonical report name.
std::string codecFromFfmpegName(const std::string& ffmpegName);

// Stable key used for localized codec comments.
std::string commentKey(const std::string& codec);

} // namespace codecs

} // namespace gpu_report
