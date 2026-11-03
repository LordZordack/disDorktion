#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <powrprof.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace combmeasure {
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

}
