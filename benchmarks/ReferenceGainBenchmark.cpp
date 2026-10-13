// Offline timing harness for disdorktion::ReferenceGain.
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powrprof.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

#include "dsp/ReferenceGain.h"
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
#include "InputSectionBenchmarkPath.h"
#elif defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
#include "dsp/WaveFolder.h"
#else
#include "dsp/TanhSaturation.h"
#endif
#include "WallTimeSampling.h"
#include "AutomationCoverage.h"
#include "TimingWindow.h"
#endif
#if defined(DISDORKTION_BIT_CRUSHER_BENCHMARK)
#include "dsp/BitCrusher.h"
#include "TimingWindow.h"
#endif
#if defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
#include "GainBenchmarkPath.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
constexpr std::size_t timingCapacity = disdorktion::benchmark::WallTimeSampler::targetSlots;
#else
constexpr std::size_t timingCapacity = 200000;
#endif
constexpr std::size_t minimumTimings = 100000;
constexpr auto warmupDuration = std::chrono::seconds(2);
constexpr auto measurementDuration = std::chrono::seconds(10);
volatile float outputSink = 0.0f;

enum class Scenario { steady, automation, bypass, oversized, mute
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    , expressive, bypassGain, bypassCrusher, bypassBoth
#endif
};
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
constexpr std::array scenarios {Scenario::steady, Scenario::expressive, Scenario::automation,
    Scenario::bypass, Scenario::bypassGain, Scenario::bypassCrusher, Scenario::bypassBoth, Scenario::mute, Scenario::oversized};
#elif defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
constexpr std::array scenarios {Scenario::steady, Scenario::automation, Scenario::bypass, Scenario::oversized, Scenario::mute};
#else
constexpr std::array scenarios {Scenario::steady, Scenario::automation, Scenario::bypass, Scenario::oversized};
#endif

const char* scenarioName(Scenario value)
{
    switch (value)
    {
        case Scenario::steady: return "steady";
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        case Scenario::expressive: return "expressive-worst";
        case Scenario::bypassGain: return "bypass-gain";
        case Scenario::bypassCrusher: return "bypass-crusher";
        case Scenario::bypassBoth: return "bypass-both";
#endif
        case Scenario::automation: return "automation";
        case Scenario::bypass: return "bypass";
        case Scenario::oversized: return "oversized";
        case Scenario::mute: return "mute";
    }
    return "unknown";
}

struct Options
{
    unsigned stages = 1;
    bool tinyInput = false;
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
    bool wideInput = false;
    bool extremeInput = false;
#endif
    unsigned runs = 3;
    bool diagnostic = false;
    bool quick = false;
    std::string output;
    std::string filter;
    std::string caseId;
    std::string conditions;
};

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    bool specifiedRuns = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        auto value = [&]() -> const char* { return ++i < argc ? argv[i] : nullptr; };
        if (argument == "--diagnostic") options.diagnostic = true;
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
        else if (argument == "--tiny-input") options.tinyInput = true;
        else if (argument == "--wide-input") options.wideInput = true;
        else if (argument == "--extreme-input") options.extremeInput = true;
#endif
#if defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
        else if (argument == "--tiny-input") options.tinyInput = true;
        else if (argument == "--stages")
        {
            const auto* raw = value();
            if (!raw || std::strlen(raw) != 1 || *raw < '1' || *raw > '3') return std::nullopt;
            options.stages = static_cast<unsigned>(*raw - '0');
        }
#endif
        else if (argument == "--quick") options.quick = true;
        else if (argument == "--runs")
        {
            const auto* raw = value();
            if (raw == nullptr) return std::nullopt;
            char* end = nullptr;
            const auto count = std::strtoul(raw, &end, 10);
            if (*raw == '\0' || *end != '\0' || count == 0 || count > 100) return std::nullopt;
            options.runs = static_cast<unsigned>(count);
            specifiedRuns = true;
        }
        else if (argument == "--output") { const auto* raw = value(); if (!raw) return std::nullopt; options.output = raw; }
        else if (argument == "--case") { const auto* raw = value(); if (!raw) return std::nullopt; options.filter = raw; }
        else if (argument == "--case-id") { const auto* raw = value(); if (!raw) return std::nullopt; options.caseId = raw; }
        else if (argument == "--conditions") { const auto* raw = value(); if (!raw) return std::nullopt; options.conditions = raw; }
        else return std::nullopt;
    }
    if (!options.filter.empty() && std::none_of(scenarios.begin(), scenarios.end(), [&](auto s) { return options.filter == scenarioName(s); }))
        return std::nullopt;
    if (!options.caseId.empty())
    {
        bool valid = false;
        for (const unsigned rate : {48000u, 96000u})
            for (const unsigned block : {32u, 64u, 127u, 256u, 1024u})
                for (const auto scenario : scenarios)
                    valid |= options.caseId == std::to_string(rate) + "/" + std::to_string(block) + "/" + scenarioName(scenario);
        if (!valid) return std::nullopt;
        if (!options.filter.empty() && !options.caseId.ends_with("/" + options.filter)) return std::nullopt;
    }
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
    if (static_cast<unsigned>(options.tinyInput) + static_cast<unsigned>(options.wideInput)
        + static_cast<unsigned>(options.extremeInput) > 1) return std::nullopt;
