#pragma once
#include "MeterModule.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>

class SpectrumMeter : public MeterModule
{
public:
    explicit SpectrumMeter(AudioEngine& engine);
    ~SpectrumMeter() override = default;

    void pushSamples(const float* srcL, const float* srcR, int numSamples) override;

    void paint(juce::Graphics& g) override;
    void resized() override {}

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    enum class ViewMode { FFT, ColorBars, Both };
    ViewMode viewMode { ViewMode::Both };

    enum class ChannelMode { LeftRight, MidSide };
    ChannelMode channelMode { ChannelMode::LeftRight };

    enum class ColorMode { Inferno, Ice };
    ColorMode colorMode { ColorMode::Ice };

    enum class ScaleMode { Linear, Logarithmic, Mel };
    ScaleMode scaleMode { ScaleMode::Logarithmic };

    int currentFftOrder { 12 }; // 4096 points default
    float smoothing { 0.8f };   // 0.0 to 0.99

    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    void recreateFFT(int order);

    // Audio buffers
    std::vector<float> fifo1, fifo2;
    int fifoIndex { 0 };
    bool nextBlockReady { false };
    
    std::vector<float> fftData1, fftData2;

    // Visual buffers
    static constexpr int scopeSize = 1024;
    std::vector<float> scopeData1, scopeData2;

    // Peak overlay info
    float peakDb { -100.0f };
    float peakFreq { 0.0f };

    void processFFT();
    juce::String getNoteString(float freq);
    juce::Colour getHeatmapColor(float level);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumMeter)
};
