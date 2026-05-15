#include "SpectrumMeter.h"
#include <cmath>

SpectrumMeter::SpectrumMeter(AudioEngine& engine) : MeterModule(engine)
{
    scopeData1.assign(scopeSize, 0.f);
    scopeData2.assign(scopeSize, 0.f);
    recreateFFT(currentFftOrder);
}

void SpectrumMeter::recreateFFT(int order)
{
    currentFftOrder = order;
    int size = 1 << order;
    
    fft = std::make_unique<juce::dsp::FFT>(order);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(size, juce::dsp::WindowingFunction<float>::hann);
    
    fifo1.assign(size, 0.f);
    fifo2.assign(size, 0.f);
    fftData1.assign(size * 2, 0.f);
    fftData2.assign(size * 2, 0.f);
    fifoIndex = 0;
    nextBlockReady = false;
}

void SpectrumMeter::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        
        juce::PopupMenu viewMenu;
        viewMenu.addItem(10, "FFT Line",   true, viewMode == ViewMode::FFT);
        viewMenu.addItem(11, "Color Bars", true, viewMode == ViewMode::ColorBars);
        viewMenu.addItem(12, "Both",       true, viewMode == ViewMode::Both);
        m.addSubMenu("Mode", viewMenu);
        
        juce::PopupMenu chMenu;
        chMenu.addItem(20, "Left / Right", true, channelMode == ChannelMode::LeftRight);
        chMenu.addItem(21, "Mid / Side",   true, channelMode == ChannelMode::MidSide);
        m.addSubMenu("Channels", chMenu);
        
        juce::PopupMenu colMenu;
        colMenu.addItem(80, "Inferno", true, colorMode == ColorMode::Inferno);
        colMenu.addItem(81, "Ice",     true, colorMode == ColorMode::Ice);
        m.addSubMenu("Color Palette", colMenu);
        
        juce::PopupMenu scaleMenu;
        scaleMenu.addItem(30, "Linear",      true, scaleMode == ScaleMode::Linear);
        scaleMenu.addItem(31, "Logarithmic", true, scaleMode == ScaleMode::Logarithmic);
        scaleMenu.addItem(32, "Mel",         true, scaleMode == ScaleMode::Mel);
        m.addSubMenu("FFT Scale", scaleMenu);
        
        juce::PopupMenu sizeMenu;
        sizeMenu.addItem(40, "1024",  true, currentFftOrder == 10);
        sizeMenu.addItem(41, "2048",  true, currentFftOrder == 11);
        sizeMenu.addItem(42, "4096",  true, currentFftOrder == 12);
        sizeMenu.addItem(43, "8192",  true, currentFftOrder == 13);
        sizeMenu.addItem(44, "16384", true, currentFftOrder == 14);
        m.addSubMenu("FFT Size", sizeMenu);

        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result) {
                if (result == 10) viewMode = ViewMode::FFT;
                if (result == 11) viewMode = ViewMode::ColorBars;
                if (result == 12) viewMode = ViewMode::Both;
                
                if (result == 20) channelMode = ChannelMode::LeftRight;
                if (result == 21) channelMode = ChannelMode::MidSide;
                
                if (result == 30) scaleMode = ScaleMode::Linear;
                if (result == 31) scaleMode = ScaleMode::Logarithmic;
                if (result == 32) scaleMode = ScaleMode::Mel;
                
                if (result == 80) colorMode = ColorMode::Inferno;
                if (result == 81) colorMode = ColorMode::Ice;
                
                if (result >= 40 && result <= 44) {
                    recreateFFT(result - 30); // 40-30=10 (1024)
                }
                
                repaint();
            });
    }
}

void SpectrumMeter::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e);
    // Adjust smoothing
    smoothing += wheel.deltaY * 0.1f;
    smoothing = juce::jlimit(0.0f, 0.99f, smoothing);
}

