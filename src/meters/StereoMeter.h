#pragma once
#include "MeterModule.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

class StereoMeter : public MeterModule
{
public:
    explicit StereoMeter(AudioEngine& engine);
    ~StereoMeter() override = default;

    void pushSamples(const float* srcL, const float* srcR, int numSamples) override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    enum class DisplayMode { Scaled, Linear, Lissajous };
    DisplayMode displayMode { DisplayMode::Linear };

    enum class ColorMode { Static, RGB, MultiBand };
    ColorMode colorMode { ColorMode::RGB };

    enum class CorrMode { SingleBand, MultiBand };
    CorrMode corrMode { CorrMode::MultiBand };

    struct PointData { 
        float x, y; 
        juce::Colour c; 
    };
    std::vector<PointData> points;
    int pointIndex { 0 };
    static constexpr int maxPoints = 2048;

    // Crossover filters
    juce::dsp::LinkwitzRileyFilter<float> lp1L, lp1R;
    juce::dsp::LinkwitzRileyFilter<float> hp1L, hp1R;
    juce::dsp::LinkwitzRileyFilter<float> lp2L, lp2R;
    juce::dsp::LinkwitzRileyFilter<float> hp2L, hp2R;

    // Correlation state
    float corrOverall { 0.f }, corrLow { 0.f }, corrMid { 0.f }, corrHigh { 0.f };
    float smoothPowerOverallL { 0.f }, smoothPowerOverallR { 0.f }, smoothCrossOverall { 0.f };
    float smoothPowerLowL { 0.f }, smoothPowerLowR { 0.f }, smoothCrossLow { 0.f };
    float smoothPowerMidL { 0.f }, smoothPowerMidR { 0.f }, smoothCrossMid { 0.f };
    float smoothPowerHighL { 0.f }, smoothPowerHighR { 0.f }, smoothCrossHigh { 0.f };
    
    float calculateCorrelation(float l, float r, float& pL, float& pR, float& cross);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoMeter)
};