#endif
    if (options.diagnostic) options.runs = 1;
    if (options.quick && !specifiedRuns) options.runs = 1;
    return options;
}

std::string csv(const std::string& input)
{
    std::string result = "\"";
    for (const char c : input) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}

std::string compilerName()
{
#if defined(_MSC_VER)
    std::string result = "MSVC " + std::to_string(_MSC_VER);
#if defined(_MSC_FULL_VER)
    result += " full=" + std::to_string(_MSC_FULL_VER);
#endif
#elif defined(__clang__)
    std::string result = "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    std::string result = "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    std::string result = "unknown";
#endif
#if defined(NDEBUG)
    result += " Release";
#else
    result += " Debug";
#endif
#if defined(_M_X64) || defined(__x86_64__)
    result += " x64";
#else
    result += " architecture-unknown";
#endif
    return result;
}

struct Platform
{
    std::string cpu = "unavailable";
    std::string architecture = "unavailable";
    std::string os = "unavailable";
    std::string sdk = "unavailable";
    std::string topology = "unavailable";
    std::string affinity = "unavailable";
    std::string power = "unavailable";
    std::string ac = "unavailable";
    std::string battery = "unavailable";
    std::string memory = "unavailable";
    bool hybridEstablished = false;
    bool pinned = false;
    bool onAC = false;
};

#if defined(_WIN32)
std::string hexMask(std::uintptr_t mask)
{
    std::ostringstream out;
    out << "0x" << std::hex << mask;
    return out.str();
}

std::string guidText(const GUID& guid)
{
    std::ostringstream out;
    out << '{' << std::hex << std::setfill('0') << std::setw(8) << guid.Data1 << '-'
        << std::setw(4) << guid.Data2 << '-' << std::setw(4) << guid.Data3 << '-'
        << std::setw(2) << static_cast<unsigned>(guid.Data4[0])
        << std::setw(2) << static_cast<unsigned>(guid.Data4[1]) << '-';
    for (unsigned i = 2; i < 8; ++i) out << std::setw(2) << static_cast<unsigned>(guid.Data4[i]);
    return out.str() + '}';
}

class ThreadAffinity
{
public:
    ThreadAffinity() = default;
    ThreadAffinity(const ThreadAffinity&) = delete;
    ThreadAffinity& operator=(const ThreadAffinity&) = delete;
    ~ThreadAffinity() { if (oldMask != 0) SetThreadAffinityMask(GetCurrentThread(), oldMask); }

    bool pin(std::uintptr_t mask)
    {
        oldMask = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask));
        return oldMask != 0;
    }
    DWORD_PTR originalMask() const { return oldMask; }
private:
    DWORD_PTR oldMask = 0;
};