void SpectrumMeter::pushSamples(const float* srcL, const float* srcR, int numSamples)
{
    int size = 1 << currentFftOrder;
    for (int i = 0; i < numSamples; ++i)
    {
        float v1 = 0, v2 = 0;
        if (channelMode == ChannelMode::LeftRight) {
            v1 = srcL[i];
            v2 = srcR[i];
        } else {
            v1 = (srcL[i] + srcR[i]) * 0.5f; // Mid
            v2 = (srcL[i] - srcR[i]) * 0.5f; // Side
        }

        if (fifoIndex == size)
        {
            if (!nextBlockReady)
            {
                juce::FloatVectorOperations::copy(fftData1.data(), fifo1.data(), size);
                juce::FloatVectorOperations::copy(fftData2.data(), fifo2.data(), size);
                nextBlockReady = true;
            }
            fifoIndex = 0;
        }
        fifo1[fifoIndex] = v1;
        fifo2[fifoIndex] = v2;
        fifoIndex++;
    }
}

void SpectrumMeter::processFFT()
{
    int size = 1 << currentFftOrder;
    
    window->multiplyWithWindowingTable(fftData1.data(), size);
    window->multiplyWithWindowingTable(fftData2.data(), size);
    
    fft->performFrequencyOnlyForwardTransform(fftData1.data());
    fft->performFrequencyOnlyForwardTransform(fftData2.data());

    float sr = audioEngine.getSampleRate();
    if (sr <= 0) sr = 48000.f;

    const float minDb = -100.0f;
    const float maxDb = 0.0f;

    float localPeakDb = -100.f;
    float localPeakFreq = 0.f;

    auto freqToBin = [&](float freq) {
        return (freq * size) / sr;
    };
    
    auto mel = [](float f) { return 2595.0f * std::log10(1.0f + f / 700.0f); };
    auto invMel = [](float m) { return 700.0f * (std::pow(10.0f, m / 2595.0f) - 1.0f); };

    float minFreq = 20.0f;
    float maxFreq = sr * 0.5f;

    auto getFreqForNormX = [&](float normX) {
        if (scaleMode == ScaleMode::Linear) {
            return minFreq + normX * (maxFreq - minFreq);
        } else if (scaleMode == ScaleMode::Logarithmic) {
            return minFreq * std::pow(maxFreq / minFreq, normX);
        } else {
            float minM = mel(minFreq);
            float maxM = mel(maxFreq);
            float m = minM + normX * (maxM - minM);
            return invMel(m);
        }
    };

    for (int i = 0; i < scopeSize; ++i)
    {
        float normX = (float)i / (scopeSize - 1);
        float nextNormX = (float)(i + 1) / (scopeSize - 1);
        
        float freq = getFreqForNormX(normX);
        float nextFreq = (i < scopeSize - 1) ? getFreqForNormX(nextNormX) : maxFreq;

        float binStart = (freq * size) / sr;
        float binEnd = (nextFreq * size) / sr;
        
        float mag1 = 0.0f;
        float mag2 = 0.0f;
        
        if (binEnd - binStart <= 1.0f) {
            // Interpolate for smooth low frequencies
            int b0 = juce::jlimit(0, size / 2, (int)binStart);
            int b1 = juce::jlimit(0, size / 2, b0 + 1);
            float t = binStart - (float)b0;
            if (t < 0.0f) t = 0.0f;
            
            mag1 = fftData1[b0] * (1.0f - t) + fftData1[b1] * t;
            mag2 = fftData2[b0] * (1.0f - t) + fftData2[b1] * t;
        } else {
            // Peak picking for high frequencies to prevent aliasing noise
            int b0 = juce::jlimit(0, size / 2, (int)binStart);
            int b1 = juce::jlimit(0, size / 2, (int)binEnd);
            for (int b = b0; b <= b1; ++b) {
                if (fftData1[b] > mag1) mag1 = fftData1[b];
                if (fftData2[b] > mag2) mag2 = fftData2[b];
            }
        }

        mag1 = std::max(mag1, 1e-7f);
        mag2 = std::max(mag2, 1e-7f);

        float db1 = juce::Decibels::gainToDecibels(mag1) - juce::Decibels::gainToDecibels((float)size);
        float db2 = juce::Decibels::gainToDecibels(mag2) - juce::Decibels::gainToDecibels((float)size);

        // Find absolute peak
        if (db1 > localPeakDb) { localPeakDb = db1; localPeakFreq = freq; }
        if (db2 > localPeakDb) { localPeakDb = db2; localPeakFreq = freq; }

        float lvl1 = juce::jmap(db1, minDb, maxDb, 0.0f, 1.0f);
        float lvl2 = juce::jmap(db2, minDb, maxDb, 0.0f, 1.0f);
        
        lvl1 = juce::jlimit(0.0f, 1.0f, lvl1);
        lvl2 = juce::jlimit(0.0f, 1.0f, lvl2);

        scopeData1[i] = scopeData1[i] * smoothing + lvl1 * (1.0f - smoothing);
        scopeData2[i] = scopeData2[i] * smoothing + lvl2 * (1.0f - smoothing);
    }
    
    // Smooth the peak readout
    peakDb = peakDb * 0.95f + localPeakDb * 0.05f;
    peakFreq = peakFreq * 0.95f + localPeakFreq * 0.05f;
}

