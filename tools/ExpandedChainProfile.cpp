#include "CombBenchmarkPlatform.h"
#include "FullChainQualification.h"
#include "dsp/FullChainControls.h"
#include "../benchmarks/ExpandedTimingWindow.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

// Deliberately separate from qualification: filtered cases, extra timers and
// thread accounting can diagnose a failure, but can never qualify a reference.
namespace
{
using namespace disdorktion;
using namespace disdorktion::qualification;
using Clock = std::chrono::steady_clock;
enum class ProfileMode { full, controls, sections };
constexpr unsigned frames = 32;
constexpr std::array<const char*, 6> columns {"total", "controls", "input", "middle", "output", "gain"};
using Durations = std::array<long long, 6>;

std::string maskText(std::uintptr_t mask)
{
    std::ostringstream out;
    out << "0x" << std::hex << mask;
    return out.str();
}

struct Options
{
    std::filesystem::path settingsFile, output;
    std::string mode = "full", conditions = "conditions unspecified";
    unsigned rounds = 3;
    double seconds = 10., soak = 0.;
    bool reverse = false, active = false, automation = false, highQos = false;
    std::uintptr_t coreMask = 0;
};

Options parseOptions(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--reverse") o.reverse = true;
        else if (arg == "--active") o.active = true;
        else if (arg == "--automation") o.automation = true;
        else if (arg == "--high-qos") o.highQos = true;
        else
        {
            require(i + 1 < argc);
            const std::string value(argv[++i]);
            if (arg == "--settings") o.settingsFile = value;
            else if (arg == "--output") o.output = value;
            else if (arg == "--mode") o.mode = value;
            else if (arg == "--conditions") o.conditions = value;
            else if (arg == "--core-mask")
            {
                require(!value.empty() && value.front() != '-');
                std::size_t end = 0;
                const auto mask = std::stoull(value, &end, 16);
                require(end == value.size() && mask != 0 && (mask & (mask - 1)) == 0
                        && mask <= std::numeric_limits<std::uintptr_t>::max());
                o.coreMask = static_cast<std::uintptr_t>(mask);
            }
            else if (arg == "--rounds")
            {
                std::size_t end = 0;
                const auto n = std::stoul(value, &end);
                require(end == value.size() && n >= 1 && n <= 100);
                o.rounds = static_cast<unsigned>(n);
            }
            else if (arg == "--seconds" || arg == "--soak-seconds")
            {
                std::size_t end = 0;
                const auto n = std::stod(value, &end);
                require(end == value.size() && std::isfinite(n) && n >= 0. && n <= 1200.);
                (arg == "--seconds" ? o.seconds : o.soak) = n;
            }
            else throw std::runtime_error("unknown option: " + arg);
        }
    }
    require(!o.settingsFile.empty() && !o.output.empty() && o.seconds >= 10.);
    require(o.mode == "full" || o.mode == "controls-only" || o.mode == "sections");
    require(!(o.active && o.automation));
    require((!o.active && !o.automation) || o.mode == "full");
    require(!std::filesystem::exists(o.output));
    return o;
}

template<class State>
juce::var powerFlags(const State& state, bool available, unsigned long error)
{
    auto result = object();
    put(result, "available", available); put(result, "error", juce::int64(error));
    if (available)
    {
        put(result, "version", juce::int64(state.Version));
        put(result, "control_mask", juce::int64(state.ControlMask));
        put(result, "state_mask", juce::int64(state.StateMask));
    }
    return result;
}

