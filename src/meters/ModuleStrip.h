#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "MeterModule.h"

// ModuleStrip holds a collection of MeterModules in a horizontal flex layout
// and provides draggable dividers to resize them.
class ModuleStrip : public juce::Component
{
public:
    ModuleStrip();
    ~ModuleStrip() override = default;

    // Sets the active modules to display and evenly distributes their width
    void setModules(const std::vector<MeterModule*>& newModules);

    // Forwards audio samples to all visible modules
    void pushSamples(const float* srcL, const float* srcR, int numSamples);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Resizing interactions (called by DividerComponent)
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    std::vector<MeterModule*> modules;
    std::vector<float> widthFlexes;
    std::vector<std::unique_ptr<juce::Component>> dividers;

    int draggingDivider { -1 };
    float initialDragFlexLeft { 0.f };
    float initialDragFlexRight { 0.f };
    int initialDragMouseX { 0 };

    static constexpr int hitAreaMargin = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleStrip)
};