Platform observePlatform(bool diagnostic, ThreadAffinity& affinity)
{
    Platform platform;
    platform.os = "Windows (version unavailable)";
    using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (const auto module = GetModuleHandleW(L"ntdll.dll"))
        if (const auto rtlGetVersion = reinterpret_cast<RtlGetVersion>(GetProcAddress(module, "RtlGetVersion")))
        {
            OSVERSIONINFOW version {sizeof(version)};
            if (rtlGetVersion(&version) == 0)
                platform.os = "Windows " + std::to_string(version.dwMajorVersion) + "."
                            + std::to_string(version.dwMinorVersion) + "." + std::to_string(version.dwBuildNumber);
        }
#if defined(DISDORKTION_WINDOWS_SDK_VERSION)
    platform.sdk = DISDORKTION_WINDOWS_SDK_VERSION;
#endif
    SYSTEM_INFO system {};
    GetNativeSystemInfo(&system);
    platform.architecture = "processor_architecture=" + std::to_string(system.wProcessorArchitecture)
                          + "; logical_processors=" + std::to_string(system.dwNumberOfProcessors);
    MEMORYSTATUSEX memory {sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) platform.memory = std::to_string(memory.ullTotalPhys) + " bytes";
#if defined(_MSC_VER)
    int cpuInfo[4] {};
    __cpuid(cpuInfo, 0x80000000);
    if (static_cast<unsigned>(cpuInfo[0]) >= 0x80000004u)
    {
        char brand[49] {};
        for (int leaf = 0; leaf < 3; ++leaf)
        {
            __cpuid(cpuInfo, 0x80000002 + leaf);
            std::memcpy(brand + leaf * 16, cpuInfo, 16);
        }
        platform.cpu = brand;
    }
#endif
    SYSTEM_POWER_STATUS power {};
    if (GetSystemPowerStatus(&power))
    {
        platform.ac = power.ACLineStatus == 1 ? "AC" : power.ACLineStatus == 0 ? "battery" : "unknown";
        platform.onAC = power.ACLineStatus == 1;
        if (power.BatteryLifePercent != 255) platform.battery = std::to_string(power.BatteryLifePercent) + "%";
    }
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) == ERROR_SUCCESS && scheme != nullptr)
    {
        platform.power = guidText(*scheme);
        LocalFree(scheme);
    }

    DWORD bytes = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        return platform;
    std::vector<std::uint8_t> storage(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()), &bytes))
        return platform;
    std::array<bool, 256> classes {};
    std::uintptr_t chosen = 0;
    BYTE highest = 0;
    DWORD_PTR processMask = 0, systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask)) return platform;
    std::ostringstream evidence;
    for (DWORD offset = 0; offset < bytes;)
    {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(storage.data() + offset);
        if (entry->Size == 0 || entry->Size > bytes - offset) return platform;
        const auto cls = entry->Processor.EfficiencyClass;
        classes[cls] = true;
        evidence << "class=" << static_cast<unsigned>(cls);
        for (WORD index = 0; index < entry->Processor.GroupCount; ++index)
        {
            const auto& mask = entry->Processor.GroupMask[index];
            evidence << "/group=" << mask.Group << "/mask=" << hexMask(mask.Mask) << ';';
            if (mask.Group == 0)
            {
                const auto eligible = mask.Mask & processMask & systemMask;
                if (eligible && (chosen == 0 || cls > highest))
                {
                    chosen = eligible & (~eligible + 1);
                    highest = cls;
                }
            }
        }
        offset += entry->Size;
    }
    platform.topology = evidence.str() + " process_mask=" + hexMask(processMask);
    platform.hybridEstablished = std::count(classes.begin(), classes.end(), true) >= 2;
    if (diagnostic) { platform.affinity = "unpinned diagnostic"; return platform; }
    if (!platform.hybridEstablished || chosen == 0) return platform;
    if (affinity.pin(chosen))
    {
        platform.pinned = true;
        platform.affinity = "selected_group=0 selected_mask=" + hexMask(chosen)
                          + " previous_thread_mask=" + hexMask(affinity.originalMask())
                          + " selected_efficiency_class=" + std::to_string(highest);
    }
    return platform;
}
#else
class ThreadAffinity {};
Platform observePlatform(bool, ThreadAffinity&) { return {}; }
#endif

struct Result
{
    unsigned rate = 0, block = 0, actual = 0, run = 0;
    Scenario scenario = Scenario::steady;
    std::size_t count = 0, stride = 0;
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    std::size_t automationCovered = 0, automationMinimum = 0, automationMaximum = 0;
#endif
    double warmupProjectedCallbacks = 0, calibratedCallbacks = 0, measurementSeconds = 0;
    double median = 0, p95 = 0, p99 = 0, maximum = 0, timer = 0, deadline = 0, fraction = 0, budgetFraction = 0;
    bool qualified = false;
    std::string reason;
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    std::size_t interactionCovered=0, interactionMinimum=0;
    std::size_t bypassCovered=0, bypassMinimum=0;
    std::vector<std::int64_t> rawTimings;
    std::vector<std::uint64_t> rawIterations;
#endif
};

constexpr std::uint64_t selectSamplingStride(std::uint64_t projected)
{
    constexpr std::uint64_t target = 150000;
    constexpr std::uint64_t minimum = 100000;
    constexpr std::uint64_t capacity = 200000;
    const auto lower = std::max<std::uint64_t>(1, projected / target);
    const auto upper = lower + (projected % target != 0 ? 1 : 0);
    const auto lowerCount = projected / lower;
    const auto upperCount = projected / upper;
    const bool lowerFits = lowerCount >= minimum && lowerCount <= capacity;
    const bool upperFits = upperCount >= minimum && upperCount <= capacity;
    if (lowerFits != upperFits) return lowerFits ? lower : upper;
    const auto lowerDistance = lowerCount > target ? lowerCount - target : target - lowerCount;
    const auto upperDistance = upperCount > target ? upperCount - target : target - upperCount;
    return upperDistance < lowerDistance ? upper : lower;
}

static_assert(selectSamplingStride(199000) == 1);
static_assert(selectSamplingStride(300000) == 2);
static_assert(selectSamplingStride(60000000) == 400);
static_assert(selectSamplingStride(260000) == 2);

double percentile(const std::vector<std::int64_t>& sorted, double p)
{
    const auto position = static_cast<std::size_t>((sorted.size() - 1) * p);
    return static_cast<double>(sorted[position]);
}

double measureTimerOverhead()
{
    std::vector<std::int64_t> samples(10000);
    for (auto& value : samples)
    {
        const auto before = Clock::now();
        const auto after = Clock::now();
        value = std::chrono::duration_cast<Nanoseconds>(after - before).count();
    }
    std::sort(samples.begin(), samples.end());
    return percentile(samples, 0.5);
}

