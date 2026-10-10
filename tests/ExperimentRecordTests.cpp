#include <catch2/catch_test_macros.hpp>
#include "harness/ExperimentRecord.h"
#include <limits>

using namespace disdorktion::harness;

TEST_CASE("Extended crusher records require all expressive fields and upgrade legacy version", "[experiment][crusher]")
{
    ExperimentRecord extended;
    extended.version = 4; extended.targetId = "bit-crusher";
    extended.mix = 0.37f; extended.crush = 0.62f; extended.shape = -0.5f;
    extended.jitter = 0.3f; extended.jitterSeed = 0xffffffffu;
    juce::String error; ExperimentRecord restored;
    auto json = extended.toJson();
    REQUIRE(ExperimentRecord::fromJson(juce::JSON::parse(juce::JSON::toString(json)), restored, error));
    REQUIRE(restored.version == 4);
    REQUIRE(restored.mix == extended.mix); REQUIRE(restored.crush == extended.crush);
    REQUIRE(restored.shape == extended.shape); REQUIRE(restored.jitter == extended.jitter);
    REQUIRE(restored.jitterSeed == extended.jitterSeed);
    for (auto* key : {"mix", "crush", "shape", "jitter", "jitterSeed"})
    {
        auto missing = extended.toJson(); missing.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(missing, restored, error));
    }
    for (const auto bad : {juce::var(-0.01), juce::var(1.01), juce::var("0.5")})
    {
        auto invalid = extended.toJson(); invalid.getDynamicObject()->setProperty("mix", bad);
        REQUIRE_FALSE(ExperimentRecord::fromJson(invalid, restored, error));
    }
    auto invalidShape = extended.toJson(); invalidShape.getDynamicObject()->setProperty("shape", 1.01);
    REQUIRE_FALSE(ExperimentRecord::fromJson(invalidShape, restored, error));
    auto invalidSeed = extended.toJson(); invalidSeed.getDynamicObject()->setProperty("jitterSeed", 4294967296.0);
    REQUIRE_FALSE(ExperimentRecord::fromJson(invalidSeed, restored, error));
    ExperimentRecord legacy; legacy.version = 3; legacy.targetId = "bit-crusher";
    auto legacyJson = legacy.toJson();
    REQUIRE_FALSE(legacyJson.getDynamicObject()->hasProperty("mix"));
    REQUIRE(ExperimentRecord::fromJson(legacyJson, restored, error));
    REQUIRE(restored.mix == 1.0f); REQUIRE(restored.crush == 0.0f);
    REQUIRE(restored.shape == 0.0f); REQUIRE(restored.jitter == 0.0f); REQUIRE(restored.jitterSeed == 1u);
    legacyJson.getDynamicObject()->setProperty("mix", 1.0);
    REQUIRE_FALSE(ExperimentRecord::fromJson(legacyJson, restored, error));
    legacy.crush = 0.1f;
    REQUIRE_FALSE(legacy.validate(error));
    for (auto oldVersion : {1, 2})
    {
        ExperimentRecord old; old.version = oldVersion;
        old.targetId = oldVersion == 1 ? "reference-gain" : "gain";
        auto mixed = old.toJson(); mixed.getDynamicObject()->setProperty("jitter", 0.0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(mixed, restored, error));
    }
}

TEST_CASE("Bit crusher records round trip and reject mixed schemas", "[experiment][crusher]")
{
    ExperimentRecord record; record.version = 3; record.targetId = "bit-crusher";
    record.bitDepth = 8; record.captureRateHz = 12345.5f; record.bypass = true;
    juce::String error; ExperimentRecord restored;
    auto json = record.toJson();
    REQUIRE_FALSE(json.getDynamicObject()->hasProperty("gain"));
    REQUIRE_FALSE(json.getDynamicObject()->hasProperty("gainDb"));
    REQUIRE(ExperimentRecord::fromJson(json, restored, error));
    REQUIRE(restored.bitDepth == 8);
    REQUIRE(restored.captureRateHz == 12345.5f);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("crusher-record", ".json");
    REQUIRE(record.save(file, error));
    REQUIRE(ExperimentRecord::load(file, restored, error));
    REQUIRE(file.deleteFile());
    for (const auto* field : {"bitDepth", "captureRateHz", "bypass"})
    {
        auto missing = record.toJson(); missing.getDynamicObject()->removeProperty(field);
        REQUIRE_FALSE(ExperimentRecord::fromJson(missing, restored, error));
    }
    for (const auto value : {juce::var(0), juce::var(9), juce::var(65), juce::var(2.5), juce::var("8")})
    {
        auto invalid = record.toJson(); invalid.getDynamicObject()->setProperty("bitDepth", value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(invalid, restored, error));
    }
    for (const auto value : {juce::var(99), juce::var(384001), juce::var("12000")})
    {
        auto invalid = record.toJson(); invalid.getDynamicObject()->setProperty("captureRateHz", value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(invalid, restored, error));
    }
    auto mixed = record.toJson(); mixed.getDynamicObject()->setProperty("gain", 1.0);
    REQUIRE_FALSE(ExperimentRecord::fromJson(mixed, restored, error));
    mixed = record.toJson(); mixed.getDynamicObject()->setProperty("mute", false);
    REQUIRE_FALSE(ExperimentRecord::fromJson(mixed, restored, error));
}

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