juce::var schedulingSnapshot()
{
    auto result = object();
    auto unavailable = object();
    put(unavailable, "available", juce::var(false));
    put(unavailable, "reason", "API unavailable in compiled SDK/platform");
    put(result, "process_power_throttling", unavailable);
    put(result, "thread_power_throttling", unavailable);
    put(result, "thread_group_affinity", unavailable);
#if defined(_WIN32)
    GROUP_AFFINITY affinity {};
    const bool haveAffinity = GetThreadGroupAffinity(GetCurrentThread(), &affinity) != 0;
    const DWORD affinityError = haveAffinity ? ERROR_SUCCESS : GetLastError();
    auto group = object();
    put(group, "available", haveAffinity); put(group, "error", juce::int64(affinityError));
    if (haveAffinity)
    {
        put(group, "group", int(affinity.Group));
        put(group, "mask", combmeasure::hexMask(affinity.Mask));
    }
    PROCESSOR_NUMBER processor {};
    GetCurrentProcessorNumberEx(&processor);
    put(group, "observed_processor_group", int(processor.Group));
    put(group, "observed_processor_number", int(processor.Number));
    put(result, "thread_group_affinity", group);
#if defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
    PROCESS_POWER_THROTTLING_STATE process {};
    process.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    const bool haveProcess = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &process, sizeof(process)) != 0;
    const DWORD processError = haveProcess ? ERROR_SUCCESS : GetLastError();
    put(result, "process_power_throttling", powerFlags(process, haveProcess, processError));
#endif
#if defined(THREAD_POWER_THROTTLING_CURRENT_VERSION)
    THREAD_POWER_THROTTLING_STATE thread {};
    thread.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    const bool haveThread = GetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &thread, sizeof(thread)) != 0;
    const DWORD threadError = haveThread ? ERROR_SUCCESS : GetLastError();
    put(result, "thread_power_throttling", powerFlags(thread, haveThread, threadError));
#endif
#endif
    put(result, "interpretation", "control/state masks describe explicit policy only; system-inferred QoS, effective frequency and throttling are not measured");
    return result;
}

// Changes only this diagnostic process, and only when explicitly requested.
// Restore the complete captured policy, including OS-managed ControlMask=0.
class ProcessQos
{
public:
    explicit ProcessQos(bool request) : requested(request), before(schedulingSnapshot())
    {
        if (requested) setError = 50; // ERROR_NOT_SUPPORTED if compiled without the API.
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
        if (!requested) return;
        saved.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if (!GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &saved, sizeof(saved)))
        {
            setError = GetLastError(); return;
        }
        auto desired = saved;
        desired.ControlMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        desired.StateMask &= ~PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        attempted = true;
        changed = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &desired, sizeof(desired)) != 0;
        succeeded = changed;
        setError = succeeded ? ERROR_SUCCESS : GetLastError();
#endif
    }
    ProcessQos(const ProcessQos&) = delete;
    ProcessQos& operator=(const ProcessQos&) = delete;
    ~ProcessQos() { restore(); }
    void restore() noexcept
    {
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
        if (!changed) return;
        restoreAttempted = true;
        restoreSucceeded = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &saved, sizeof(saved)) != 0;
        restoreError = restoreSucceeded ? ERROR_SUCCESS : GetLastError();
        if (restoreSucceeded) changed = false;
#endif
    }
    juce::var metadata() const
    {
        auto result = object();
        put(result, "requested", requested); put(result, "set_attempted", attempted); put(result, "set_succeeded", succeeded);
        put(result, "set_error", juce::int64(setError)); put(result, "before_request", before);
        put(result, "restore_attempted", restoreAttempted); put(result, "restore_succeeded", restoreSucceeded);
        put(result, "restore_error", juce::int64(restoreError));
        put(result, "scope", "own diagnostic process only; EXECUTION_SPEED control on and state off; all other captured policy bits preserved");
        return result;
    }
private:
    bool requested = false, attempted = false, succeeded = false, changed = false;
    bool restoreAttempted = false, restoreSucceeded = false;
    unsigned long setError = 0, restoreError = 0;
    juce::var before;
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
    PROCESS_POWER_THROTTLING_STATE saved {};
#endif
};