#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
disdorktion::InputSettings parametersFor(Scenario scenario, std::uint64_t index, unsigned hostRate)
{
    if(scenario==Scenario::automation) return disdorktion::benchmark::inputAutomationSettings(index);
    if(scenario==Scenario::bypass) return disdorktion::benchmark::inputBypassSettings(index,hostRate);
    if(scenario==Scenario::steady) return {};
    auto settings=disdorktion::benchmark::inputExpressiveSettings(hostRate);
    settings.gain.mute=scenario==Scenario::mute;
    settings.gain.bypass=scenario==Scenario::bypassGain||scenario==Scenario::bypassBoth;
    settings.crusher.bypass=scenario==Scenario::bypassCrusher||scenario==Scenario::bypassBoth;
    return settings;
}
#elif defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
disdorktion::WaveFolderSettings parametersFor(Scenario scenario, std::uint64_t index)
{
    // The coprime permutations visit every state of each of the three controls.
    // Joint-cycle coverage therefore proves per-control coverage for this cycle.
    const auto state = index % 101;
    switch (scenario)
    {
        case Scenario::automation: return {
            static_cast<float>(-24.0 + static_cast<double>(state) * .6),
            static_cast<float>(.05 + static_cast<double>((state * 3) % 101) * .0095),
            static_cast<float>(-60.0 + static_cast<double>((state * 7) % 101) * .72), false};
        case Scenario::bypass: return {36.0f, .05f, 12.0f, (index / 8) % 2 != 0};
        default: return {36.0f, .05f, 12.0f, false};
    }
}
#elif defined(DISDORKTION_TANH_SATURATION_BENCHMARK)
disdorktion::TanhSaturationSettings parametersFor(Scenario scenario, std::uint64_t index)
{
    switch (scenario)
    {
        case Scenario::automation: return {static_cast<float>(-24.0 + static_cast<double>(index % 101) * .6),
            static_cast<float>(-60.0 + static_cast<double>((index * 7) % 101) * .72), false};
        case Scenario::bypass: return {36.0f, 12.0f, (index / 8) % 2 != 0};
        default: return {36.0f, 12.0f, false};
    }
}
#elif defined(DISDORKTION_BIT_CRUSHER_BENCHMARK)
disdorktion::BitCrusherSettings parametersFor(Scenario scenario, std::uint64_t index)
{
    switch (scenario)
    {
        case Scenario::automation: return {disdorktion::bitCrusherDepths[index % 11],
            index % 2 ? 100.0f : 384000.0f, false,
            index % 2 ? .2f : 1.0f, index % 2 ? .8f : .2f,
            index % 2 ? -.8f : .8f, index % 2 ? 1.0f : .3f, static_cast<std::uint32_t>(index)};
        case Scenario::bypass: return {8, 12345.5f, (index / 8) % 2 != 0, .5f, .5f, .8f, 1.0f, 1};
        case Scenario::oversized: return {64, 384000.0f, false, 1.0f, .5f, .8f, 1.0f, 1};
        case Scenario::steady: return {64, 384000.0f, false, 1.0f, .5f, .8f, 1.0f, 1};
        case Scenario::mute: return {};
    }
    return {};
}
#elif defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
disdorktion::GainSettings parametersFor(Scenario scenario, std::uint64_t index)
{
    switch (scenario)
    {
        case Scenario::automation: return {-60.0f + static_cast<float>(index % 101) * 0.84f, false, false};
        case Scenario::bypass: return {24.0f, false, (index / 8) % 2 != 0};
        case Scenario::mute: return {24.0f, (index / 8) % 2 != 0, false};
        case Scenario::oversized: return {24.0f, false, false};
        case Scenario::steady: return {24.0f, false, false};
    }
    return {};
}
#else
disdorktion::GainParameters parametersFor(Scenario scenario, std::uint64_t index)
{
    switch (scenario)
    {
        case Scenario::automation: return {0.5f + static_cast<float>(index % 101) * 0.01f, false};
        case Scenario::bypass: return {0.5f, (index / 8) % 2 != 0};
        case Scenario::oversized: return {0.5f, false};
        case Scenario::steady: return {1.0f, false};
        case Scenario::mute: return {0.0f, false};
    }
    return {};
}
#endif

