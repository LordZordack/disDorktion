# Building

## Requirements and pinned dependencies

- CMake 4.2 or newer. CI pins the official CMake 4.2.8 Windows x64 archive; its SHA-256 is `d33720a4e03114ae7c964c184e88f68a640be419f973bf4fb685f0900d1864ee` (also recorded in [`cmake/tools-lock.json`](../cmake/tools-lock.json)).
- Visual Studio 2026 with the x64 C++ workload for local builds.
- Visual Studio 2022 with the v143 toolset for CI.
- JUCE 8.0.14, commit `2cdfca8feb300fb424002ba2c2751569e5bacb64`.
- Catch2 3.15.1, commit `bcfb10e498df3e2ed8f814b3e4b689b9a85608ab`.

The checked-out dependency sources under `.tools/deps/` can serve as a local cache. A clean configure fetches pinned dependencies if they are not cached. JUCE configuration builds `juceaide` as part of its normal CMake setup.

JUCE modules are available under the [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) or the [commercial JUCE 8 license](https://juce.com/legal/juce-8-licence/); the pinned source's [LICENSE.md](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/LICENSE.md) documents the available routes. Catch2 is under the [Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt), as stated in its pinned [LICENSE.txt](https://github.com/catchorg/Catch2/blob/bcfb10e498df3e2ed8f814b3e4b689b9a85608ab/LICENSE.txt).

## Presets and commands

Configure a full plugin build, then build and run its tests by configuration:

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug --output-on-failure --no-tests=error
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release --output-on-failure --no-tests=error
```

For the Visual Studio 2022 / v143 toolchain used by CI, substitute `windows-vs2022` for `windows-vs2026` in each command. The DSP-only Release preset is available for both toolchains:

```powershell
cmake --preset windows-vs2026-dsp-only
cmake --build --preset windows-vs2026-dsp-only-release
ctest --preset windows-vs2026-dsp-only-release --output-on-failure --no-tests=error
```

Replace `vs2026` with `vs2022` to use the CI toolchain. DSP-only presets omit the plugin and adapter targets.

### Offline dependency sources

To configure without fetching JUCE, point CMake at a local JUCE source tree and provide a local Catch2 checkout at the pinned revision:

```powershell
cmake --preset windows-vs2026 `
  -DDISDORKTION_JUCE_SOURCE_DIR=C:/path/JUCE `
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/path/Catch2
git -C C:/path/JUCE rev-parse HEAD
git -C C:/path/Catch2 rev-parse HEAD
```

The reported revisions must match the pinned commits above. Archive extracts do not contain Git metadata, so `git rev-parse HEAD` cannot verify an archive; use a checkout when verifying the revision. The configure still needs JUCE's normal `juceaide` build tools to be available or buildable from the local JUCE tree.

If a failed configure leaves empty compiler flags in the build directory, reconfigure with `cmake --fresh --preset <configure-preset>` (and repeat any dependency override arguments). Do not change toolsets blindly; first confirm the intended Visual Studio installation and generator.

## Build outputs

For the full plugin presets, expected outputs include:

```text
build/<configure-preset>/{Debug,Release}/disdorktion_dsp_tests.exe
build/<configure-preset>/{Debug,Release}/disdorktion_realtime_tests.exe
build/<configure-preset>/{Debug,Release}/disdorktion_adapter_tests.exe
build/<configure-preset>/{Debug,Release}/disdorktion_reference_gain_benchmark.exe
build/<configure-preset>/DisDorktion_artefacts/{Debug,Release}/VST3/DisDorktion.vst3
build/<configure-preset>/DisDorktion_artefacts/{Debug,Release}/Standalone/DisDorktion.exe
```

The VST3 bundle may contain the plugin binary below its platform-specific `Contents` directory. DSP-only builds omit adapter tests and plugin artifacts.

## Observed configuration status

The local configure completed with Visual Studio Community 18.8.2, MSVC 19.51.36252.0 (tool path 14.51.36231), Windows SDK 10.0.26100.0, and CMake 4.3.1-msvc1. Full Debug and Release builds succeeded, with 16/16 CTest cases passing in each configuration. The independent DSP-only Release build succeeded with 10/10 cases passing. VST3 and Standalone binaries were checked in both configurations. See the [validation record](okf/notes/foundation_validation.md) for scope and limitations.

CI uses Visual Studio 2022 / v143 and pinned CMake 4.2.8. For architecture, processing requirements, and proposed performance budgets, see the [DSP contract](DSP_CONTRACT.md) and [performance procedure](PERFORMANCE.md).
