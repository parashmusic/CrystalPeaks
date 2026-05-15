#include "ModuleStrip.h"
#include <cmath>

class DividerComponent : public juce::Component {
public:
    DividerComponent(ModuleStrip& s, int i) : strip(s), index(i) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setRepaintsOnMouseActivity(true);
    }
    
    void paint(juce::Graphics& g) override {
        // Very subtle 2px line in the middle
        g.setColour(isMouseOverOrDragging() ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff1f1f1f));
        g.fillRect(getWidth() / 2 - 1, 0, 2, getHeight());
    }
    
    void mouseDown(const juce::MouseEvent& e) override {
        strip.mouseDown(e.getEventRelativeTo(&strip));
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        strip.mouseDrag(e.getEventRelativeTo(&strip));
    }
    void mouseUp(const juce::MouseEvent& e) override {
        strip.mouseUp(e.getEventRelativeTo(&strip));
    }
    
private:
    ModuleStrip& strip;
    int index;
};

ModuleStrip::ModuleStrip()
{
    setOpaque(true);
}

void ModuleStrip::setModules(const std::vector<MeterModule*>& newModules)
{
    // Remove old components
    for (auto* m : modules) {
        removeChildComponent(m);
    }
    dividers.clear();
    
    modules = newModules;
    widthFlexes.clear();
    
    // Add new modules
    for (auto* m : modules) {
        addAndMakeVisible(m);
        widthFlexes.push_back(1.0f / modules.size());
    }
    
    // Create dividers
    for (size_t i = 0; i < modules.size(); ++i) {
        if (i < modules.size() - 1) {
            auto div = std::make_unique<DividerComponent>(*this, (int)i);
            addAndMakeVisible(div.get());
            dividers.push_back(std::move(div));
        }
    }
    
    resized();
    repaint();
}

void ModuleStrip::pushSamples(const float* srcL, const float* srcR, int numSamples)
{
    for (auto* mod : modules)
        mod->pushSamples(srcL, srcR, numSamples);
}

void ModuleStrip::paint(juce::Graphics& g)
{
    // Fill background black, though modules should cover it
    g.fillAll(juce::Colour(0xff000000));
}

void ModuleStrip::resized()
{
    if (modules.empty()) return;

    float x = 0;
    for (size_t i = 0; i < modules.size(); ++i)
    {
        float targetW = getWidth() * widthFlexes[i];
        
        // Ensure the last module snaps exactly to the right edge
        if (i == modules.size() - 1)
            targetW = getWidth() - x;
            
        modules[i]->setBounds((int)x, 0, (int)targetW, getHeight());
        
        // Position divider exactly between modules, on top of them
        if (i < modules.size() - 1) {
            dividers[i]->setBounds((int)(x + targetW) - hitAreaMargin, 0, hitAreaMargin * 2, getHeight());
            dividers[i]->toFront(false);
        }
        
        x += targetW;
    }
}

void ModuleStrip::mouseDown(const juce::MouseEvent& e)
{
    draggingDivider = -1;
    if (modules.size() < 2) return;

    float x = 0;
    for (size_t i = 0; i < modules.size() - 1; ++i)
    {
        x += getWidth() * widthFlexes[i];
        // Allow a generous hit area for programmatic dragging via event relative mapping
        if (std::abs(e.x - x) <= hitAreaMargin + 4.f) 
        {
            draggingDivider = (int)i;
            initialDragFlexLeft = widthFlexes[i];
            initialDragFlexRight = widthFlexes[i+1];
            initialDragMouseX = e.x;
            break;
        }
    }
}

void ModuleStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingDivider >= 0)
    {
        float deltaRatio = (float)(e.x - initialDragMouseX) / getWidth();
        
        float newLeft = initialDragFlexLeft + deltaRatio;
        float newRight = initialDragFlexRight - deltaRatio;
        
        // Minimum width of 150 pixels
        float minFlex = 150.0f / std::max(1.0f, (float)getWidth());
        
        if (newLeft < minFlex)
        {
            newLeft = minFlex;
            newRight = initialDragFlexLeft + initialDragFlexRight - minFlex;
        }
        else if (newRight < minFlex)
        {
            newRight = minFlex;
            newLeft = initialDragFlexLeft + initialDragFlexRight - minFlex;
        }
        
        widthFlexes[draggingDivider] = newLeft;
        widthFlexes[draggingDivider + 1] = newRight;
        
        resized();
        repaint();
    }
}

void ModuleStrip::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    draggingDivider = -1;
}