juce::String SpectrumMeter::getNoteString(float freq)
{
    if (freq < 10.0f) return "---";
    
    float noteFloat = 69.0f + 12.0f * std::log2(freq / 440.0f);
    int noteIndex = (int)std::floor(noteFloat + 0.5f);
    int cents = (int)std::floor((noteFloat - noteIndex) * 100.0f + 0.5f);
    
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    
    int octave = (noteIndex / 12) - 1;
    int nameIdx = noteIndex % 12;
    if (nameIdx < 0) nameIdx += 12;
    
    juce::String noteStr = juce::String(noteNames[nameIdx]) + juce::String(octave);
    
    if (cents > 0) noteStr += " + " + juce::String(cents) + " Cents";
    else if (cents < 0) noteStr += " - " + juce::String(std::abs(cents)) + " Cents";
    
    return noteStr;
}

juce::Colour SpectrumMeter::getHeatmapColor(float level)
{
    level = juce::jlimit(0.0f, 1.0f, level);
    
    if (colorMode == ColorMode::Ice) {
        juce::Colour c0(0xff001122);
        juce::Colour c1(0xff004488);
        juce::Colour c2(0xff0088ff);
        juce::Colour c3(0xff88ccff);
        juce::Colour c4(0xffffffff);
        
        if (level < 0.25f) return c0.interpolatedWith(c1, level / 0.25f);
        if (level < 0.50f) return c1.interpolatedWith(c2, (level - 0.25f) / 0.25f);
        if (level < 0.75f) return c2.interpolatedWith(c3, (level - 0.50f) / 0.25f);
        return c3.interpolatedWith(c4, (level - 0.75f) / 0.25f);
    }
    
    juce::Colour c0(0xff050011); // Dark bg
    juce::Colour c1(0xff550055); // Purple
    juce::Colour c2(0xffdd2244); // Pink/Red
    juce::Colour c3(0xffff8800); // Orange
    juce::Colour c4(0xffffeeaa); // Yellow/White
    
    if (level < 0.25f) return c0.interpolatedWith(c1, level / 0.25f);
    if (level < 0.50f) return c1.interpolatedWith(c2, (level - 0.25f) / 0.25f);
    if (level < 0.75f) return c2.interpolatedWith(c3, (level - 0.50f) / 0.25f);
    return c3.interpolatedWith(c4, (level - 0.75f) / 0.25f);
}

void SpectrumMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff000000));

    if (nextBlockReady)
    {
        processFFT();
        nextBlockReady = false;
    }

    float w = (float)getWidth();
    float h = (float)getHeight();
    
    // Draw grid
    g.setColour(juce::Colour(0x22ffffff));
    float freqs[] = { 100.f, 1000.f, 10000.f };
    
    auto freqToNormX = [&](float freq) {
        float minFreq = 20.0f;
        float maxFreq = audioEngine.getSampleRate() * 0.5f;
        if (maxFreq <= minFreq) maxFreq = 24000.f;
        
        if (scaleMode == ScaleMode::Linear) {
            return (freq - minFreq) / (maxFreq - minFreq);
        } else if (scaleMode == ScaleMode::Logarithmic) {
            return std::log(freq / minFreq) / std::log(maxFreq / minFreq);
        } else {
            auto mel = [](float f) { return 2595.0f * std::log10(1.0f + f / 700.0f); };
            return (mel(freq) - mel(minFreq)) / (mel(maxFreq) - mel(minFreq));
        }
    };
    
    for (float f : freqs) {
        float normX = freqToNormX(f);
        if (normX >= 0.f && normX <= 1.f) {
            float x = normX * w;
            g.drawVerticalLine((int)x, 0.f, h);
            g.setColour(juce::Colour(0x66ffffff));
            g.setFont(10.f);
            juce::String label = f >= 1000.f ? juce::String((int)(f/1000.f)) + "kHz" : juce::String((int)f) + "Hz";
            g.drawText(label, (int)x + 5, 5, 50, 15, juce::Justification::topLeft);
            g.setColour(juce::Colour(0x22ffffff));
        }
    }

    // Color Bars
    if (viewMode == ViewMode::ColorBars || viewMode == ViewMode::Both)
    {
        for (int i = 0; i < scopeSize; ++i)
        {
            float x = juce::jmap((float)i, 0.0f, (float)(scopeSize-1), 0.0f, w);
            float wRect = (w / scopeSize) + 1.0f; // slight overlap
            
            float lvl1 = scopeData1[i];
            float lvl2 = scopeData2[i];
            float maxLvl = std::max(lvl1, lvl2);
            
            float y = juce::jmap(maxLvl, 0.0f, 1.0f, h, 0.0f);
            
            juce::Colour c = getHeatmapColor(maxLvl);
            g.setColour(c);
            g.fillRect(x, y, wRect, h - y);
        }
    }

    // FFT Lines
    if (viewMode == ViewMode::FFT || viewMode == ViewMode::Both)
    {
        juce::Path p1, p2;
        p1.startNewSubPath(0, h);
        p2.startNewSubPath(0, h);
        
        for (int i = 0; i < scopeSize; ++i)
        {
            float x = juce::jmap((float)i, 0.0f, (float)(scopeSize-1), 0.0f, w);
            float y1 = juce::jmap(scopeData1[i], 0.0f, 1.0f, h, 0.0f);
            float y2 = juce::jmap(scopeData2[i], 0.0f, 1.0f, h, 0.0f);
            
            p1.lineTo(x, y1);
            p2.lineTo(x, y2);
        }
        
        if (colorMode == ColorMode::Ice) {
            juce::Path pFill1 = p1;
            pFill1.lineTo(w, h);
            pFill1.lineTo(0, h);
            pFill1.closeSubPath();
            
            juce::Path pFill2 = p2;
            pFill2.lineTo(w, h);
            pFill2.lineTo(0, h);
            pFill2.closeSubPath();
            
            juce::ColourGradient grad2(juce::Colour(0x880055ff), 0, 0, juce::Colour(0x00001144), 0, h, false);
            g.setGradientFill(grad2);
            g.fillPath(pFill2);

            juce::ColourGradient grad1(juce::Colour(0xaa00aaff), 0, 0, juce::Colour(0x00002266), 0, h, false);
            g.setGradientFill(grad1);
            g.fillPath(pFill1);
        }
        
        // Faint underlying line (Right or Side)
        g.setColour(juce::Colour(0xff88aaff).withAlpha(0.5f));
        g.strokePath(p2, juce::PathStrokeType(1.5f));
        
        // Bright primary line (Left or Mid)
        g.setColour(juce::Colour(0xffcceeff));
        g.strokePath(p1, juce::PathStrokeType(2.0f));
    }

    // Overlay readout
    juce::String overlayText = juce::String(peakDb, 2) + "dB  |  " + 
                               juce::String(peakFreq, 2) + "Hz  |  " + 
                               getNoteString(peakFreq);
                               
    g.setColour(juce::Colour(0x99000000));
    g.fillRoundedRectangle(10.f, 10.f, 280.f, 24.f, 4.f);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 13.0f, juce::Font::bold)));
    g.drawText(overlayText, 15, 10, 270, 24, juce::Justification::centredLeft);
}
