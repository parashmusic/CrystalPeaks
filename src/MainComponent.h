#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "meters/ModuleStrip.h"
#include "meters/WaveformMeter.h"
#include "meters/SpectrumMeter.h"
#include "meters/StereoMeter.h"

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AudioEngine audioEngine;
    ModuleStrip moduleStrip;

    std::unique_ptr<WaveformMeter> waveformMeter;
    std::unique_ptr<SpectrumMeter> spectrumMeter;
    std::unique_ptr<StereoMeter> stereoMeter;

    juce::TextButton modulesButton { "Modules" };
    juce::Label statusLabel;

    bool showWaveform { true };
    bool showSpectrum { true };
    bool showStereo { true };
    bool showPeakBars { true };
    bool stickMode { false };

    void updateModuleLayout();
    void updateStickMode();

    static constexpr int kScratchSize = 8192;
    float scratchL[kScratchSize] {};
    float scratchR[kScratchSize] {};

    std::atomic<float> peakL { 0.f }, peakR { 0.f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};