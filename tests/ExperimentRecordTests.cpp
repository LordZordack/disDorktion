#include <catch2/catch_test_macros.hpp>
#include "harness/ExperimentRecord.h"
#include <limits>

using namespace disdorktion::harness;

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
