cmake_minimum_required(VERSION 3.22)

project(EasyEfx VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_plugin(EasyEfx
    COMPANY_NAME "EasyEfx Audio"
    BUNDLE_ID "com.easyefx.easyefx"
    PLUGIN_MANUFACTURER_CODE Eefx
    PLUGIN_CODE Ef01
    FORMATS VST3 Standalone
    PRODUCT_NAME "EasyEfx"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

target_sources(EasyEfx PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/Effects.cpp
    Source/DSP/Effects.h
    Source/GUI/CustomKnob.h
    Source/GUI/EffectTile.cpp
    Source/GUI/EffectTile.h
    Source/GUI/SettingsOverlay.cpp
    Source/GUI/SettingsOverlay.h
    Source/GUI/GlobalPanel.h
)

target_compile_definitions(EasyEfx PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(EasyEfx PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
