#include "StereoMeter.h"

StereoMeter::StereoMeter(AudioEngine& engine) : MeterModule(engine)
{
    points.resize(maxPoints * 3); // 3x for multiband mode
    
    float sr = engine.getSampleRate();
    if (sr <= 0) sr = 48000.f;
    
    auto setFilters = [&](juce::dsp::LinkwitzRileyFilter<float>& f, float freq, juce::dsp::LinkwitzRileyFilterType type) {
        f.setType(type);
        f.setCutoffFrequency(freq);
        f.prepare({sr, (juce::uint32)512, 1});
    };
    
    setFilters(lp1L, 250.f, juce::dsp::LinkwitzRileyFilterType::lowpass);
    setFilters(lp1R, 250.f, juce::dsp::LinkwitzRileyFilterType::lowpass);
    setFilters(hp1L, 250.f, juce::dsp::LinkwitzRileyFilterType::highpass);
    setFilters(hp1R, 250.f, juce::dsp::LinkwitzRileyFilterType::highpass);
    
    setFilters(lp2L, 2500.f, juce::dsp::LinkwitzRileyFilterType::lowpass);
    setFilters(lp2R, 2500.f, juce::dsp::LinkwitzRileyFilterType::lowpass);
    setFilters(hp2L, 2500.f, juce::dsp::LinkwitzRileyFilterType::highpass);
    setFilters(hp2R, 2500.f, juce::dsp::LinkwitzRileyFilterType::highpass);
}

float StereoMeter::calculateCorrelation(float l, float r, float& pL, float& pR, float& cross)
{
    float alpha = 0.0005f;
    pL = pL * (1.0f - alpha) + (l * l) * alpha;
    pR = pR * (1.0f - alpha) + (r * r) * alpha;
    cross = cross * (1.0f - alpha) + (l * r) * alpha;
    
    float denom = std::sqrt(pL * pR);
    if (denom < 1e-6f) return 0.0f;
    return juce::jlimit(-1.0f, 1.0f, cross / denom);
}

void StereoMeter::pushSamples(const float* srcL, const float* srcR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float l = srcL[i];
        float r = srcR[i];
        
        corrOverall = calculateCorrelation(l, r, smoothPowerOverallL, smoothPowerOverallR, smoothCrossOverall);
        
        float lowL = lp1L.processSample(0, l);
        float lowR = lp1R.processSample(0, r);
        float remainL = hp1L.processSample(0, l);
        float remainR = hp1R.processSample(0, r);
        
        float midL = lp2L.processSample(0, remainL);
        float midR = lp2R.processSample(0, remainR);
        float highL = hp2L.processSample(0, remainL);
        float highR = hp2R.processSample(0, remainR);
        
        corrLow = calculateCorrelation(lowL, lowR, smoothPowerLowL, smoothPowerLowR, smoothCrossLow);
        corrMid = calculateCorrelation(midL, midR, smoothPowerMidL, smoothPowerMidR, smoothCrossMid);
        corrHigh = calculateCorrelation(highL, highR, smoothPowerHighL, smoothPowerHighR, smoothCrossHigh);
        
        // Subsample plotting to save CPU (draw 1 out of every 2 samples)
        static int subSample = 0;
        if (++subSample >= 2)
        {
            subSample = 0;
            
            auto getXY = [&](float sl, float sr, float& x, float& y) {
                if (displayMode == DisplayMode::Lissajous) {
                    x = sr;
                    y = -sl; // Usually Left is Y, Right is X. Negated so L+ is up.
                } else {
                    // Linear / Scaled: Rotated 45 deg (Mid is up/down, Side is left/right)
                    // Mid = (L+R), Side = (L-R)
                    x = (sl - sr) * 0.7071f;
                    y = -(sl + sr) * 0.7071f; // negative so +Mid is UP
                    
                    if (displayMode == DisplayMode::Scaled) {
                        auto scale = [](float v) { 
                            float sign = v < 0 ? -1.f : 1.f;
                            return sign * std::pow(std::abs(v), 0.5f);
                        };
                        x = scale(x);
                        y = scale(y);
                    }
                }
            };
            
            if (colorMode == ColorMode::MultiBand) {
                float xl, yl, xm, ym, xh, yh;
                getXY(lowL, lowR, xl, yl);
                getXY(midL, midR, xm, ym);
                getXY(highL, highR, xh, yh);
                
                points[pointIndex++] = {xl, yl, juce::Colour(0xffff3333)}; // Red Low
                if (pointIndex >= points.size()) pointIndex = 0;
                points[pointIndex++] = {xm, ym, juce::Colour(0xff33ff33)}; // Green Mid
                if (pointIndex >= points.size()) pointIndex = 0;
                points[pointIndex++] = {xh, yh, juce::Colour(0xff3333ff)}; // Blue High
                if (pointIndex >= points.size()) pointIndex = 0;
            } else {
                float px, py;
                getXY(l, r, px, py);
                
                juce::Colour c = juce::Colour(0xffaaffff); // Static
                if (colorMode == ColorMode::RGB) {
                    float pl = std::abs(lowL) + std::abs(lowR);
                    float pm = std::abs(midL) + std::abs(midR);
                    float ph = std::abs(highL) + std::abs(highR);
                    float maxP = std::max({pl, pm, ph, 0.0001f});
                    
                    juce::uint8 rC = (juce::uint8)(pl/maxP * 255);
                    juce::uint8 gC = (juce::uint8)(pm/maxP * 255);
                    juce::uint8 bC = (juce::uint8)(ph/maxP * 255);
                    c = juce::Colour(rC, gC, bC);
                }
                
                points[pointIndex++] = {px, py, c};
                if (pointIndex >= points.size()) pointIndex = 0;
            }
        }
    }
}

void StereoMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff000000));
    
    float w = (float)getWidth();
    float h = (float)getHeight();
    
    // Draw correlation meter
    float corrW = 40.f;
    float corrH = h - 60.f;
    float corrX = w - corrW - 20.f;
    float corrY = 30.f;
    
    g.setColour(juce::Colour(0xff888888));
    g.setFont(12.f);
    g.drawText("+1", (int)corrX - 25, (int)corrY - 6, 20, 12, juce::Justification::centredRight);
    g.drawText("0",  (int)corrX - 25, (int)(corrY + corrH/2) - 6, 20, 12, juce::Justification::centredRight);
    g.drawText("-1", (int)corrX - 25, (int)(corrY + corrH) - 6, 20, 12, juce::Justification::centredRight);
    
    // Meter background
    g.setColour(juce::Colour(0xff151515));
    g.fillRect(corrX, corrY, corrW, corrH);
    
    // Zero line
    g.setColour(juce::Colour(0xff666666));
    g.drawHorizontalLine((int)(corrY + corrH/2), corrX - 5.f, corrX + corrW);
    g.drawHorizontalLine((int)corrY, corrX - 5.f, corrX + corrW);
    g.drawHorizontalLine((int)(corrY + corrH), corrX - 5.f, corrX + corrW);
    
    auto drawTickAndTrail = [&](float corr, juce::Colour c, float xPos, float barW) {
        float yCenter = corrY + corrH/2;
        float yVal = yCenter - corr * (corrH/2);
        
        // Draw faint trail from 0 to current value
        g.setColour(c.withAlpha(0.2f));
        if (corr > 0) g.fillRect(xPos, yVal, barW, yCenter - yVal);
        else          g.fillRect(xPos, yCenter, barW, yVal - yCenter);
        
        // Draw the bright horizontal line (tick)
        g.setColour(c);
        g.fillRect(xPos, yVal - 1.5f, barW, 3.0f);
    };
    
    if (corrMode == CorrMode::MultiBand) {
        float bw = corrW / 4.0f;
        drawTickAndTrail(corrLow, juce::Colour(0xffff3333), corrX, bw);
        drawTickAndTrail(corrMid, juce::Colour(0xff33ff33), corrX + bw, bw);
        drawTickAndTrail(corrHigh, juce::Colour(0xff3333ff), corrX + bw*2, bw);
        drawTickAndTrail(corrOverall, juce::Colour(0xffaaaaaa), corrX + bw*3, bw);
    } else {
        drawTickAndTrail(corrOverall, juce::Colour(0xffaaaaaa), corrX, corrW);
    }
    
    // Draw Goniometer Scope
    float scopeW = w - 100.f; // reserve 100px for right meter
    float size = std::min(scopeW, h) * 0.9f;
    float cx = scopeW / 2.0f;
    float cy = h / 2.0f;
    
    g.setColour(juce::Colour(0xff333333));
    if (displayMode == DisplayMode::Linear || displayMode == DisplayMode::Scaled) {
        juce::Path p;
        float r1 = size / 2.0f;
        p.addQuadrilateral(cx, cy - r1, cx + r1, cy, cx, cy + r1, cx - r1, cy);
        float r2 = size / 4.0f;
        p.addQuadrilateral(cx, cy - r2, cx + r2, cy, cx, cy + r2, cx - r2, cy);
        g.strokePath(p, juce::PathStrokeType(1.5f));
        
        // Crosses
        g.drawLine(cx, cy - r1, cx, cy + r1);
        g.drawLine(cx - r1, cy, cx + r1, cy);
        float rt = r1 * 0.7071f;
        g.drawLine(cx - rt, cy - rt, cx + rt, cy + rt);
        g.drawLine(cx - rt, cy + rt, cx + rt, cy - rt);
    } else {
        // Lissajous square grid
        float r1 = size / 2.0f;
        g.drawRect(cx - r1, cy - r1, size, size, 1.5f);
        g.drawRect(cx - r1/2, cy - r1/2, size/2, size/2, 1.5f);
        g.drawLine(cx, cy - r1, cx, cy + r1);
        g.drawLine(cx - r1, cy, cx + r1, cy);
        g.drawLine(cx - r1, cy - r1, cx + r1, cy + r1);
        g.drawLine(cx - r1, cy + r1, cx + r1, cy - r1);
    }
    
    float scale = size / 2.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        float px = cx + points[i].x * scale;
        float py = cy + points[i].y * scale;
        
        g.setColour(points[i].c.withAlpha(0.6f));
        g.fillRect(px - 1.f, py - 1.f, 2.f, 2.f);
    }
}