class CoreOverride
{
public:
    CoreOverride(std::uintptr_t mask, bool defaultPinned)
    {
        if (mask == 0) return;
#if defined(_WIN32)
        require(defaultPinned && mask != 0 && (mask & (mask-1)) == 0);
        GROUP_AFFINITY current {};
        require(GetThreadGroupAffinity(GetCurrentThread(), &current) != 0 && current.Group == 0);
        DWORD bytes = 0;
        (void) GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
        require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes != 0);
        std::vector<std::uint8_t> storage(bytes);
        require(GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()), &bytes) != 0);
        DWORD_PTR processMask = 0, systemMask = 0;
        require(GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) != 0);
        BYTE highest = 0;
        std::uintptr_t performanceMask = 0;
        for (DWORD offset = 0; offset < bytes;)
        {
            const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(storage.data()+offset);
            require(entry->Size != 0 && entry->Size <= bytes-offset);
            const auto cls = entry->Processor.EfficiencyClass;
            if (cls > highest) { highest = cls; performanceMask = 0; }
            for (WORD i = 0; i < entry->Processor.GroupCount; ++i)
            {
                const auto& group = entry->Processor.GroupMask[i];
                if (group.Group != 0) continue;
                if (cls == highest) performanceMask |= group.Mask;
            }
            offset += entry->Size;
        }
        require((mask & performanceMask & processMask & systemMask) == mask);
        previous = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask));
        require(previous != 0);
#else
        (void) defaultPinned;
        throw std::runtime_error("--core-mask requires Windows topology and affinity APIs");
#endif
    }
    CoreOverride(const CoreOverride&) = delete;
    CoreOverride& operator=(const CoreOverride&) = delete;
    ~CoreOverride()
    {
#if defined(_WIN32)
        if (previous != 0) SetThreadAffinityMask(GetCurrentThread(), previous);
#endif
    }
private:
#if defined(_WIN32)
    DWORD_PTR previous = 0;
#endif
};

FullChainSettings loadSettings(const std::filesystem::path& path)
{
    const juce::File file(juce::String(std::filesystem::absolute(path).string()));
    require(file.existsAsFile());
    const auto metadata = juce::JSON::parse(file.loadFileAsString());
    const auto controls = metadata.getProperty("initial_settings", juce::var());
    require(controls.isObject());
    FullChainSettings s;
    for (const auto& control : fullChainControls)
    {
        require(controls.hasProperty(control.id));
        const auto value = controls[control.id];
        require(value.isDouble() || value.isInt() || value.isInt64() || value.isBool());
        require(setFullChainControl(s, control.id, static_cast<float>(value)));
    }
    require(controls.hasProperty("input.crusher.jitterSeed"));
    const double seed = static_cast<double>(controls["input.crusher.jitterSeed"]);
    require(std::isfinite(seed) && seed >= 0. && seed <= double(std::numeric_limits<std::uint32_t>::max()) && seed == std::floor(seed));
    s.input.crusher.jitterSeed = static_cast<std::uint32_t>(seed);
    require(isValidFullChainSettings(s));
    return s;
}

juce::var controlSnapshot(const FullChainSettings& s)
{
    auto result = settings(s);
    for (const auto& c : fullChainControls) put(result, c.id, getFullChainControl(s, c.id));
    put(result, "input.crusher.jitterSeed", juce::int64(s.input.crusher.jitterSeed));
    return result;
}

// Matches ExpandedChainQualification's automation; invocation is outside timing.
void profileAutomation(FullChainSettings& s, std::uint64_t iteration, unsigned dwell)
{
    const float t = float(iteration % 101) / 100.f;
    s.output.comb.delayMs = .25f + 39.75f * t;
    s.output.comb.feedback = -.95f + 1.9f * t;
    s.output.comb.feedforward = -1.f + 2.f * t;
    s.output.comb.dampingHz = 20.f + 19980.f * t;
    s.output.comb.dampingEnabled = iteration % 2 != 0;
    s.output.dispersion.frequencyHz = 20.f + 19980.f * t;
    s.output.tone.toneDb = -6.f + 12.f * t;
    s.output.comb.bypass = (iteration / dwell) % 2 != 0;
    s.output.dispersion.bypass = (iteration / dwell) % 3 == 1;
    s.output.tone.bypass = (iteration / dwell) % 5 == 1;
    s.mix = t;
}

