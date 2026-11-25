#include "diagnostics/Diagnostics.hpp"

#include "gpu/Codecs.hpp"

#include <algorithm>

namespace gpu_report {

namespace {

template <typename T>
void appendUnique(std::vector<T>& target, const std::vector<T>& values)
{
    for (const T& value : values) {
        if (std::find(target.begin(), target.end(), value) == target.end()) {
            target.push_back(value);
        }
    }
}

void applyStatus(CapabilityStatus& status, TriState value, const std::string& source)
{
    if (value == TriState::Unknown) {
        return;
    }
    if (value > status.value) {
        status.value = value;
        status.sources.clear();
    }
    if (value == status.value && !source.empty()
        && std::find(status.sources.begin(), status.sources.end(), source) == status.sources.end()) {
        status.sources.push_back(source);
    }
}

} // namespace

void ensureMandatoryCodecs(std::vector<CodecCapability>& capabilities)
{
    for (const std::string& codec : codecs::mandatoryCodecs()) {
        bool found = false;
        for (const CodecCapability& capability : capabilities) {
            if (capability.codec == codec) {
                found = true;
                break;
            }
        }
        if (!found) {
            CodecCapability capability;
            capability.codec = codec;
            capabilities.push_back(capability);
        }
    }
}

void mergeEvidence(std::vector<CodecCapability>& capabilities,
                   const std::vector<CodecEvidence>& evidence)
{
    for (const CodecEvidence& item : evidence) {
        if (item.codec.empty()) {
            continue;
        }

        CodecCapability* capability = nullptr;
        for (CodecCapability& candidate : capabilities) {
            if (candidate.codec == item.codec) {
                capability = &candidate;
                break;
            }
        }
        if (capability == nullptr) {
            CodecCapability created;
            created.codec = item.codec;
            capabilities.push_back(created);
            capability = &capabilities.back();
        }

        applyStatus(capability->decode, item.decode, item.decodeSource);
        applyStatus(capability->encode, item.encode, item.encodeSource);

        if (item.decode == TriState::Yes || item.encode == TriState::Yes) {
            appendUnique(capability->profiles, item.profiles);
            appendUnique(capability->bitDepths, item.bitDepths);
            appendUnique(capability->pixelFormats, item.pixelFormats);
            std::sort(capability->bitDepths.begin(), capability->bitDepths.end());
        }
    }
}

} // namespace gpu_report