void StereoMeter::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        
        juce::PopupMenu dispMenu;
        dispMenu.addItem(10, "Scaled", true, displayMode == DisplayMode::Scaled);
        dispMenu.addItem(11, "Linear", true, displayMode == DisplayMode::Linear);
        dispMenu.addItem(12, "Lissajous", true, displayMode == DisplayMode::Lissajous);
        m.addSubMenu("Display Mode", dispMenu);
        
        juce::PopupMenu colMenu;
        colMenu.addItem(20, "Static", true, colorMode == ColorMode::Static);
        colMenu.addItem(21, "RGB", true, colorMode == ColorMode::RGB);
        colMenu.addItem(22, "Multi-Band", true, colorMode == ColorMode::MultiBand);
        m.addSubMenu("Color Mode", colMenu);
        
        juce::PopupMenu corrMenu;
        corrMenu.addItem(30, "Single-Band", true, corrMode == CorrMode::SingleBand);
        corrMenu.addItem(31, "Multi-Band", true, corrMode == CorrMode::MultiBand);
        m.addSubMenu("Correlation", corrMenu);

        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int result) {
                if (result == 10) displayMode = DisplayMode::Scaled;
                if (result == 11) displayMode = DisplayMode::Linear;
                if (result == 12) displayMode = DisplayMode::Lissajous;
                
                if (result == 20) colorMode = ColorMode::Static;
                if (result == 21) colorMode = ColorMode::RGB;
                if (result == 22) colorMode = ColorMode::MultiBand;
                
                if (result == 30) corrMode = CorrMode::SingleBand;
                if (result == 31) corrMode = CorrMode::MultiBand;
                
                repaint();
            });
    }
}