long long ns(Clock::duration duration)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

struct ThreadCounters
{
    bool cpuAvailable = false, cyclesAvailable = false;
    std::uint64_t kernel100ns = 0, user100ns = 0, cycles = 0;
};

ThreadCounters threadCounters()
{
    ThreadCounters result;
#if defined(_WIN32)
    FILETIME created {}, exited {}, kernel {}, user {};
    result.cpuAvailable = GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != 0;
    if (result.cpuAvailable)
    {
        result.kernel100ns = (std::uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
        result.user100ns = (std::uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
    }
    ULONG64 cycles = 0;
    result.cyclesAvailable = QueryThreadCycleTime(GetCurrentThread(), &cycles) != 0;
    result.cycles = cycles;
#endif
    return result;
}

juce::var counterSnapshot(const ThreadCounters& c)
{
    auto result = object();
    put(result, "cpu_available", c.cpuAvailable); put(result, "cycles_available", c.cyclesAvailable);
    put(result, "kernel_100ns", juce::int64(c.kernel100ns)); put(result, "user_100ns", juce::int64(c.user100ns));
    put(result, "cycles", juce::int64(c.cycles));
    return result;
}

juce::var statistics(std::vector<long long> values, double threshold)
{
    require(!values.empty());
    std::sort(values.begin(), values.end());
    auto result = object();
    for (const auto& percentile : std::array<std::pair<const char*, double>, 3>{{{"p50_ns", .5}, {"p95_ns", .95}, {"p99_ns", .99}}})
        put(result, percentile.first, juce::int64(values[std::size_t(std::ceil(percentile.second * values.size())) - 1]));
    put(result, "maximum_ns", juce::int64(values.back()));
    put(result, "count", int(values.size()));
    // The reference condition is strictly <25%; equality also fails.
    put(result, "at_or_above_budget_count", int(std::count_if(values.begin(), values.end(), [threshold](auto n) { return double(n) >= threshold; })));
    return result;
}

template<ProfileMode mode>
juce::var collectCase(const Options& o, const FullChainSettings& initial, double rate, unsigned round,
                      unsigned id, combmeasure::ThreadAffinity& affinity, Clock::time_point sessionStart)
{
    const auto before = combmeasure::observePlatform(true, affinity);
    const auto schedulingBefore = schedulingSnapshot();
    const juce::dsp::ProcessSpec spec {rate, frames, 2};
    FullChain chain;
    InputSection input; MiddleSection middle; OutputSection output; Gain gain;
    if constexpr (mode == ProfileMode::sections)
    {
        // This attribution path has no FullChain dry/mix wrapper.
        require(initial.mix == 1.f && !initial.bypass);
        require(input.setParameters(initial.input)); require(middle.setParameters(initial.middle));
        require(output.setParameters(initial.output)); require(gain.setParameters(initial.finalGain));
        require(input.prepare(spec));
        require(middle.prepare(spec, 4, MiddleLatencyCorrection::fir, MiddleFoldAlgorithm::adaaTriangle));
        require(output.prepare(spec)); require(gain.prepare(spec));
        require(middle.getLatencySamples() == 92);
    }
    else
    {
        require(chain.setParameters(initial)); require(chain.prepare(spec));
        require(chain.getLatencySamples() == 92);
    }
    juce::AudioBuffer<float> b(2, frames), fixture(2, frames);
    for (int c = 0; c < 2; ++c)
        for (unsigned n = 0; n < frames; ++n)
            fixture.setSample(c, int(n), float(.3 * std::sin(.013 * n) * (c ? -.73 : 1)));
    FullChainSettings s = initial;
    const unsigned ramp = unsigned(std::ceil(.01 * rate / frames)), dwell = ramp + 8;
    std::uint64_t iteration = 0;
    bool ok = true;
    double sink = 0.;
    const juce::ScopedNoDenormals noDenormals;
    auto callback = [&]() -> Durations
    {
        for (int c = 0; c < 2; ++c) b.copyFrom(c, 0, fixture, c, 0, frames);
        if (o.automation) profileAutomation(s, iteration, dwell);
        Durations durations {};
        auto block = juce::dsp::AudioBlock<float>(b);
        bool controls = false, processed = true;
        float peak = 0.f;
        if constexpr (mode == ProfileMode::full)
        {
            // Exact qualification boundary: no additional stage timers here.
            const auto start = Clock::now();
            controls = chain.setParameters(s); processed = chain.process(juce::dsp::AudioBlock<float>(b));
            const auto peaks = chain.getPeaks();
            const auto stop = Clock::now();
            durations[0] = ns(stop - start); peak = peaks.output;
        }
        else if constexpr (mode == ProfileMode::controls)
        {
            const auto start = Clock::now();
            controls = chain.setParameters(s);
            const auto stop = Clock::now();
            durations[0] = ns(stop - start);
            // Advance real histories between setter observations, outside timing.
            processed = chain.process(block); peak = chain.getPeaks().output;
        }
        else
        {
            const auto a = Clock::now();
            controls = input.setParameters(s.input); controls &= middle.setParameters(s.middle);
            controls &= output.setParameters(s.output); controls &= gain.setParameters(s.finalGain);
            const auto b0 = Clock::now();
            processed = input.process(block);
            const auto c = Clock::now();
            processed &= middle.process(block);
            const auto d = Clock::now();
            processed &= output.process(block);
            const auto e = Clock::now();
            processed &= gain.process(block);
            const auto f = Clock::now();
            durations = {ns(f-a), ns(b0-a), ns(c-b0), ns(d-c), ns(e-d), ns(f-e)};
            for (int channel = 0; channel < 2; ++channel)
                for (unsigned n = 0; n < frames; ++n) peak = std::max(peak, std::abs(b.getSample(channel, int(n))));
        }
        ok &= controls && processed && std::isfinite(peak);
        for (int c = 0; c < 2; ++c)
            for (unsigned n = 0; n < frames; ++n) ok &= std::isfinite(b.getSample(c, int(n)));
        sink += peak;
        ++iteration;
        return durations;
    };
    auto runUntimed = [&](double seconds)
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);
        while (Clock::now() < end) callback();
    };
    // Soak is per case: every rate/round starts with the declared preconditioning.
    runUntimed(o.soak); runUntimed(2.);
    const auto probeStart = iteration;
    runUntimed(2.);
    const auto probeCallbacks = iteration - probeStart;
    struct Observation { long long wall; std::uint64_t iteration; Durations durations; };
    std::vector<Observation> observations; observations.reserve(150000);
    const auto window = static_cast<long long>(o.seconds * 1e9);
    disdorktion::benchmark::ExpandedWallTimeSampler sampler(window);
    const auto firstIteration = iteration;
    const auto countersBefore = threadCounters();
    const auto begin = Clock::now();
    while (true)
    {
        const auto elapsed = ns(Clock::now() - begin);
        if (elapsed >= window) break;
        const bool sample = sampler.shouldSample(elapsed);
        const auto index = iteration;
        const auto durations = callback();
        if (sample) observations.push_back({elapsed, index, durations});
    }
    const auto finish = Clock::now();
    const auto countersAfter = threadCounters();
    const auto after = combmeasure::observePlatform(true, affinity);
    const auto schedulingAfter = schedulingSnapshot();
    const auto rawName = "case-" + std::to_string(id) + ".csv";
    std::ofstream raw(o.output / rawName);
    raw << "wall_elapsed_ns,iteration,total_ns,controls_ns,input_ns,middle_ns,output_ns,gain_ns\n";
    for (const auto& observation : observations)
    {
        raw << observation.wall << ',' << observation.iteration;
        for (const auto duration : observation.durations) raw << ',' << duration;
        raw << '\n';
    }
    raw.close(); require(!raw.fail()); require(!observations.empty());
    auto row = object();
    put(row, "id", int(id)); put(row, "round", int(round)); put(row, "rate", rate);
    put(row, "frames", int(frames)); put(row, "channels", 2); put(row, "mode", o.mode);
    put(row, "initial_settings", controlSnapshot(initial)); put(row, "latency_samples", 92);
    put(row, "reference_qualified", juce::var(false)); put(row, "processing_ok", ok);
    put(row, "warmup_seconds", 2.); put(row, "probe_seconds", 2.); put(row, "soak_seconds", o.soak);
    put(row, "probe_callbacks", juce::int64(probeCallbacks)); put(row, "window_seconds", o.seconds);
    put(row, "actual_window_ns", juce::int64(ns(finish-begin)));
    put(row, "session_begin_ns", juce::int64(ns(begin-sessionStart)));
    put(row, "loop_iterations", juce::int64(iteration-firstIteration)); put(row, "all_iterations", juce::int64(iteration));
    put(row, "thread_counters_before", counterSnapshot(countersBefore)); put(row, "thread_counters_after", counterSnapshot(countersAfter));
    if (countersBefore.cpuAvailable && countersAfter.cpuAvailable)
    {
        put(row, "thread_kernel_100ns", juce::int64(countersAfter.kernel100ns-countersBefore.kernel100ns));
        put(row, "thread_user_100ns", juce::int64(countersAfter.user100ns-countersBefore.user100ns));
    }
    if (countersBefore.cyclesAvailable && countersAfter.cyclesAvailable)
        put(row, "thread_cycles", juce::int64(countersAfter.cycles-countersBefore.cycles));
    put(row, "ac_before", before.onAC); put(row, "ac_after", after.onAC);
    put(row, "scheduling_before", schedulingBefore); put(row, "scheduling_after", schedulingAfter);
    put(row, "power_before", before.power); put(row, "power_after", after.power);
    put(row, "power_stable", before.onAC && after.onAC && before.power == after.power && before.power != "unavailable");
    put(row, "sink", sink); put(row, "raw_file", rawName); put(row, "raw_sha256", hash(o.output/rawName));
    const double deadline = frames / rate * 1e9;
    put(row, "deadline_ns", deadline); put(row, "budget_ns", deadline * .25);
    auto stats = object();
    for (std::size_t c = 0; c < (mode == ProfileMode::sections ? columns.size() : 1u); ++c)
    {
        std::vector<long long> values; values.reserve(observations.size());
        for (const auto& observation : observations) values.push_back(observation.durations[c]);
        put(stats, columns[c], statistics(std::move(values), deadline * .25));
    }
    put(row, "statistics", stats);
    json(o.output / ("case-" + std::to_string(id) + ".json"), row);
    std::cout << "round=" << round << " rate=" << rate << " mode=" << o.mode
              << " count=" << observations.size() << " p99_ns=" << stats["total"]["p99_ns"].toString()
              << " processing_ok=" << ok << std::endl;
    return row;
}