Result measureCase(unsigned rate, unsigned block, Scenario scenario, unsigned run,
                   bool quick, double timerOverhead, const std::string& environmentReason, unsigned stages = 1,
                   bool tinyInput = false
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
                   , bool wideInput = false, bool extremeInput = false
#endif
                   )
{
    Result result;
    result.rate = rate;
    result.block = block;
    result.actual = scenario == Scenario::oversized ? 3 * block + 1 : block;
    result.run = run;
    result.scenario = scenario;
    const auto blockDeadline = 1e9 * static_cast<double>(result.actual) / rate;
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    const auto budgetPercent = 6u;
    disdorktion::InputSection gain;
#elif defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
    const auto budgetPercent = 5u;
    disdorktion::WaveFolder gain;
#elif defined(DISDORKTION_TANH_SATURATION_BENCHMARK)
    const auto budgetPercent = 5u;
    disdorktion::TanhSaturation gain;
#elif defined(DISDORKTION_BIT_CRUSHER_BENCHMARK)
    const auto budgetPercent = 5u;
    disdorktion::BitCrusher gain;
#else
    const auto budgetPercent = stages;
#endif
    result.deadline = blockDeadline * 0.01 * budgetPercent;

#if defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
    GainBenchmarkPath gain(stages);
#elif !defined(DISDORKTION_INPUT_SECTION_BENCHMARK) && !defined(DISDORKTION_BIT_CRUSHER_BENCHMARK) && !(defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    disdorktion::ReferenceGain gain;
#endif
    const auto settingsFor=[&](std::uint64_t index)
    {
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        return parametersFor(scenario,index,rate);
#else
        return parametersFor(scenario,index);
#endif
    };
    if (!gain.prepare({static_cast<double>(rate), block, 2}) || !gain.setParameters(settingsFor(0)))
    {
        result.reason = "prepare or initial parameters failed";
        return result;
    }
    juce::AudioBuffer<float> audio(2, static_cast<int>(result.actual));
    auto blockView = juce::dsp::AudioBlock<float>(audio);
    std::vector<std::int64_t> timings(timingCapacity);
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    std::vector<std::uint64_t> rawIterations(timingCapacity);
#endif
    std::uint64_t iteration = 0;
    bool processingFailed = false;
    auto callback = [&](bool record, std::size_t slot)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            auto* samples = audio.getWritePointer(channel);
            for (unsigned sample = 0; sample < result.actual; ++sample)
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
                samples[sample] = tinyInput ? (sample % 2 == 0 ? 1.0e-40f : -1.0e-40f)
                    : extremeInput ? (sample % 2 == 0 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max())
                    : (wideInput ? 8.0f : 1.0f) * (static_cast<float>((sample * 37u + channel * 13u + iteration) % 257u) / 256.0f - 0.5f);
#elif defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
                samples[sample] = (static_cast<float>((sample * 37u + channel * 13u + iteration) % 257u) / 257.0f - 0.5f) / 16.0f;
#else
                samples[sample] = tinyInput ? (sample % 2 == 0 ? 1.0e-40f : -1.0e-40f)
                    : static_cast<float>((sample * 37u + channel * 13u + iteration) % 257u) / 257.0f - 0.5f;
#endif
        }
        const auto controls = settingsFor(iteration);
        const bool dynamic = scenario == Scenario::automation || scenario == Scenario::bypass || scenario == Scenario::mute;
        const auto before = record ? Clock::now() : Clock::time_point {};
        {
            const juce::ScopedNoDenormals noDenormals;
            if (dynamic && !gain.setParameters(controls)) processingFailed = true;
            if (!gain.process(blockView)) processingFailed = true;
        }
        const auto after = record ? Clock::now() : Clock::time_point {};
        if (record) timings[slot] = std::chrono::duration_cast<Nanoseconds>(after - before).count();
        outputSink = outputSink + audio.getSample(0, static_cast<int>(iteration % result.actual));
        ++iteration;
    };

    const auto warmupStart = Clock::now();
    const auto warmupEnd = warmupStart + (quick ? std::chrono::milliseconds(100) : warmupDuration);
    while (Clock::now() < warmupEnd) callback(false, 0);
    const auto targetSeconds = quick ? 0.2 : 10.0;
    const auto warmupFinished = Clock::now();
    result.warmupProjectedCallbacks = static_cast<double>(iteration) * targetSeconds
        / std::chrono::duration<double>(warmupFinished - warmupStart).count();
    const auto probeStart = Clock::now();
    const auto probeEnd = probeStart + (quick ? std::chrono::milliseconds(50) : std::chrono::seconds(2));
    const auto beforeProbe = iteration;
    while (Clock::now() < probeEnd) callback(false, 0);
    const auto probeFinished = Clock::now();
    result.calibratedCallbacks = static_cast<double>(iteration - beforeProbe) * targetSeconds
        / std::chrono::duration<double>(probeFinished - probeStart).count();
#if defined(DISDORKTION_BIT_CRUSHER_BENCHMARK) || (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    const auto windowSeconds = quick ? 0.2 : disdorktion::benchmark::timingWindowSeconds(result.calibratedCallbacks);
    if (!quick)
    {
        result.warmupProjectedCallbacks *= windowSeconds / targetSeconds;
        result.calibratedCallbacks *= windowSeconds / targetSeconds;
    }
#endif
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    result.stride = 0; // Not applicable to revision 2 wall-time sampling.
#else
    result.stride = static_cast<std::size_t>(selectSamplingStride(static_cast<std::uint64_t>(std::ceil(result.calibratedCallbacks))));
#endif
    const auto measureStart = Clock::now();
#if defined(DISDORKTION_BIT_CRUSHER_BENCHMARK) || (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    const auto measureEnd = measureStart + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(windowSeconds));
#else
    const auto measureEnd = measureStart + (quick ? std::chrono::milliseconds(200) : measurementDuration);
#endif
    std::uint64_t callbackNumber = 0;
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    disdorktion::benchmark::AutomationCoverage coverage;
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    disdorktion::benchmark::InputInteractionCoverage interactionCoverage;
    disdorktion::benchmark::InputBypassCoverage bypassCoverage;
