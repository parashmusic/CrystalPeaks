#include "MainComponent.h"
#include "meters/SpectrumMeter.h"

#if JUCE_WINDOWS
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

// ============================================================
MainComponent::MainComponent()
{
    setSize (900, 200);

    // ---- Status label & Settings button ----
    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::Font (juce::FontOptions ("Arial", 12.f, juce::Font::plain)));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (modulesButton);
    modulesButton.onClick = [this]() {
        juce::PopupMenu m;
        m.addItem(1, "Waveform", true, showWaveform);
        m.addItem(2, "Spectrum", true, showSpectrum);
        m.addItem(3, "Stereometer", true, showStereo);
        m.addSeparator();
        m.addItem(4, "Show Bottom Bar", true, showPeakBars);
        m.addItem(5, "Stick Mode (Top)", true, stickMode);
        
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(modulesButton),
            [this](int result) {
                if (result == 1) showWaveform = !showWaveform;
                if (result == 2) showSpectrum = !showSpectrum;
                if (result == 3) showStereo = !showStereo;
                if (result == 4) showPeakBars = !showPeakBars;
                if (result == 5) {
                    stickMode = !stickMode;
                    updateStickMode();
                }
                updateModuleLayout();
                resized();
                repaint();
            });
    };

    // ---- Initialize Modular Layout ----
    waveformMeter = std::make_unique<WaveformMeter>(audioEngine);
    spectrumMeter = std::make_unique<SpectrumMeter>(audioEngine);
    stereoMeter   = std::make_unique<StereoMeter>(audioEngine);
    addAndMakeVisible (moduleStrip);
    updateModuleLayout();

    // ---- Start audio engine ----
    auto err = audioEngine.initialise();
    if (err.isNotEmpty())
        statusLabel.setText ("Error: " + err, juce::dontSendNotification);
    else
        statusLabel.setText (audioEngine.getDeviceInfo(), juce::dontSendNotification);

    startTimerHz (60); // 60fps poll
}

void MainComponent::updateModuleLayout()
{
    std::vector<MeterModule*> active;
    if (showWaveform) active.push_back(waveformMeter.get());
    if (showSpectrum) active.push_back(spectrumMeter.get());
    if (showStereo)   active.push_back(stereoMeter.get());
    moduleStrip.setModules(active);
}

void MainComponent::updateStickMode()
{
    if (auto* topWindow = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
    {
        topWindow->setTitleBarHeight(stickMode ? 0 : 26);
    }

#if JUCE_WINDOWS
    if (auto* topWindow = getTopLevelComponent())
    {
        topWindow->setAlwaysOnTop(stickMode);
        
        HWND hwnd = (HWND)topWindow->getWindowHandle();
        if (!hwnd) return;
        
        static bool isAppBarRegistered = false;
        static UINT APPBAR_CALLBACK = WM_USER + 101;
        
        if (stickMode)
        {
            APPBARDATA abd;
            memset(&abd, 0, sizeof(abd));
            abd.cbSize = sizeof(abd);
            abd.hWnd = hwnd;
            abd.uCallbackMessage = APPBAR_CALLBACK;
            
            if (!isAppBarRegistered) {
                SHAppBarMessage(ABM_NEW, &abd);
                isAppBarRegistered = true;
            }
            
            abd.uEdge = ABE_TOP;
            
            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi;
            mi.cbSize = sizeof(mi);
            GetMonitorInfo(hMon, &mi);
            
            abd.rc.left = mi.rcMonitor.left;
            abd.rc.right = mi.rcMonitor.right;
            abd.rc.top = mi.rcMonitor.top;
            abd.rc.bottom = mi.rcMonitor.top + topWindow->getHeight();
            
            SHAppBarMessage(ABM_QUERYPOS, &abd);
            abd.rc.bottom = abd.rc.top + topWindow->getHeight();
            SHAppBarMessage(ABM_SETPOS, &abd);
            
            topWindow->setBounds(abd.rc.left, abd.rc.top, abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top);
        }
        else
        {
            if (isAppBarRegistered)
            {
                APPBARDATA abd;
                memset(&abd, 0, sizeof(abd));
                abd.cbSize = sizeof(abd);
                abd.hWnd = hwnd;
                
                SHAppBarMessage(ABM_REMOVE, &abd);
                isAppBarRegistered = false;
            }
        }
    }
#endif
}

// ============================================================
MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.shutdown();
}

