#include "ExperimentRecord.h"
#include "dsp/ModuleContract.h"
#include "dsp/BitCrusher.h"
#include <cmath>
#include <limits>

#ifndef DISDORKTION_BUILD_ID
#define DISDORKTION_BUILD_ID "unknown"
#endif

namespace disdorktion::harness
{
namespace
{
constexpr const char* names[] { "impulse", "sine", "two-tone", "sweep", "noise", "file", "midi" };
bool fail(juce::String& error, const juce::String& message) { error = message; return false; }
bool number(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }
bool integer(const juce::var& v, double low, double high)
{
    if (!number(v)) return false;
    const double n = static_cast<double>(v);
    return std::isfinite(n) && n >= low && n <= high && std::floor(n) == n;
}
}

ExperimentRecord::ExperimentRecord() : buildIdentity(DISDORKTION_BUILD_ID) {}

bool ExperimentRecord::validate(juce::String& error) const
{
    error.clear();
    if (!((version == 1 && targetId == "reference-gain") || (version == 2 && targetId == "gain")
        || ((version == 3 || version == 4) && targetId == "bit-crusher")
        || (version == 5 && targetId == "tanh-saturation") || (version == 6 && targetId == "wave-folder")
        || (version == 7 && (targetId == "high-pass" || targetId == "low-pass"))
        || (version == 8 && targetId == "input-section")))
        return fail(error, "Unsupported experiment version or target");
    if (!isValidProcessSpec({ sampleRate, blockSize, channels })) return fail(error, "Invalid process specification");
    if (version == 7 && (!isValidCutoffSettings({cutoffHz,bypass,response}) || driveDb != 0.f || outputDb != 0.f))
        return fail(error,"Invalid or foreign filter settings");
    if (version != 7 && (cutoffHz != 1000.f || response != FilterResponse::butterworth))
        return fail(error,"Foreign filter settings");
    if (version == 1 && (!std::isfinite(gain) || gain < 0 || gain > 4)) return fail(error, "Gain must be in [0, 4]");
    if ((version == 2 || version == 8) && (!std::isfinite(gainDb) || gainDb < -60 || gainDb > 24)) return fail(error, "Gain dB must be in [-60, 24]");
    if ((version == 3 || version == 4 || version == 8) && (!isValidBitDepth(bitDepth) || !std::isfinite(captureRateHz)
        || captureRateHz < 100.0f || captureRateHz > 384000.0f)) return fail(error, "Invalid bit crusher settings");
    if ((version == 4 || version == 8) && (!std::isfinite(mix) || mix < 0.0f || mix > 1.0f
        || !std::isfinite(crush) || crush < 0.0f || crush > 1.0f
        || !std::isfinite(shape) || shape < -1.0f || shape > 1.0f
        || !std::isfinite(jitter) || jitter < 0.0f || jitter > 1.0f))
        return fail(error, "Invalid expressive crusher settings");
    if (version != 4 && version != 8 && (mix != 1.0f || crush != 0.0f || shape != 0.0f
        || jitter != 0.0f || jitterSeed != 1))
        return fail(error, "Legacy experiment cannot contain expressive crusher settings");
    if (version < 3 && (bitDepth != 64 || captureRateHz != 384000.0f))
        return fail(error, "Gain experiment cannot contain crusher settings");
    if (version >= 5 && (!std::isfinite(driveDb) || driveDb < -24 || driveDb > 36
        || !std::isfinite(outputDb) || outputDb < -60 || outputDb > 12))
        return fail(error, "Invalid tanh saturation settings");
    if ((version < 5 || version == 8) && (driveDb != 0.0f || outputDb != 0.0f))
        return fail(error, "Foreign tanh saturation settings");
    if (version == 6 && (!std::isfinite(threshold) || threshold < 0.05f || threshold > 1.0f))
        return fail(error, "Invalid wave folder threshold");
    if (version != 6 && threshold != 1.0f) return fail(error, "Foreign wave folder threshold");
    if (version >= 5 && version != 8 && (gain != 1.0f || gainDb != 0.0f || mute
        || bitDepth != 64 || captureRateHz != 384000.0f))
        return fail(error, "Foreign gain or crusher settings");
    if (version == 8 && gain != 1.f) return fail(error, "Foreign reference gain");
    if (version != 8 && (gainBypass || crusherBypass)) return fail(error, "Foreign section bypass settings");
    if (!std::isfinite(monitorDb) || monitorDb < -60 || monitorDb > 0) return fail(error, "Monitor dB must be in [-60, 0]");
    if (renderDurationSamples == 0 || renderDurationSamples > maximumSamples
        || source.durationSamples == 0 || source.durationSamples > maximumSamples)
        return fail(error, "Sample counts must be in [1, 536870000]");
    const auto kind = static_cast<int>(source.kind);
    if (kind < 0 || kind >= 7) return fail(error, "Unsupported source kind");
    // Validate all source scalars, including fields unused by the selected kind.
    if (!std::isfinite(source.frequency) || !std::isfinite(source.frequency2)
        || source.frequency <= 0 || source.frequency2 <= 0
        || source.frequency >= sampleRate * 0.5 || source.frequency2 >= sampleRate * 0.5
        || !std::isfinite(source.amplitude) || source.amplitude < 0 || source.amplitude > 1
        || !std::isfinite(source.phase)) return fail(error, "Invalid source settings");
    if (source.kind == SourceKind::sweep && (source.frequency2 <= source.frequency || source.durationSamples < 2))
        return fail(error, "Sweep requires increasing endpoints and at least two samples");
    if (sourceConversion != "linear;mono-duplicate;stereo-average") return fail(error, "Unsupported source conversion");
    if (!std::isfinite(originalSampleRate) || originalSampleRate < 0 || originalSampleRate > 384000
        || originalChannels > 2) return fail(error, "Invalid original file format");
    if (source.kind == SourceKind::file)
    {
        if (!juce::File::isAbsolutePath(filePath) || fileHash.length() != 64 || !fileHash.containsOnly("0123456789abcdef"))
            return fail(error, "File source requires a path and lowercase SHA-256 hash");
        if (originalSampleRate <= 0 || originalChannels == 0) return fail(error, "File source requires original format");
    }
    return true;
}

juce::var ExperimentRecord::toJson() const
{
    auto* object = new juce::DynamicObject;
    juce::var result(object);
    auto put = [object](const char* key, const juce::var& value) { object->setProperty(key, value); };
    put("version", version); put("targetId", targetId);
    if (version == 1) put("gain", gain);
    else if (version == 2) { put("gainDb", gainDb); put("mute", mute); }
    else if (version == 8)
    {
        put("gainDb", gainDb); put("mute", mute);
        put("gainBypass", gainBypass); put("crusherBypass", crusherBypass);
        put("bitDepth", static_cast<int>(bitDepth)); put("captureRateHz", captureRateHz);
    }
    else if (version == 7)
    {
        put("cutoffHz",cutoffHz);
        const auto index=static_cast<unsigned>(response);
        put("response",index<filterResponseIds.size() ? juce::String(filterResponseIds[index].data()) : juce::String("invalid"));
    }
    else if (version >= 5) { put("driveDb", driveDb); put("outputDb", outputDb); }
    else { put("bitDepth", static_cast<int>(bitDepth)); put("captureRateHz", captureRateHz); }
    if (version == 6) put("threshold", threshold);
    if (version == 4 || version == 8)
    {
        put("mix", mix); put("crush", crush); put("shape", shape); put("jitter", jitter);
        put("jitterSeed", static_cast<juce::int64>(jitterSeed));
    }
    put("bypass", bypass);
    put("sampleRate", sampleRate); put("channels", static_cast<int>(channels)); put("blockSize", static_cast<int>(blockSize));
    put("renderDurationSamples", static_cast<juce::int64>(renderDurationSamples));
    put("filePath", filePath); put("fileHash", fileHash); put("sourceConversion", sourceConversion);
    put("originalSampleRate", originalSampleRate); put("originalChannels", static_cast<int>(originalChannels));
    put("monitorDb", monitorDb); put("muted", muted); put("midiDevice", midiDevice);
    put("buildIdentity", buildIdentity); put("observations", observations);
    auto* settings = new juce::DynamicObject;
    const int kind = static_cast<int>(source.kind);
    settings->setProperty("kind", kind >= 0 && kind < 7 ? names[kind] : "invalid");
    settings->setProperty("frequency", source.frequency); settings->setProperty("frequency2", source.frequency2);
    settings->setProperty("amplitude", source.amplitude); settings->setProperty("phase", source.phase);
    settings->setProperty("durationSamples", static_cast<juce::int64>(source.durationSamples));
    settings->setProperty("seed", static_cast<juce::int64>(source.seed));
    put("source", juce::var(settings));
    return result;
}

bool ExperimentRecord::fromJson(const juce::var& value, ExperimentRecord& destination, juce::String& error)
{
    error.clear();
    auto* o = value.getDynamicObject();
    if (o == nullptr) return fail(error, "Experiment must be a JSON object");
    auto get = [o](const char* key) { return o->getProperty(key); };
    for (auto key : {"targetId", "filePath", "fileHash", "sourceConversion", "midiDevice", "buildIdentity", "observations"})
        if (!get(key).isString()) return fail(error, juce::String("Missing or invalid string: ") + key);
    if (!integer(get("version"), 1, 8)) return fail(error, "Unsupported or missing version");
    const auto version = static_cast<int>(get("version"));
    if ((version == 1 && get("targetId").toString() != "reference-gain")
        || (version == 2 && get("targetId").toString() != "gain")
        || ((version == 3 || version == 4) && get("targetId").toString() != "bit-crusher")
        || (version == 5 && get("targetId").toString() != "tanh-saturation")
        || (version == 6 && get("targetId").toString() != "wave-folder")
        || (version == 7 && get("targetId").toString() != "high-pass" && get("targetId").toString() != "low-pass")
        || (version == 8 && get("targetId").toString() != "input-section"))
        return fail(error, "Unsupported target for version");
    if ((version != 1 && o->hasProperty("gain"))
        || (version != 2 && version != 8 && (o->hasProperty("gainDb") || o->hasProperty("mute")))
        || ((version < 3 || (version >= 5 && version != 8)) && (o->hasProperty("bitDepth") || o->hasProperty("captureRateHz")))
        || (version != 4 && version != 8 && (o->hasProperty("mix") || o->hasProperty("crush")
            || o->hasProperty("shape") || o->hasProperty("jitter") || o->hasProperty("jitterSeed"))))
        return fail(error, "Mixed target schemas");
    if (version != 8 && (o->hasProperty("gainBypass") || o->hasProperty("crusherBypass")))
        return fail(error, "Mixed target schemas");
    if ((version < 5 || version == 7 || version == 8) && (o->hasProperty("driveDb") || o->hasProperty("outputDb")))
        return fail(error, "Mixed target schemas");
    if (version != 6 && o->hasProperty("threshold")) return fail(error, "Mixed target schemas");
    if (version == 6 && (!number(get("threshold")) || !std::isfinite(static_cast<double>(get("threshold")))
        || static_cast<double>(get("threshold")) < 0.05 || static_cast<double>(get("threshold")) > 1.0))
        return fail(error, "Missing or invalid wave folder threshold");
    FilterResponse filterMode=FilterResponse::butterworth;
    if (version != 7 && (o->hasProperty("cutoffHz") || o->hasProperty("response"))) return fail(error,"Mixed target schemas");
    if (version == 7)
    {
        if (!number(get("cutoffHz")) || !std::isfinite(static_cast<double>(get("cutoffHz")))
            || static_cast<double>(get("cutoffHz")) < 20. || static_cast<double>(get("cutoffHz")) > 20000.
            || !get("response").isString()) return fail(error,"Missing or invalid cutoff filter settings");
        bool found=false;
        for (unsigned i=0;i<filterResponseIds.size();++i)
            if (get("response").toString()==juce::String(filterResponseIds[i].data())) {filterMode=static_cast<FilterResponse>(i);found=true;}
        if (!found) return fail(error,"Unsupported filter response");
    }
    if (version == 5 || version == 6)
    {
        for (auto key : {"driveDb", "outputDb"})
            if (!number(get(key)) || !std::isfinite(static_cast<double>(get(key))))
                return fail(error, "Missing or invalid tanh saturation number");
        if (static_cast<double>(get("driveDb")) < -24 || static_cast<double>(get("driveDb")) > 36
            || static_cast<double>(get("outputDb")) < -60 || static_cast<double>(get("outputDb")) > 12)
            return fail(error, "Tanh saturation setting out of range");
    }
    double gainValue = 0.0;
    if (version < 3 || version == 8)
    {
        const auto* gainKey = version == 1 ? "gain" : "gainDb";
        if (!number(get(gainKey)) || !std::isfinite(static_cast<double>(get(gainKey))))
            return fail(error, "Missing or invalid gain number");
        gainValue = static_cast<double>(get(gainKey));
        if ((version == 1 && (gainValue < 0 || gainValue > 4))
            || ((version == 2 || version == 8) && (gainValue < -60 || gainValue > 24))) return fail(error, "Gain out of range");
    }
    if (((version >= 3 && version < 5) || version == 8) && (!integer(get("bitDepth"), 1, 64)
        || !isValidBitDepth(static_cast<unsigned>(static_cast<int>(get("bitDepth"))))
        || !number(get("captureRateHz")) || !std::isfinite(static_cast<double>(get("captureRateHz")))
        || static_cast<double>(get("captureRateHz")) < 100.0
        || static_cast<double>(get("captureRateHz")) > 384000.0))
        return fail(error, "Missing or invalid bit crusher settings");
    if (version == 4 || version == 8)
    {
        for (auto key : {"mix", "crush", "shape", "jitter"})
            if (!number(get(key)) || !std::isfinite(static_cast<double>(get(key))))
                return fail(error, juce::String("Missing or invalid crusher number: ") + key);
        if (static_cast<double>(get("mix")) < 0 || static_cast<double>(get("mix")) > 1
            || static_cast<double>(get("crush")) < 0 || static_cast<double>(get("crush")) > 1
            || static_cast<double>(get("shape")) < -1 || static_cast<double>(get("shape")) > 1
            || static_cast<double>(get("jitter")) < 0 || static_cast<double>(get("jitter")) > 1
            || !integer(get("jitterSeed"), 0, std::numeric_limits<std::uint32_t>::max()))
            return fail(error, "Expressive crusher setting out of range");
    }
    if ((version == 2 || version == 8) && !get("mute").isBool()) return fail(error, "Missing or invalid module mute");
    if (version == 8 && (!get("gainBypass").isBool() || !get("crusherBypass").isBool()))
        return fail(error, "Missing or invalid section bypass");
    for (auto key : {"sampleRate", "monitorDb", "originalSampleRate"})
        if (!number(get(key)) || !std::isfinite(static_cast<double>(get(key))))
            return fail(error, juce::String("Missing or invalid number: ") + key);
    if (static_cast<double>(get("monitorDb")) < -60 || static_cast<double>(get("monitorDb")) > 0)
        return fail(error, "Gain or monitoring value out of range");
    if (!get("bypass").isBool() || !get("muted").isBool()) return fail(error, "Missing or invalid boolean");
    if (!integer(get("channels"), 1, 2) || !integer(get("blockSize"), 1, 65536)
        || !integer(get("originalChannels"), 0, 2)
        || !integer(get("renderDurationSamples"), 1, maximumSamples)) return fail(error, "Invalid integer field");
    auto* s = get("source").getDynamicObject();
    if (s == nullptr) return fail(error, "Missing source object");
    auto sg = [s](const char* key) { return s->getProperty(key); };
    if (!sg("kind").isString()) return fail(error, "Missing source kind");
    int kind = -1;
    for (int i = 0; i < 7; ++i) if (sg("kind").toString() == names[i]) kind = i;
    if (kind < 0) return fail(error, "Unsupported source kind");
    for (auto key : {"frequency", "frequency2", "amplitude", "phase"})
        if (!number(sg(key)) || !std::isfinite(static_cast<double>(sg(key)))) return fail(error, "Invalid source number");
    if (!integer(sg("durationSamples"), 1, maximumSamples)
        || !integer(sg("seed"), 0, std::numeric_limits<std::uint32_t>::max())) return fail(error, "Invalid source count or seed");
    ExperimentRecord candidate;
    candidate.version = version; candidate.targetId = get("targetId").toString();
    if (version == 1) candidate.gain = static_cast<float>(gainValue);
    else if (version == 2) { candidate.gainDb = static_cast<float>(gainValue); candidate.mute = static_cast<bool>(get("mute")); }
    else if (version == 8)
    {
        candidate.gainDb = static_cast<float>(gainValue); candidate.mute = static_cast<bool>(get("mute"));
        candidate.gainBypass = static_cast<bool>(get("gainBypass")); candidate.crusherBypass = static_cast<bool>(get("crusherBypass"));
        candidate.bitDepth = static_cast<unsigned>(static_cast<int>(get("bitDepth")));
        candidate.captureRateHz = static_cast<float>(get("captureRateHz"));
        candidate.mix = static_cast<float>(get("mix")); candidate.crush = static_cast<float>(get("crush"));
        candidate.shape = static_cast<float>(get("shape")); candidate.jitter = static_cast<float>(get("jitter"));
        candidate.jitterSeed = static_cast<std::uint32_t>(static_cast<juce::int64>(get("jitterSeed")));
    }
    else if (version == 7) { candidate.cutoffHz=static_cast<float>(get("cutoffHz"));candidate.response=filterMode; }
    else if (version >= 5) { candidate.driveDb = static_cast<float>(get("driveDb")); candidate.outputDb = static_cast<float>(get("outputDb"));
                            if (version == 6) candidate.threshold = static_cast<float>(get("threshold")); }
    else { candidate.bitDepth = static_cast<unsigned>(static_cast<int>(get("bitDepth")));
           candidate.captureRateHz = static_cast<float>(get("captureRateHz"));
           if (version == 4)
           {
               candidate.mix = static_cast<float>(get("mix"));
               candidate.crush = static_cast<float>(get("crush"));
               candidate.shape = static_cast<float>(get("shape"));
               candidate.jitter = static_cast<float>(get("jitter"));
               candidate.jitterSeed = static_cast<std::uint32_t>(static_cast<juce::int64>(get("jitterSeed")));
           }
    }
    candidate.bypass = static_cast<bool>(get("bypass"));
    candidate.sampleRate = static_cast<double>(get("sampleRate"));
    candidate.channels = static_cast<unsigned>(static_cast<int>(get("channels")));
    candidate.blockSize = static_cast<unsigned>(static_cast<int>(get("blockSize")));
    candidate.renderDurationSamples = static_cast<std::uint64_t>(static_cast<juce::int64>(get("renderDurationSamples")));
    candidate.filePath = get("filePath").toString(); candidate.fileHash = get("fileHash").toString();
    candidate.sourceConversion = get("sourceConversion").toString();
    candidate.originalSampleRate = static_cast<double>(get("originalSampleRate"));
    candidate.originalChannels = static_cast<unsigned>(static_cast<int>(get("originalChannels")));
    candidate.monitorDb = static_cast<float>(get("monitorDb")); candidate.muted = static_cast<bool>(get("muted"));
    candidate.midiDevice = get("midiDevice").toString(); candidate.buildIdentity = get("buildIdentity").toString();
    candidate.observations = get("observations").toString(); candidate.source.kind = static_cast<SourceKind>(kind);
    candidate.source.frequency = static_cast<double>(sg("frequency")); candidate.source.frequency2 = static_cast<double>(sg("frequency2"));
    candidate.source.amplitude = static_cast<double>(sg("amplitude")); candidate.source.phase = static_cast<double>(sg("phase"));
    candidate.source.durationSamples = static_cast<std::uint64_t>(static_cast<juce::int64>(sg("durationSamples")));
    candidate.source.seed = static_cast<std::uint32_t>(static_cast<juce::int64>(sg("seed")));
    if (!candidate.validate(error)) return false;
    destination = std::move(candidate);
    return true;
}

bool ExperimentRecord::save(const juce::File& file, juce::String& error) const
{
    if (!validate(error)) return false;
    if (!file.replaceWithText(juce::JSON::toString(toJson()))) return fail(error, "Cannot save experiment");
    return true;
}
bool ExperimentRecord::load(const juce::File& file, ExperimentRecord& result, juce::String& error)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024) return fail(error, "Missing or oversized experiment file");
    juce::var parsed;
    const auto parse = juce::JSON::parse(file.loadFileAsString(), parsed);
    if (parse.failed()) return fail(error, "Malformed JSON: " + parse.getErrorMessage());
    return fromJson(parsed, result, error);
}
}
