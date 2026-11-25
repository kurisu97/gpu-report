#include "gpu/Codecs.hpp"

namespace gpu_report {
namespace codecs {

const std::vector<std::string>& mandatoryCodecs()
{
    static const std::vector<std::string> values = {
        kH264,
        kHEVC,
        kVP8,
        kVP9,
        kAV1,
        kMPEG2,
        kVC1,
        kMJPEG,
    };
    return values;
}

std::string ffmpegCodecName(const std::string& codec)
{
    if (codec == kH264) {
        return "h264";
    }
    if (codec == kHEVC) {
        return "hevc";
    }
    if (codec == kVP8) {
        return "vp8";
    }
    if (codec == kVP9) {
        return "vp9";
    }
    if (codec == kAV1) {
        return "av1";
    }
    if (codec == kMPEG2) {
        return "mpeg2";
    }
    if (codec == kVC1) {
        return "vc1";
    }
    if (codec == kMJPEG) {
        return "mjpeg";
    }
    return {};
}

std::string codecFromFfmpegName(const std::string& ffmpegName)
{
    if (ffmpegName == "h264" || ffmpegName == "h264_qsv" || ffmpegName == "h264_vaapi") {
        return kH264;
    }
    if (ffmpegName == "hevc" || ffmpegName == "hevc_qsv") {
        return kHEVC;
    }
    if (ffmpegName == "vp8") {
        return kVP8;
    }
    if (ffmpegName == "vp9") {
        return kVP9;
    }
    if (ffmpegName == "av1") {
        return kAV1;
    }
    if (ffmpegName == "mpeg2video" || ffmpegName == "mpeg2") {
        return kMPEG2;
    }
    if (ffmpegName == "vc1") {
        return kVC1;
    }
    if (ffmpegName == "mjpeg") {
        return kMJPEG;
    }
    return {};
}

std::string commentKey(const std::string& codec)
{
    if (codec == kH264) {
        return "codec.comment.h264";
    }
    if (codec == kHEVC) {
        return "codec.comment.hevc";
    }
    if (codec == kVP8) {
        return "codec.comment.vp8";
    }
    if (codec == kVP9) {
        return "codec.comment.vp9";
    }
    if (codec == kAV1) {
        return "codec.comment.av1";
    }
    if (codec == kMPEG2) {
        return "codec.comment.mpeg2";
    }
    if (codec == kVC1) {
        return "codec.comment.vc1";
    }
    if (codec == kMJPEG) {
        return "codec.comment.mjpeg";
    }
    return {};
}

} // namespace codecs
} // namespace gpu_report