#endif
    disdorktion::benchmark::WallTimeSampler sampler(std::chrono::duration_cast<Nanoseconds>(measureEnd - measureStart).count());
    while (true)
    {
        // Reuse the existing loop clock read; scheduling remains outside callback timing.
        const auto now = Clock::now();
        if (now >= measureEnd) break;
        const bool record = sampler.shouldSample(std::chrono::duration_cast<Nanoseconds>(now - measureStart).count());
#else
    while (Clock::now() < measureEnd)
    {
        const bool scheduled = callbackNumber % result.stride == 0;
        const bool record = scheduled && result.count < timings.size();
        if (scheduled && !record) result.reason = "200000 timing capacity exhausted; full window retained, sampled distribution truncated";
#endif
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
        const auto controlIteration = iteration;
#endif
        callback(record, result.count);
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
        if (record && scenario == Scenario::automation) coverage.record(controlIteration);
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        if(record)
        {
            rawIterations[result.count]=controlIteration;
            if(scenario==Scenario::automation) interactionCoverage.record(controlIteration);
            if(scenario==Scenario::bypass) bypassCoverage.record(controlIteration);
        }
#endif
#endif
        if (record) ++result.count;
        ++callbackNumber;
    }
    const auto finished = Clock::now();
    result.measurementSeconds = std::chrono::duration<double>(finished - measureStart).count();
    const bool fullWindow = finished >= measureEnd;
    if (processingFailed)
    {
        if (!result.reason.empty()) result.reason += "; ";
        result.reason += "gain processing failed";
    }
    if (!quick && result.count < minimumTimings)
        result.reason = "fewer than 100000 sampled callbacks; revise measurement method";
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    result.automationCovered = coverage.coveredCount();
    result.automationMinimum = coverage.minimum();
    result.automationMaximum = coverage.maximum();
    if (!quick && scenario == Scenario::automation && !coverage.qualifies())
    {
        if (!result.reason.empty()) result.reason += "; ";
        result.reason += "automation sampling coverage below 100 observations for each of 101 settings";
    }
#endif
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    result.bypassCovered=bypassCoverage.coveredCount();
    result.bypassMinimum=bypassCoverage.minimum();
    if(!quick && scenario==Scenario::bypass && !bypassCoverage.qualifies())
    {
        if(!result.reason.empty()) result.reason+="; ";
        result.reason+="bypass sampling coverage below 100 observations for each of 4 states";
    }
    result.interactionCovered=interactionCoverage.coveredCount();
    result.interactionMinimum=interactionCoverage.minimum();
    if(!quick && scenario==Scenario::automation && !interactionCoverage.qualifies())
    {
        if(!result.reason.empty()) result.reason+="; ";
        result.reason+="depth/bypass coverage below 100 observations for each of 44 combinations";
    }
#endif
    if (!fullWindow && result.reason.empty()) result.reason = "measurement shorter than 10 seconds";
    if (result.count)
    {
        timings.resize(result.count);
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        result.rawTimings=timings;
        rawIterations.resize(result.count);result.rawIterations=std::move(rawIterations);
#endif
        std::sort(timings.begin(), timings.end());
        result.median = percentile(timings, 0.5);
        result.p95 = percentile(timings, 0.95);
        result.p99 = percentile(timings, 0.99);
        result.maximum = static_cast<double>(timings.back());
        result.fraction = result.p99 / blockDeadline;
        result.budgetFraction = result.p99 / result.deadline;
    }
    result.timer = timerOverhead;
    if (quick) result.reason = "quick smoke run; unqualified";
    else
    {
        if (!environmentReason.empty())
        {
            if (!result.reason.empty()) result.reason += "; ";
            result.reason += environmentReason;
        }
        if (result.count && result.p99 > result.deadline)
        {
            if (!result.reason.empty()) result.reason += "; ";
            result.reason += "raw p99 exceeds " + std::to_string(budgetPercent) + "% deadline";
        }
    }
    result.qualified = result.reason.empty();
    return result;
}

void writeHeader(std::ostream& out)
{
#if defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK) || defined(DISDORKTION_BIT_CRUSHER_BENCHMARK) || (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    out << "target,stages,latency_samples,input_profile,";
#endif
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    out << "collector_revision,sampling_mode,automation_settings_covered,automation_min_samples_per_setting,automation_max_samples_per_setting,";
#endif
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    out << "depth_bypass_combinations_covered,depth_bypass_min_samples,bypass_states_covered,bypass_min_samples_per_state,build,juce,";
#endif
    out << "mode,run,rate_hz,prepared_block,actual_block,scenario,timing_count,sampling_stride,warmup_projected_callbacks,calibrated_callbacks,measurement_seconds,median_ns,p95_ns,p99_ns,max_ns,timer_only_median_ns,p99_budget_ns,p99_block_deadline_fraction,p99_budget_fraction,qualified,reason,cpu,architecture,physical_memory,os,sdk,compiler,power_scheme_guid,ac_status,battery_percent,topology,affinity,conditions,thermal_measurement\n";
}

