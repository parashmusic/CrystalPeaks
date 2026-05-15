#pragma once
#include "MeterModule.h"
#include <vector>
#include <juce_dsp/juce_dsp.h>

class WaveformMeter : public MeterModule
{
public:
    explicit WaveformMeter(AudioEngine& engine, float pixelsPerSecond = 200.f);
    ~WaveformMeter() override = default;

    void pushSamples(const float* srcL, const float* srcR, int numSamples) override;

    void paint(juce::Graphics& g) override;
    void resized() override {}
    
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    float pixelsPerSecond;

    static constexpr int kHistorySeconds = 6;
    int historySize = 0;
    
    std::vector<float> histL;
    std::vector<float> histR;
    std::vector<uint32_t> colorL;
    std::vector<uint32_t> colorR;
    int writePos = 0;

    enum class ColorMode { Static, MultiBand, ColorMap };
    ColorMode colorMode { ColorMode::MultiBand };

    enum class ChannelMode { LeftRight, MidSide, LeftOnly, RightOnly, MidOnly, SideOnly };
    ChannelMode channelMode { ChannelMode::LeftRight };

    // Calculates real-time frequency-based colors for MultiBand mode
    struct MultiBandColor {
        float z1_L = 0, z1_H = 0;
        float lpCoef = 0.05f;
        float hpCoef = 0.2f;
        float envL = 0, envM = 0, envH = 0;
        
        void update(float sr) {
            if (sr < 1000) return;
            lpCoef = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 150.0f / sr);
            hpCoef = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 2500.0f / sr);
        }
        
        uint32_t process(float in) {
            z1_L += lpCoef * (in - z1_L);
            float low = z1_L;
            z1_H += hpCoef * (in - z1_H);
            float high = in - z1_H;
            float mid = in - low - high;
            
            auto env = [](float& state, float v) {
                v = std::abs(v);
                if (v > state) state += 0.02f * (v - state);
                else state += 0.0002f * (v - state);
            };
            env(envL, low);
            env(envM, mid);
            env(envH, high);
            
            float r = envL * 2.0f + envM * 0.4f;
            float g = envM * 1.5f + envL * 0.3f + envH * 0.3f;
            float b = envH * 2.0f + envM * 0.4f;
            
            float maxV = std::max({r, g, b, 0.001f});
            r /= maxV; g /= maxV; b /= maxV;
            
            float total = juce::jlimit(0.0f, 1.0f, (envL + envM + envH) * 4.0f);
            
            uint8_t R = (uint8_t)juce::jlimit(0.0f, 255.0f, std::pow(r, 0.8f) * total * 255.0f + 10.f);
            uint8_t G = (uint8_t)juce::jlimit(0.0f, 255.0f, std::pow(g, 0.8f) * total * 255.0f + 10.f);
            uint8_t B = (uint8_t)juce::jlimit(0.0f, 255.0f, std::pow(b, 0.8f) * total * 255.0f + 10.f);
            
            return 0xFF000000 | (R << 16) | (G << 8) | B;
        }
    };

    MultiBandColor mbColorL, mbColorR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformMeter)
};
