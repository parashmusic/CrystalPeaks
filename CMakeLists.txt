cmake_minimum_required(VERSION 3.22)
project(MiniMeters VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

# Point this at wherever you cloned JUCE (one level up from MiniMeters/)
add_subdirectory(../JUCE JUCE)

juce_add_gui_app(MiniMeters
    PRODUCT_NAME "CrystalPeaks"
    COMPANY_NAME "Spectrum"
    ICON_BIG resources/logo.png
    ICON_SMALL resources/logo.png
)

target_sources(MiniMeters PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/AudioEngine.cpp
    src/meters/WaveformMeter.cpp
    src/meters/ModuleStrip.cpp
    src/meters/SpectrumMeter.cpp
    src/meters/StereoMeter.cpp
)

target_compile_definitions(MiniMeters PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DSP_USE_INTEL_MKL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MiniMeters,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MiniMeters,JUCE_VERSION>"
)

target_link_libraries(MiniMeters PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)