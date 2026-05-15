#include "WaveformMeter.h"
#include <cmath>

WaveformMeter::WaveformMeter(AudioEngine& engine, float pxPerSec)
    : MeterModule(engine), pixelsPerSecond(pxPerSec)
{
    float sr = audioEngine.getSampleRate();
    if (sr <= 0) sr = 48000.f;
    mbColorL.update(sr);
    mbColorR.update(sr);
}

void WaveformMeter::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        
        juce::PopupMenu chMenu;
        chMenu.addItem(10, "Left / Right", true, channelMode == ChannelMode::LeftRight);
        chMenu.addItem(11, "Mid / Side",   true, channelMode == ChannelMode::MidSide);
        chMenu.addItem(12, "Left Only",    true, channelMode == ChannelMode::LeftOnly);
        chMenu.addItem(13, "Right Only",   true, channelMode == ChannelMode::RightOnly);
        chMenu.addItem(14, "Mid Only",     true, channelMode == ChannelMode::MidOnly);
        chMenu.addItem(15, "Side Only",    true, channelMode == ChannelMode::SideOnly);
        m.addSubMenu("Channels", chMenu);
        
        juce::PopupMenu colMenu;
        colMenu.addItem(20, "Static",     true, colorMode == ColorMode::Static);
        colMenu.addItem(21, "Multi-Band", true, colorMode == ColorMode::MultiBand);
        colMenu.addItem(22, "Color Map",  true, colorMode == ColorMode::ColorMap);
        m.addSubMenu("Color Mode", colMenu);
        
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result) {
                if (result >= 10 && result <= 15) channelMode = static_cast<ChannelMode>(result - 10);
                if (result == 20) colorMode = ColorMode::Static;
                if (result == 21) colorMode = ColorMode::MultiBand;
                if (result == 22) colorMode = ColorMode::ColorMap;
                repaint();
            });
    }
}

void WaveformMeter::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e);
    pixelsPerSecond *= (1.0f + wheel.deltaY * 0.5f);
    pixelsPerSecond = juce::jlimit(20.0f, 2000.0f, pixelsPerSecond);
}

void WaveformMeter::pushSamples(const float* srcL, const float* srcR, int numSamples)
{
    float sr = audioEngine.getSampleRate();
    if (sr <= 0) sr = 48000.f;
    
    int neededSize = (int)sr * kHistorySeconds;
    if (historySize != neededSize) {
        historySize = neededSize;
        histL.assign(historySize, 0.f);
        histR.assign(historySize, 0.f);
        colorL.assign(historySize, 0xFF000000);
        colorR.assign(historySize, 0xFF000000);
        writePos = 0;
        mbColorL.update(sr);
        mbColorR.update(sr);
    }
    
    if (historySize == 0) return;

    for (int i = 0; i < numSamples; ++i) {
        histL[writePos] = srcL[i];
        histR[writePos] = srcR[i];
        
        colorL[writePos] = mbColorL.process(srcL[i]);
        colorR[writePos] = mbColorR.process(srcR[i]);
        
        writePos++;
        if (writePos >= historySize) writePos = 0;
    }
}

void WaveformMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff000000)); // Pitch black background matches MiniMeters perfectly

    if (historySize == 0) return;

    int w = getWidth();
    int h = getHeight();
    
    float sr = audioEngine.getSampleRate();
    if (sr <= 0) sr = 48000.f;
    
    float samplesPerPixel = sr / pixelsPerSecond;
    int visibleSamples = (int)(w * samplesPerPixel);
    
    if (visibleSamples > historySize) visibleSamples = historySize;

    // Track configuration based on ChannelMode
    bool dualTrack = (channelMode == ChannelMode::LeftRight || channelMode == ChannelMode::MidSide);
    float trackH = dualTrack ? ((float)h * 0.5f) : (float)h;
    float maxAmpHeight = trackH * 0.95f * 0.5f;

    float center1 = trackH * 0.5f;
    float center2 = trackH + trackH * 0.5f;

    g.setColour(juce::Colour(0x33ffffff));
    g.drawHorizontalLine((int)center1, 0.f, (float)w);
    if (dualTrack) g.drawHorizontalLine((int)center2, 0.f, (float)w);

    int currentWritePos = writePos; 

    // Helper to generate dynamic heat/color map based on amplitude
    auto getMapColor = [](float amp) -> juce::Colour {
        amp = juce::jlimit(0.0f, 1.0f, amp * 1.5f); // Boost visual heat
        if (amp < 0.5f) return juce::Colour(0xff00ccff).interpolatedWith(juce::Colour(0xffccff00), amp * 2.0f);
        else return juce::Colour(0xffccff00).interpolatedWith(juce::Colour(0xffff0055), (amp - 0.5f) * 2.0f);
    };

    for (int x = 0; x < w; ++x)
    {
        int pixelsFromRight = (w - 1) - x;
        int endOffset = (int)(pixelsFromRight * samplesPerPixel);
        int startOffset = (int)((pixelsFromRight + 1) * samplesPerPixel);
        if (startOffset > historySize) break;
        
        float min1 = 0, max1 = 0, min2 = 0, max2 = 0;
        
        int sampleIndex = currentWritePos - endOffset - 1;
        while (sampleIndex < 0) sampleIndex += historySize;
        
        uint32_t mbColL = colorL[sampleIndex];
        uint32_t mbColR = colorR[sampleIndex];

        int numSamplesInBlock = startOffset - endOffset;
        for (int i = 0; i < numSamplesInBlock; ++i)
        {
            int idx = currentWritePos - endOffset - 1 - i;
            while (idx < 0) idx += historySize;
            
            float sL = histL[idx];
            float sR = histR[idx];
            
            float v1 = 0, v2 = 0;
            
            if (channelMode == ChannelMode::LeftRight) {
                v1 = sL; v2 = sR;
            } else if (channelMode == ChannelMode::MidSide) {
                v1 = (sL + sR) * 0.5f; v2 = (sL - sR) * 0.5f;
            } else if (channelMode == ChannelMode::LeftOnly) {
                v1 = sL;
            } else if (channelMode == ChannelMode::RightOnly) {
                v1 = sR;
            } else if (channelMode == ChannelMode::MidOnly) {
                v1 = (sL + sR) * 0.5f;
            } else if (channelMode == ChannelMode::SideOnly) {
                v1 = (sL - sR) * 0.5f;
            }
            
            if (v1 < min1) min1 = v1;
            if (v1 > max1) max1 = v1;
            
            if (v2 < min2) min2 = v2;
            if (v2 > max2) max2 = v2;
        }

        juce::Colour c1, c2;
        if (colorMode == ColorMode::Static) {
            c1 = juce::Colour(0xffff3366); // Neon pink
            c2 = juce::Colour(0xff3399ff); // Neon blue
        } else if (colorMode == ColorMode::ColorMap) {
            c1 = getMapColor(std::max(std::abs(min1), std::abs(max1)));
            c2 = getMapColor(std::max(std::abs(min2), std::abs(max2)));
        } else {
            // MultiBand
            if (channelMode == ChannelMode::LeftRight || channelMode == ChannelMode::LeftOnly) {
                c1 = juce::Colour(mbColL); c2 = juce::Colour(mbColR);
            } else if (channelMode == ChannelMode::RightOnly) {
                c1 = juce::Colour(mbColR);
            } else {
                // Approximate for M/S by blending colors
                c1 = juce::Colour(mbColL).interpolatedWith(juce::Colour(mbColR), 0.5f);
                c2 = c1;
            }
        }

        g.setColour(c1);
        g.drawVerticalLine(x, center1 - max1 * maxAmpHeight, center1 - min1 * maxAmpHeight);

        if (dualTrack) {
            g.setColour(c2);
            g.drawVerticalLine(x, center2 - max2 * maxAmpHeight, center2 - min2 * maxAmpHeight);
        }
    }
}
