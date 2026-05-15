#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../AudioEngine.h"

class MeterModule : public juce::Component
{
public:
    explicit MeterModule(AudioEngine& engine) : audioEngine(engine) {}
    ~MeterModule() override = default;

    // Called periodically from the main message thread (timer)
    // with new audio samples to visualize.
    virtual void pushSamples(const float* srcL, const float* srcR, int numSamples) = 0;

protected:
    AudioEngine& audioEngine;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterModule)
};
