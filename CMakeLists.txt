cmake_minimum_required(VERSION 3.22)

project(MagicalChords
    VERSION 1.0.0
    DESCRIPTION "Magical Chords - AI vocal-driven chord / MIDI generator VST3"
    LANGUAGES C CXX)

# C++17 is required (std::clamp, structured bindings are used by the analysis
# core). Set globally AND on the target so MSVC (default C++14) uses C++17 too.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# JUCE
# -----------------------------------------------------------------------------
if(NOT DEFINED JUCE_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()

if(NOT DEFINED JUCE_PATH)
    message(FATAL_ERROR
        "JUCE not found. Set -DJUCE_PATH=/path/to/JUCE or place the JUCE "
        "source tree at ${CMAKE_CURRENT_SOURCE_DIR}/JUCE.")
endif()

add_subdirectory("${JUCE_PATH}" juce EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# Plugin target
# -----------------------------------------------------------------------------
juce_add_plugin(MagicalChords
    COMPANY_NAME             "Ryan Zack"
    PLUGIN_MANUFACTURER_CODE RzZk
    PLUGIN_CODE              MgCh
    FORMATS                  VST3 Standalone
    PRODUCT_NAME             "Magical Chords"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        TRUE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_CATEGORIES          Instrument Effect)

target_compile_features(MagicalChords PRIVATE cxx_std_17)

target_sources(MagicalChords PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analysis/AudioAnalysisWorker.cpp
    Source/Analysis/PitchDetector.cpp
    Source/Harmony/ChordEngine.cpp
    Source/Arrangement/ArrangementEngine.cpp
    Source/MIDI/MidiExportEngine.cpp)

target_include_directories(MagicalChords PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_link_libraries(MagicalChords
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(MagicalChords PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_MIDI_EFFECT=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

juce_generate_juce_header(MagicalChords)
