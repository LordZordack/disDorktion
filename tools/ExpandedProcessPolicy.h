#pragma once
#include "CombBenchmarkPlatform.h"
#include "FullChainQualification.h"
#include <sstream>

namespace disdorktion::processpolicy
{
using qualification::object;
using qualification::put;

template<class State>
inline juce::var flags(const State& state, bool available, unsigned long error)
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

inline juce::var observe()
{
    auto result = object(), unavailable = object();
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
    put(result, "process_power_throttling", flags(process, haveProcess, processError));
#endif
#if defined(THREAD_POWER_THROTTLING_CURRENT_VERSION)
    THREAD_POWER_THROTTLING_STATE thread {};
    thread.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    const bool haveThread = GetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &thread, sizeof(thread)) != 0;
    const DWORD threadError = haveThread ? ERROR_SUCCESS : GetLastError();
    put(result, "thread_power_throttling", flags(thread, haveThread, threadError));
#endif
#endif
    put(result, "interpretation", "explicit policy flags only; inferred QoS, effective frequency and actual throttling are not measured");
    return result;
}

inline bool observedWithinAffinity(const juce::var& snapshot)
{
    const auto group = snapshot["thread_group_affinity"];
    if (!static_cast<bool>(group["available"])) return false;
    const int processor = static_cast<int>(group["observed_processor_number"]);
    std::uint64_t mask = 0;
    std::istringstream input(group["mask"].toString().toStdString());
    input >> std::hex >> mask;
    return bool(input) && input.eof() && processor >= 0 && processor < 64
        && static_cast<int>(group["observed_processor_group"]) == static_cast<int>(group["group"])
        && (mask & (std::uint64_t{1} << processor)) != 0;
}

inline bool sameAffinity(const juce::var& a, const juce::var& b)
{
    const auto x = a["thread_group_affinity"], y = b["thread_group_affinity"];
    return observedWithinAffinity(a) && observedWithinAffinity(b)
        && static_cast<int>(x["group"]) == static_cast<int>(y["group"])
        && x["mask"].toString() == y["mask"].toString();
}

inline bool selectedAffinityMatches(const juce::var& snapshot, const combmeasure::Platform& platform)
{
    if (!platform.pinned || !observedWithinAffinity(snapshot)) return false;
    const auto group = snapshot["thread_group_affinity"];
    const auto prefix = "selected_group=" + std::to_string(static_cast<int>(group["group"]))
        + " selected_mask=" + group["mask"].toString().toStdString() + " ";
    return platform.affinity.rfind(prefix, 0) == 0;
}

inline bool sameProcessPolicy(const juce::var& a, const juce::var& b)
{
    const auto x = a["process_power_throttling"], y = b["process_power_throttling"];
    return static_cast<bool>(x["available"]) && static_cast<bool>(y["available"])
        && static_cast<juce::int64>(x["version"]) == static_cast<juce::int64>(y["version"])
        && static_cast<juce::int64>(x["control_mask"]) == static_cast<juce::int64>(y["control_mask"])
        && static_cast<juce::int64>(x["state_mask"]) == static_cast<juce::int64>(y["state_mask"]);
}

// Own-process policy only. The existing affinity helper remains the only code
// that changes thread affinity. No priority, global power or host changes.
class ScopedProcessPolicy
{
public:
    explicit ScopedProcessPolicy(bool request) : requested(request), before(observe())
    {
        if (requested) setError = 50; // ERROR_NOT_SUPPORTED in an unsupported build.
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
        if (requested)
        {
            saved.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            const bool captured = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &saved, sizeof(saved)) != 0;
            const DWORD captureError = captured ? ERROR_SUCCESS : GetLastError();
            // This is the exact state restored later, rather than an earlier read.
            put(before, "process_power_throttling", flags(saved, captured, captureError));
            if (captured)
            {
                auto desired = saved;
                desired.ControlMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                desired.StateMask &= ~PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                attempted = true;
                changed = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &desired, sizeof(desired)) != 0;
                succeeded = changed;
                setError = succeeded ? ERROR_SUCCESS : GetLastError();
            }
            else setError = captureError;
        }
#endif
        try { afterRequest = observe(); }
        catch (...) { restoreNative(); throw; }
    }
    ScopedProcessPolicy(const ScopedProcessPolicy&) = delete;
    ScopedProcessPolicy& operator=(const ScopedProcessPolicy&) = delete;
    ~ScopedProcessPolicy() { restoreNative(); }

    bool applicationOk() const
    {
        return !requested || (attempted && succeeded && matchesRequested(afterRequest) && sameAffinity(before, afterRequest));
    }
    bool caseOk(const juce::var& first, const juce::var& last) const
    {
        return !requested || (applicationOk() && matchesRequested(first) && matchesRequested(last)
            && sameAffinity(afterRequest, first) && sameAffinity(afterRequest, last));
    }
    void restore()
    {
        restoreNative();
        afterRestore = observe();
    }
    bool restorationOk() const
    {
        return !requested || (restoreAttempted && restoreSucceeded && sameProcessPolicy(before, afterRestore)
            && sameAffinity(before, afterRestore));
    }
    const juce::var& beforeRequest() const { return before; }
    const juce::var& appliedSnapshot() const { return afterRequest; }
    const juce::var& restoredSnapshot() const { return afterRestore; }
    juce::var metadata() const
    {
        auto result = object();
        put(result, "requested", requested); put(result, "set_attempted", attempted); put(result, "set_succeeded", succeeded);
        put(result, "set_error", juce::int64(setError)); put(result, "before_request", before); put(result, "after_request", afterRequest);
        put(result, "application_ok", applicationOk());
        put(result, "restore_attempted", restoreAttempted); put(result, "restore_succeeded", restoreSucceeded);
        put(result, "restore_error", juce::int64(restoreError)); put(result, "after_restore", afterRestore);
        put(result, "restoration_ok", restorationOk());
        put(result, "scope", "own process EXECUTION_SPEED only; preserve unrelated policy bits; restore captured policy");
        return result;
    }
private:
    bool matchesRequested(const juce::var& snapshot) const
    {
        const auto prior = before["process_power_throttling"], actual = snapshot["process_power_throttling"];
        // Windows EXECUTION_SPEED is bit 0. Flags represent explicit policy,
        // not an observation of frequency or the system's inferred thread QoS.
        constexpr juce::int64 executionSpeed = 1;
        return static_cast<bool>(prior["available"]) && static_cast<bool>(actual["available"])
            && static_cast<juce::int64>(prior["version"]) == static_cast<juce::int64>(actual["version"])
            && static_cast<juce::int64>(actual["control_mask"]) == (static_cast<juce::int64>(prior["control_mask"]) | executionSpeed)
            && static_cast<juce::int64>(actual["state_mask"]) == (static_cast<juce::int64>(prior["state_mask"]) & ~executionSpeed);
    }
    void restoreNative() noexcept
    {
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
        if (!changed) return;
        restoreAttempted = true;
        restoreSucceeded = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &saved, sizeof(saved)) != 0;
        restoreError = restoreSucceeded ? ERROR_SUCCESS : GetLastError();
        if (restoreSucceeded) changed = false;
#endif
    }
    bool requested = false, attempted = false, succeeded = false, changed = false;
    bool restoreAttempted = false, restoreSucceeded = false;
    unsigned long setError = 0, restoreError = 0;
    juce::var before, afterRequest, afterRestore;
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
    PROCESS_POWER_THROTTLING_STATE saved {};
#endif
};
}