void writeResult(std::ostream& out, const Result& result, const Platform& platform, const Options& options)
{
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    out << "input-section,1,0,interior-post-gain,";
#elif defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
    out << "wave-folder,1,0," << (options.tinyInput ? "subnormal" : options.wideInput ? "curve-envelope[-4;4]"
        : options.extremeInput ? "finite-float-extremes" : "ordinary") << ',';
#elif defined(DISDORKTION_TANH_SATURATION_BENCHMARK)
    out << "tanh-saturation,1,0," << (options.tinyInput ? "subnormal" : "ordinary") << ',';
#elif defined(DISDORKTION_BIT_CRUSHER_BENCHMARK)
    out << "bit-crusher,1,0,ordinary,";
#elif defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
    out << "production-gain," << options.stages << ",0," << (options.tinyInput ? "subnormal" : "ordinary") << ',';
#endif
#if (defined(DISDORKTION_TANH_SATURATION_BENCHMARK) || defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK))
    out << "2,equal-wall-time-slots-150000-no-catchup,"
        << result.automationCovered << ',' << result.automationMinimum << ',' << result.automationMaximum << ',';
#endif
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    out << result.interactionCovered << ',' << result.interactionMinimum << ',' << result.bypassCovered << ',' << result.bypassMinimum << ',' << csv(DISDORKTION_BUILD_ID) << ',' << csv(juce::SystemStats::getJUCEVersion().toStdString()) << ',';
#endif
    out << (options.quick ? "quick" : options.diagnostic ? "diagnostic" : "baseline") << ','
        << result.run << ',' << result.rate << ',' << result.block << ',' << result.actual << ',' << scenarioName(result.scenario) << ','
        << result.count << ',' << result.stride << ',' << result.warmupProjectedCallbacks << ','
        << result.calibratedCallbacks << ',' << result.measurementSeconds << ','
        << result.median << ',' << result.p95 << ',' << result.p99 << ',' << result.maximum << ','
        << result.timer << ',' << result.deadline << ',' << result.fraction << ',' << result.budgetFraction << ','
        << (result.qualified ? "true" : "false") << ','
        << csv(result.reason) << ',' << csv(platform.cpu) << ',' << csv(platform.architecture) << ',' << csv(platform.memory) << ','
        << csv(platform.os) << ',' << csv(platform.sdk) << ',' << csv(compilerName()) << ',' << csv(platform.power) << ','
        << csv(platform.ac) << ',' << csv(platform.battery) << ',' << csv(platform.topology) << ',' << csv(platform.affinity) << ','
        << csv(options.conditions) << ',' << csv("unavailable: no thermal sensor measurement") << '\n';
}
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
struct CasePower { bool onAC=false; std::string status="unavailable"; };
CasePower observeCasePower()
{
#if defined(_WIN32)
    SYSTEM_POWER_STATUS status {};
    if(GetSystemPowerStatus(&status))
        return {status.ACLineStatus==1,status.ACLineStatus==1?"AC":status.ACLineStatus==0?"battery":"unknown"};
#endif
    return {};
}
#endif
} // namespace