template<ProfileMode mode>
int profile(const Options& o, const FullChainSettings& initial, const char* executable)
{
    std::filesystem::create_directories(o.output);
    const auto schedulingOriginal = schedulingSnapshot();
    combmeasure::ThreadAffinity affinity;
    const auto platform = combmeasure::observePlatform(false, affinity);
    CoreOverride coreOverride(o.coreMask, platform.pinned);
    ProcessQos qos(o.highQos);
    const auto schedulingSelected = schedulingSnapshot();
    const auto identity = provenance(executable);
    const auto start = Clock::now();
    auto report = object();
    put(report, "kind", "expanded-chain-profile"); put(report, "reference_qualified", juce::var(false));
    put(report, "complete", juce::var(false)); put(report, "mode", o.mode); put(report, "conditions", o.conditions);
    const auto startedAt = juce::Time::getCurrentTime();
    put(report, "started_at", startedAt.toISO8601(true).toStdString());
    put(report, "started_unix_ms", juce::int64(startedAt.toMilliseconds()));
    put(report, "provenance", identity); put(report, "settings_sha256", hash(o.settingsFile));
    put(report, "settings_file", std::filesystem::absolute(o.settingsFile).string());
    put(report, "initial_settings", controlSnapshot(initial));
    put(report, "rounds", int(o.rounds)); put(report, "reverse", o.reverse);
    put(report, "active", o.active); put(report, "automation", o.automation);
    put(report, "high_qos_requested", o.highQos);
    put(report, "core_mask_requested", o.coreMask == 0 ? std::string("automatic") : maskText(o.coreMask));
    put(report, "scheduling_original", schedulingOriginal);
    put(report, "scheduling_selected", schedulingSelected);
    put(report, "selected_thread_group_affinity", schedulingSelected["thread_group_affinity"]);
    put(report, "high_qos", qos.metadata());
    put(report, "pinned", platform.pinned); put(report, "hybrid_established", platform.hybridEstablished);
    put(report, "affinity", o.coreMask == 0 ? platform.affinity : "diagnostic_override_group=0 selected_mask=" + maskText(o.coreMask));
    put(report, "automatic_affinity", platform.affinity); put(report, "topology", platform.topology);
    put(report, "on_ac", platform.onAC); put(report, "power", platform.power); put(report, "sdk", platform.sdk);
    put(report, "thermal", "sensor unavailable; ambient unconfirmed");
    put(report, "timer_overhead_subtracted", juce::var(false));
    put(report, "sampling", "150000 equal-wall-time slots; missed slots skipped; no outliers removed");
    put(report, "counter_scope", "complete sampled loop including fixtures, finite checks, sampling and bookkeeping; cycles are never converted to time");
    put(report, "section_limitations", "real serial Input/Middle4-FIR-ADAA/Output/Gain; includes extra timers; omits FullChain dry ring, mix, outer dispatch and outer peak scans; component percentiles are not additive");
    put(report, "controls_limitations", "FullChain setter timed alone; real processing and peak acquisition advance state outside timing");
    json(o.output/"session.json", report);
    juce::Array<juce::var> rows;
    unsigned id = 0;
    for (unsigned round = 0; round < o.rounds; ++round)
        for (double rate : (o.reverse ? std::array<double, 2>{96000., 48000.} : std::array<double, 2>{48000., 96000.}))
        {
            rows.add(collectCase<mode>(o, initial, rate, round, id++, affinity, start));
            put(report, "cases", juce::var(rows)); json(o.output/"result.json", report);
        }
    put(report, "scheduling_after_collection", schedulingSnapshot());
    qos.restore();
    put(report, "high_qos", qos.metadata());
    put(report, "scheduling_after_qos_restore", schedulingSnapshot());
    put(report, "complete", true); json(o.output/"result.json", report);
    // A successful diagnostic is still explicitly unqualified, as in the other tools.
    return 1;
}
}

int main(int argc, char** argv)
{
    try
    {
        const auto options = parseOptions(argc, argv);
        auto initial = loadSettings(options.settingsFile);
        if (options.active || options.automation)
            initial.output.comb.bypass = initial.output.dispersion.bypass = initial.output.tone.bypass = false;
        // Mode dispatch occurs once, outside every warmup/probe/sampling loop.
        if (options.mode == "full") return profile<ProfileMode::full>(options, initial, argv[0]);
        if (options.mode == "controls-only") return profile<ProfileMode::controls>(options, initial, argv[0]);
        return profile<ProfileMode::sections>(options, initial, argv[0]);
    }
    catch (const std::exception& e)
    {
        std::cerr << "expanded-chain profile: " << e.what() << '\n';
        return 2;
    }
}
