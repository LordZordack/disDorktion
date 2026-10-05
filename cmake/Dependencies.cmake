include(FetchContent)

set(DISDORKTION_JUCE_RELEASE "8.0.14" CACHE INTERNAL "Pinned JUCE release")
set(DISDORKTION_JUCE_REVISION "2cdfca8feb300fb424002ba2c2751569e5bacb64" CACHE INTERNAL "Pinned JUCE commit")
set(DISDORKTION_CATCH2_RELEASE "3.15.1" CACHE INTERNAL "Pinned Catch2 release")
set(DISDORKTION_CATCH2_REVISION "bcfb10e498df3e2ed8f814b3e4b689b9a85608ab" CACHE INTERNAL "Pinned Catch2 commit")

set(DISDORKTION_JUCE_SOURCE_DIR "" CACHE PATH "Use an existing local JUCE source checkout")

function(disdorktion_fetch_dependencies)
    if(DISDORKTION_JUCE_SOURCE_DIR)
        if(NOT EXISTS "${DISDORKTION_JUCE_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "DISDORKTION_JUCE_SOURCE_DIR must point to a JUCE source root")
        endif()
        add_subdirectory("${DISDORKTION_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
    else()
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG ${DISDORKTION_JUCE_REVISION}
            GIT_PROGRESS TRUE
        )
        FetchContent_MakeAvailable(juce)
    endif()

    if(BUILD_TESTING)
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG ${DISDORKTION_CATCH2_REVISION}
            GIT_PROGRESS TRUE
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
        set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
    endif()
endfunction()
