cmake_minimum_required(VERSION 4.2)

project(DisDorktion VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(cmake/Dependencies.cmake)

option(DISDORKTION_BUILD_PLUGIN "Build the VST3 and Standalone plugin targets" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_WEB_BROWSER 0 CACHE BOOL "Disable JUCE web browser support" FORCE)
set(JUCE_USE_CURL 0 CACHE BOOL "Disable JUCE curl support" FORCE)
set(JUCE_VST3_CAN_REPLACE_VST2 0 CACHE BOOL "Disable VST2 replacement support" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Do not build JUCE extras" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Do not build JUCE examples" FORCE)

disdorktion_fetch_dependencies()

add_library(disdorktion_dsp STATIC
    src/dsp/ReferenceGain.cpp
)
target_include_directories(disdorktion_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(disdorktion_dsp PUBLIC cxx_std_20)
target_include_directories(disdorktion_dsp PUBLIC "${JUCE_MODULES_DIR}")
target_compile_definitions(disdorktion_dsp PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_MODULE_AVAILABLE_juce_core=1
    JUCE_MODULE_AVAILABLE_juce_dsp=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(disdorktion_dsp
    PUBLIC juce::juce_recommended_config_flags
    PRIVATE juce::juce_dsp juce::juce_core
)

if(BUILD_TESTING)
    include(Catch)

    add_executable(disdorktion_dsp_tests
        tests/ModuleContractTests.cpp
        tests/ReferenceGainTests.cpp
    )
    target_link_libraries(disdorktion_dsp_tests PRIVATE disdorktion_dsp Catch2::Catch2WithMain)
    catch_discover_tests(disdorktion_dsp_tests TEST_PREFIX "dsp::" PROPERTIES TIMEOUT 30)

    add_executable(disdorktion_realtime_tests
        tests/RealtimeSafetyTests.cpp
        tests/AllocationTracking.cpp
    )
    target_link_libraries(disdorktion_realtime_tests PRIVATE disdorktion_dsp Catch2::Catch2WithMain)
    catch_discover_tests(disdorktion_realtime_tests TEST_PREFIX "rt::" PROPERTIES TIMEOUT 30)

    if(DISDORKTION_BUILD_PLUGIN)
        add_executable(disdorktion_adapter_tests
            tests/PluginAdapterTests.cpp
            src/plugin/PluginProcessor.cpp
        )
        target_include_directories(disdorktion_adapter_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(disdorktion_adapter_tests PRIVATE disdorktion_dsp Catch2::Catch2WithMain juce::juce_audio_processors)
        target_compile_definitions(disdorktion_adapter_tests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
        catch_discover_tests(disdorktion_adapter_tests TEST_PREFIX "adapter::" PROPERTIES TIMEOUT 30)
    endif()
endif()

if(DISDORKTION_BUILD_PLUGIN)
    juce_add_plugin(DisDorktion
        VERSION 0.1.0
        COMPANY_NAME DisDorktion
        PLUGIN_MANUFACTURER_CODE Dsdk
        PLUGIN_CODE Dsdk
        FORMATS VST3 Standalone
        PRODUCT_NAME DisDorktion
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )
    target_sources(DisDorktion PRIVATE src/plugin/PluginProcessor.cpp)
    target_include_directories(DisDorktion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(DisDorktion PRIVATE disdorktion_dsp juce::juce_audio_utils)
    target_compile_definitions(DisDorktion PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
endif()

add_executable(disdorktion_reference_gain_benchmark
    benchmarks/ReferenceGainBenchmark.cpp
)
target_link_libraries(disdorktion_reference_gain_benchmark PRIVATE disdorktion_dsp)
if(WIN32)
    target_link_libraries(disdorktion_reference_gain_benchmark PRIVATE PowrProf)
    target_compile_definitions(disdorktion_reference_gain_benchmark PRIVATE
        DISDORKTION_WINDOWS_SDK_VERSION=\"${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\")
endif()
