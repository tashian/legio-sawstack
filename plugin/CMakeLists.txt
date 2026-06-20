cmake_minimum_required(VERSION 3.22)
project(Sawstack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

add_subdirectory(JUCE)

set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/src)

juce_add_plugin(Sawstack
    PRODUCT_NAME "Sawstack"
    COMPANY_NAME "Tashian"
    BUNDLE_ID com.tashian.sawstack
    PLUGIN_MANUFACTURER_CODE Tash
    PLUGIN_CODE Saws
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Sawstack PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/note_stack.cpp
    ${DSP_DIR}/supersaw_engine.cpp
    ${DSP_DIR}/voice.cpp
    ${DSP_DIR}/pitch.cpp
    ${DSP_DIR}/stereo_vca.cpp)

target_include_directories(Sawstack PRIVATE src ${DSP_DIR})

target_compile_definitions(Sawstack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Sawstack PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
