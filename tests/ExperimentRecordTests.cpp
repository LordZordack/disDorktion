#include <catch2/catch_test_macros.hpp>
#include "harness/ExperimentRecord.h"
#include <limits>

using namespace disdorktion::harness;

TEST_CASE("Production experiments use explicit dB and module mute fields", "[experiment][gain]")
{
    ExperimentRecord record; record.version = 2; record.targetId = "gain";
    record.gainDb = -60.0f; record.mute = true; record.muted = false;
    juce::String error; ExperimentRecord restored;
    auto json = record.toJson();
    REQUIRE_FALSE(json.getDynamicObject()->hasProperty("gain"));
    REQUIRE(ExperimentRecord::fromJson(json, restored, error));
    REQUIRE(restored.gainDb == -60.0f); REQUIRE(restored.mute); REQUIRE_FALSE(restored.muted);
    for (const auto value : {juce::var(-60.00000001), juce::var(24.00000001), juce::var(true), juce::var("0")})
    {
        auto invalid = record.toJson(); invalid.getDynamicObject()->setProperty("gainDb", value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(invalid, restored, error));
        REQUIRE(restored.gainDb == -60.0f);
    }
    auto mixed = record.toJson(); mixed.getDynamicObject()->setProperty("gain", 1.0);
    REQUIRE_FALSE(ExperimentRecord::fromJson(mixed, restored, error));
    for (const auto* key : {"gainDb", "mute", "bypass"})
    {
        auto missing = record.toJson(); missing.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(missing, restored, error));
    }
    auto wrongType = record.toJson(); wrongType.getDynamicObject()->setProperty("mute", 1);
    REQUIRE_FALSE(ExperimentRecord::fromJson(wrongType, restored, error));
    auto wrongTarget = record.toJson(); wrongTarget.getDynamicObject()->setProperty("targetId", "reference-gain");
    REQUIRE_FALSE(ExperimentRecord::fromJson(wrongTarget, restored, error));
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("production-gain-record", ".json");
    REQUIRE(record.save(file, error)); REQUIRE(ExperimentRecord::load(file, restored, error));
    REQUIRE(restored.gainDb == record.gainDb); REQUIRE(restored.mute); REQUIRE_FALSE(restored.muted);
    REQUIRE(file.deleteFile());
}

TEST_CASE("Version one experiments preserve quiet linear gains without production fields", "[experiment][gain]")
{
    for (const auto gain : {0.0f, 0.00001f, 0.001f, 1.0f, 4.0f})
    {
        ExperimentRecord record; record.gain = gain;
        const auto json = record.toJson();
        REQUIRE_FALSE(json.getDynamicObject()->hasProperty("gainDb"));
        REQUIRE_FALSE(json.getDynamicObject()->hasProperty("mute"));
        ExperimentRecord restored; juce::String error;
        REQUIRE(ExperimentRecord::fromJson(juce::JSON::parse(juce::JSON::toString(json)), restored, error));
        REQUIRE(restored.gain == gain); REQUIRE(restored.version == 1); REQUIRE(restored.targetId == "reference-gain");
        auto mixed = record.toJson(); mixed.getDynamicObject()->setProperty("gainDb", 0.0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(mixed, restored, error));
    }
}

TEST_CASE("Experiment JSON preserves complete settings and annotations", "[experiment]")
{
    ExperimentRecord record;
    record.gain = 2.5f; record.bypass = true; record.source.kind = SourceKind::noise;
    record.source.seed = 0xffffffffU; record.source.durationSamples = 371;
    record.renderDurationSamples = 1031; record.observations = "A quoted \"observation\"\nsecond line";
    juce::String error;
    ExperimentRecord restored;
    REQUIRE(ExperimentRecord::fromJson(juce::JSON::parse(juce::JSON::toString(record.toJson())), restored, error));
    REQUIRE(juce::JSON::toString(record.toJson()) == juce::JSON::toString(restored.toJson()));
    REQUIRE(restored.source.seed == 0xffffffffU);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("disdorktion-record", ".json");
    REQUIRE(record.save(file, error));
    REQUIRE(ExperimentRecord::load(file, restored, error));
    REQUIRE(file.deleteFile());
}

TEST_CASE("Malformed experiment restore retains prior record", "[experiment]")
{
    ExperimentRecord destination; destination.observations = "retained";
    juce::String error;
    for (const auto& field : {"version", "targetId", "gain", "bypass", "source", "sampleRate", "channels",
          "blockSize", "renderDurationSamples", "monitorDb", "muted", "fileHash", "observations"})
    {
        auto malformed = ExperimentRecord{}.toJson();
        malformed.getDynamicObject()->removeProperty(field);
        REQUIRE_FALSE(ExperimentRecord::fromJson(malformed, destination, error));
        REQUIRE(destination.observations == "retained"); REQUIRE(error.isNotEmpty());
    }
    for (const auto value : {juce::var("2"), juce::var(true), juce::var(1.5), juce::var(0), juce::var(2)})
    {
        auto malformed = ExperimentRecord{}.toJson(); malformed.getDynamicObject()->setProperty("version", value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(malformed, destination, error));
    }
    for (const auto value : {juce::var("48000"), juce::var(-1), juce::var(0), juce::var(1.25),
          juce::var(9007199254740992.0), juce::var(std::numeric_limits<double>::infinity())})
    {
        auto malformed = ExperimentRecord{}.toJson(); malformed.getDynamicObject()->setProperty("renderDurationSamples", value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(malformed, destination, error));
    }
    for (const auto value : {juce::var("1"), juce::var(true), juce::var(-0.1), juce::var(4.1),
          juce::var(std::numeric_limits<double>::quiet_NaN())})
    {
        auto malformed = ExperimentRecord{}.toJson(); malformed.getDynamicObject()->setProperty("gain", value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(malformed, destination, error));
    }
    REQUIRE(destination.observations == "retained");
}