// ============================================================
void MainComponent::timerCallback()
{
    const int available = audioEngine.getNumAvailableSamples();
    const int toRead    = std::min (available, kScratchSize);

    if (toRead > 0)
    {
        audioEngine.readSamples (scratchL, scratchR, toRead);
        moduleStrip.pushSamples (scratchL, scratchR, toRead);
        moduleStrip.repaint();

        float maxL = 0.f, maxR = 0.f;
        for (int i = 0; i < toRead; ++i)
        {
            maxL = std::max (maxL, std::abs (scratchL[i]));
            maxR = std::max (maxR, std::abs (scratchR[i]));
        }
        peakL = std::max (maxL, peakL.load() * 0.93f);
        peakR = std::max (maxR, peakR.load() * 0.93f);
    }

    // Always update debug label and repaint peak bars
    const int fr = audioEngine.getDbgFailReason();
    juce::String failStr = (fr > 0) ? (" FAIL=" + juce::String(fr)) : "";

    statusLabel.setText (
        "cb=" + juce::String (audioEngine.getDbgCallbackCount())
        + " st=" + juce::String (audioEngine.getDbgAboutToStart())
        + " in=" + juce::String (audioEngine.getDbgSamplesIn())
        + failStr + " | " + audioEngine.getDeviceInfo(),
        juce::dontSendNotification);

    repaint();
}

// ============================================================
void MainComponent::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour (0xff111111));

    if (!showPeakBars) return;

    // Thin separator line between waveform and peak bars
    const int barAreaH = 44;
    const int sepY = getHeight() - barAreaH - 24;
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawHorizontalLine (sepY, 0.f, static_cast<float> (getWidth()));

    // ---- Peak bars (L / R) ----
    const int W = getWidth();
    auto barRect = getLocalBounds()
                       .removeFromBottom (barAreaH)
                       .reduced (20, 8);

    auto lBar = barRect.removeFromTop (14);
    barRect.removeFromTop (4);
    auto rBar = barRect.removeFromTop (14);

    // Track background
    g.setColour (juce::Colour (0xff222222));
    g.fillRoundedRectangle (lBar.toFloat(), 3.f);
    g.fillRoundedRectangle (rBar.toFloat(), 3.f);

    // Active fill
    g.setColour (juce::Colour (0xffff3366));
    g.fillRoundedRectangle (
        lBar.toFloat().withWidth (lBar.getWidth() * peakL.load()), 3.f);

    g.setColour (juce::Colour (0xff3399ff));
    g.fillRoundedRectangle (
        rBar.toFloat().withWidth (rBar.getWidth() * peakR.load()), 3.f);

    // Channel labels
    g.setFont (juce::Font (juce::FontOptions ("Arial", 11.f, juce::Font::bold)));
    g.setColour (juce::Colour (0xff666666));
    g.drawText ("L", lBar.translated (-22, 0), juce::Justification::centredRight);
    g.drawText ("R", rBar.translated (-22, 0), juce::Justification::centredRight);

    // dBFS tick marks: 0, -6, -12, -24, -48
    const float tickdBs[] = { 0.f, -6.f, -12.f, -24.f, -48.f };
    g.setFont (juce::Font (juce::FontOptions ("Arial", 9.f, juce::Font::plain)));
    g.setColour (juce::Colour (0xff444444));
    for (float db : tickdBs)
    {
        const float linear = std::pow (10.f, db / 20.f);
        const int   tx     = 20 + static_cast<int> ((W - 40) * linear);
        g.drawVerticalLine (tx, static_cast<float> (lBar.getY()),
                                static_cast<float> (rBar.getBottom()));
        if (db < 0.f)
        {
            g.drawText (juce::String ((int)db) + "dB",
                        tx - 16, rBar.getBottom() + 2, 32, 10,
                        juce::Justification::centred);
        }
    }
}

// ============================================================
void MainComponent::resized()
{
    auto area = getLocalBounds();

    if (showPeakBars) {
        statusLabel.setVisible(true);
        
        // Status bar at bottom
        auto bottomArea = area.removeFromBottom (20);
        modulesButton.setBounds(bottomArea.removeFromRight(100).reduced(2));
        statusLabel.setBounds (bottomArea.reduced (4, 0));

        // Peak bar strip
        area.removeFromBottom (48);
    } else {
        statusLabel.setVisible(false);
        // Float the modules button in bottom right over the meters
        auto btnArea = area;
        modulesButton.setBounds(btnArea.removeFromBottom(24).removeFromRight(100).reduced(2));
    }

    // Module strip fills the rest
    moduleStrip.setBounds (area.reduced (0, 2));
    modulesButton.toFront(false);
}