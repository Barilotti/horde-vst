#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace horde::ui
{
namespace Colours
{
    const juce::Colour bg        { 0xff0c0a0a };
    const juce::Colour panel     { 0xff161212 };
    const juce::Colour panelHi   { 0xff1e1817 };
    const juce::Colour panelEdge { 0xff2e2422 };
    const juce::Colour text      { 0xffece4de };
    const juce::Colour textDim   { 0xff8d817b };
    const juce::Colour ember     { 0xffff5a2a };
    const juce::Colour red       { 0xffe0302a };
    const juce::Colour blood     { 0xff9e1a17 };
    const juce::Colour male      { 0xff4f8dff };
    const juce::Colour female    { 0xffff5fb0 };
}

constexpr int kDesignW = 1120;
constexpr int kDesignH = 700;

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

// Header logo, drawn as distressed brush lettering.
class Logo : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

class Battlefield : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Battlefield (const HordeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    const HordeProcessor& proc;
    std::array<float, horde::kMaxVoices> smooth {};
    std::array<float, horde::kMaxVoices> fade {};
};

class InputMeters : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InputMeters (HordeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    HordeProcessor& proc;
    std::array<float, horde::kMaxTakes> lv {};
};

class OutputMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit OutputMeter (HordeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    HordeProcessor& proc;
    std::array<float, horde::kNumSpeakers> lv {};
};

// Header control: < group name >. Click the name to open the Crowds browser.
class GroupSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GroupSelector (HordeProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onOpen;
private:
    HordeProcessor& proc;
    juce::TextButton prev { "<" }, next { ">" };
};

// Altiverb-style browser: groups on the left, all loaded files on the right.
class CrowdsBrowser : public juce::Component
{
public:
    explicit CrowdsBrowser (HordeProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refresh();
    std::function<void()> onAddFiles;
    std::function<void()> onClose;
private:
    juce::Rectangle<int> panelArea() const;
    juce::Rectangle<int> groupsArea() const;
    juce::Rectangle<int> filesArea() const;
    juce::Rectangle<float> groupRow (int row) const;
    juce::Rectangle<float> fileRow (int visibleRow) const;
    int visibleFileRows() const;
    void startNaming (int groupIndex);    // -1 = new group
    void commitNaming();
    void assignSelected (bool intoGroup);
    void updateButtons();

    HordeProcessor& proc;
    std::vector<bool> selected;
    int lastClicked = -1, fileScroll = 0, namingIndex = -2;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") };
    juce::TextButton newButton { "+ NEW GROUP" }, renameButton { "RENAME" }, deleteButton { "DELETE" };
    juce::TextButton putButton { "PUT INTO GROUP" }, removeButton { "REMOVE FROM GROUP" };
    juce::TextButton allButton { "SELECT ALL" }, addButton { "+ ADD FILES" };
    juce::TextEditor nameEditor;
};

// Slide-out drawer at the bottom: per output channel MIX knob, level fader and meter.
class ChannelDrawer : public juce::Component
{
public:
    explicit ChannelDrawer (HordeProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    std::function<void()> onClose;
private:
    struct Strip
    {
        juce::Slider mix { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Slider level { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAtt, levelAtt;
        float meter = 0.0f;
    };
    std::array<int, horde::kNumSpeakers> shown() const;   // speakers visible for the current format, -1 terminated
    juce::Rectangle<int> stripArea (int k, int count) const;
    HordeProcessor& proc;
    std::array<Strip, horde::kNumSpeakers> strips;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xe2\x96\xbc") };
    int lastFormat = -1;
};

class TakesPanel : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TakesPanel (HordeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    juce::Rectangle<float> rowBounds (int visibleRow) const;
    HordeProcessor& proc;
    int scroll = 0;
};

class RoomView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RoomView (HordeProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    void step (int delta);
    HordeProcessor& proc;
    juce::TextButton prev { "<" }, next { ">" };
    int lastType = -1;
};

struct Knob
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String id, label, range;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool big = false, side = false;
};

enum class Icon { crowd, clock, wave, output, room };

struct Panel
{
    juce::String title;
    Icon icon;
    juce::Rectangle<int> bounds;
};

// All UI lives in this fixed-size (1120x700) component; the editor scales it.
class Content : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Content (HordeProcessor&);
    ~Content() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void showPanel (int which) { if (which == 1) { setDrawerOpen (true); drawerPos = 1.0f; resized(); } else if (which == 2) openCrowds(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void paintCaptions (juce::Graphics&);
    juce::Rectangle<int> drawerTab() const { return { 486, 684, 148, 16 }; }
    void setDrawerOpen (bool);
    void openCrowds();
    void reroll();
    void updateMode();
    void chooseFiles();
    Knob& addKnob (const char* id, const char* label, const char* range = "");
    void placeKnob (Knob&, juce::Rectangle<int> area);

    HordeProcessor& proc;
    LookAndFeel lnf;
    Logo logo;
    Battlefield field;
    InputMeters meters;
    TakesPanel takesPanel;
    OutputMeter outMeter;
    RoomView room;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Panel> panels;
    juce::Slider balance { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> balanceAtt;
    juce::TextButton rerollButton { "REROLL" };
    juce::Label seedValue;
    juce::TextButton addButton { "+ ADD" };
    juce::TextButton linkButton { "LINK" };
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton crowdsButton { "CROWDS" };
    GroupSelector groupSelector;
    CrowdsBrowser browser;
    ChannelDrawer drawer;
    bool drawerOpen = false;
    float drawerPos = 0.0f;     // 0 hidden .. 1 fully open
    int blink = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;
    juce::TooltipWindow tooltips { this, 450 };
};
} // namespace horde::ui

class HordeEditor : public juce::AudioProcessorEditor
{
public:
    explicit HordeEditor (HordeProcessor&);
    void resized() override;
private:
    horde::ui::Content content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HordeEditor)
};
