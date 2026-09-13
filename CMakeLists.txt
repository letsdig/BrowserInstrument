cmake_minimum_required(VERSION 3.22)

project(BrowserInstrument VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check local JUCE installation or fallback to FetchContent
if(NOT TARGET juce::juce_core)
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" ${CMAKE_CURRENT_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("$ENV{JUCE_PATH}" ${CMAKE_CURRENT_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" ${CMAKE_CURRENT_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        find_package(JUCE CONFIG QUIET)
        if(NOT TARGET juce::juce_core)
            include(FetchContent)
            FetchContent_Declare(
                JUCE
                GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                GIT_TAG 8.0.4
            )
            FetchContent_MakeAvailable(JUCE)
        endif()
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBKIT_GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1 gtk+-3.0)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple libpulse)

juce_add_plugin(BrowserInstrument
    COMPANY_NAME "AudioWebLab"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES "Instrument" "Tools"
    PLUGIN_MANUFACTURER_CODE Awlb
    PLUGIN_CODE BrwI
    FORMATS VST3 Standalone
    PRODUCT_NAME "BrowserInstrument"
)

target_sources(BrowserInstrument
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
        Source/WebBridge/Sha1.h
        Source/WebBridge/BridgeScript.h
        Source/WebBridge/WebBridgeServer.h
        Source/InstrumentBrowserComponent.h
        Source/PulseCaptureThread.h
)

target_compile_definitions(BrowserInstrument
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(BrowserInstrument
    PRIVATE
        PkgConfig::WEBKIT_GTK
        PkgConfig::PULSE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