int main(int argc, char** argv)
{
    const auto parsed = parseOptions(argc, argv);
    if (!parsed)
    {
        std::cerr << "Usage: benchmark [--runs N] [--diagnostic] [--quick] [--case NAME] [--case-id RATE/BLOCK/SCENARIO] [--conditions TEXT] [--output FILE]";
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        std::cerr << " (cases steady|expressive-worst|automation|bypass|bypass-gain|bypass-crusher|bypass-both|mute|oversized)";
#elif defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
        std::cerr << " [--tiny-input|--wide-input|--extreme-input] (cases steady|automation|bypass|oversized)";
#elif defined(DISDORKTION_PRODUCTION_GAIN_BENCHMARK)
        std::cerr << " [--stages 1|2|3] [--tiny-input] (cases steady|automation|bypass|oversized|mute)";
#else
        std::cerr << " (cases steady|automation|bypass|oversized)";
#endif
        std::cerr << '\n';
        return 2;
    }
    const auto options = *parsed;
    std::cerr << "Expected reference: Intel Core Ultra 7 155H, 16 cores / 22 logical processors, approximately 32 GiB RAM, Windows 11 Home 10.0.26200. Observations are written in each CSV row.\n";
    ThreadAffinity affinity;
    const auto platform = observePlatform(options.diagnostic, affinity);
    const bool releaseBuild = compilerName().find(" Release") != std::string::npos;
    const bool specialInput = options.tinyInput
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
        || options.wideInput || options.extremeInput
#endif
        ;
    const bool environmentQualified = releaseBuild && !specialInput && !options.quick && !options.diagnostic && options.runs >= 3
        && options.filter.empty() && options.caseId.empty()
        && platform.hybridEstablished && platform.pinned && platform.onAC && platform.power != "unavailable"
        && platform.cpu.find("Ultra") != std::string::npos && platform.cpu.find("155H") != std::string::npos
        && !options.conditions.empty()
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        && !options.output.empty()
#endif
        ;
    std::string environmentReason;
    if (!environmentQualified)
    {
        if (!releaseBuild) environmentReason = "Debug build; Release reference qualification unavailable";
        else if (options.tinyInput) environmentReason = "tiny-input diagnostic; reference qualification unavailable";
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
        else if (specialInput) environmentReason = "finite-envelope diagnostic; reference qualification unavailable";
#endif
        else if (options.diagnostic) environmentReason = platform.onAC
            ? "unpinned diagnostic; reference qualification unavailable"
            : "unpinned battery diagnostic; reference qualification unavailable";
        else if (!platform.onAC) environmentReason = "battery power; reference qualification unavailable";
        else if (!options.caseId.empty() || !options.filter.empty()) environmentReason = "filtered case run; reference qualification unavailable";
        else environmentReason = "environment qualification unresolved";
    }
    std::ofstream file;
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    std::ofstream rawFile;
    if(!options.output.empty())
    {
        const auto rawPath=juce::String(options.output+".timings.csv");
        const auto rawDestination=juce::File::isAbsolutePath(rawPath)?juce::File(rawPath):juce::File::getCurrentWorkingDirectory().getChildFile(rawPath);
        if(rawDestination.exists()){std::cerr<<"Raw timing file must not exist\n";return 2;}
    }
#endif
    if (!options.output.empty())
    {
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK) || defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
        const auto requested = juce::String(options.output);
        const auto destination = juce::File::isAbsolutePath(requested) ? juce::File(requested)
            : juce::File::getCurrentWorkingDirectory().getChildFile(requested);
        if (destination.exists())
        { std::cerr << "Output file must not exist; refusing to alter previous results\n"; return 2; }
#endif
        file.open(options.output, std::ios::out | std::ios::trunc);
        if (!file) { std::cerr << "Cannot open output file: " << options.output << '\n'; return 2; }
    }
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
    if(!options.output.empty())
    {
        rawFile.open(options.output+".timings.csv");
        if(!rawFile){std::cerr<<"Cannot open raw timing file\n";return 2;}
        rawFile<<"run,rate_hz,prepared_block,actual_block,scenario,observation,control_iteration,elapsed_ns\n";
    }
#endif
    auto& out = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;
    out << std::fixed << std::setprecision(3);
    writeHeader(out);
    const auto timerOverhead = measureTimerOverhead();
    bool allQualified = true;
    for (unsigned run = 1; run <= options.runs; ++run)
        for (const unsigned rate : {48000u, 96000u})
            for (const unsigned block : {32u, 64u, 127u, 256u, 1024u})
                for (const auto scenario : scenarios)
                {
                    if (!options.filter.empty() && options.filter != scenarioName(scenario)) continue;
                    if (!options.caseId.empty() && options.caseId != std::to_string(rate) + "/" + std::to_string(block) + "/" + scenarioName(scenario)) continue;
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
                    const auto beforePower=observeCasePower();
                    auto casePlatform=platform;
                    auto caseReason=environmentReason;
                    if(!beforePower.onAC)
                    {if(!caseReason.empty())caseReason+="; ";caseReason+="case start AC power unavailable";}
                    auto result = measureCase(rate, block, scenario, run, options.quick, timerOverhead, caseReason, options.stages, options.tinyInput);
                    const auto afterPower=observeCasePower();
                    casePlatform.ac="before="+beforePower.status+";after="+afterPower.status;
                    casePlatform.onAC=beforePower.onAC&&afterPower.onAC;
                    if(!afterPower.onAC)
                    {if(!result.reason.empty())result.reason+="; ";result.reason+="case finish AC power unavailable";result.qualified=false;}
                    writeResult(out,result,casePlatform,options);
#else
                    const auto result = measureCase(rate, block, scenario, run, options.quick, timerOverhead, environmentReason, options.stages, options.tinyInput
#if defined(DISDORKTION_WAVE_FOLDER_BENCHMARK)
                        , options.wideInput, options.extremeInput
#endif
                    );
                    writeResult(out, result, platform, options);
#endif
#if defined(DISDORKTION_INPUT_SECTION_BENCHMARK)
                    if(rawFile.is_open())
                    {
                        for(std::size_t observation=0;observation<result.rawTimings.size();++observation)
                            rawFile<<run<<','<<rate<<','<<block<<','<<result.actual<<','<<scenarioName(scenario)<<','<<observation<<','<<result.rawIterations[observation]<<','<<result.rawTimings[observation]<<'\n';
                        rawFile.flush();
                        if(!rawFile){std::cerr<<"Failed to write raw timings\n";return 2;}
                    }
#endif
                    out.flush();
                    allQualified &= result.qualified;
                    std::cerr << "run " << run << " " << rate << "Hz " << block << " " << scenarioName(scenario)
                              << ": p99=" << result.p99 << "ns budget=" << result.deadline << "ns "
                              << (result.qualified ? "qualified" : result.reason) << '\n';
                }
    if (!out) { std::cerr << "Failed to write results\n"; return 2; }
    return allQualified ? 0 : 1;
}
