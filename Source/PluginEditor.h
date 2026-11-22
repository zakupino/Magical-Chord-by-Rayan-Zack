#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "PluginProcessor.h"

class MagicalChordsAudioProcessorEditor;

class MidiDragRelay final : public juce::MouseListener
{
public:
    MidiDragRelay (MagicalChordsAudioProcessorEditor& editorIn,
                   int cardIndexIn,
                   juce::Component& targetCompIn);

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    MagicalChordsAudioProcessorEditor& editor;
    int cardIndex = 0;
    juce::Component& targetComp;
    bool dragStarted = false;
};

class MagicalChordsAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                public juce::DragAndDropContainer,
                                                private juce::Timer
{
public:
    explicit MagicalChordsAudioProcessorEditor (MagicalChordsAudioProcessor&);
    ~MagicalChordsAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::File buildMidiFile (int cardIndex);

private:
    void timerCallback() override;
    void applyDetectedKeyAndScale();
    void syncFromProcessor();
    void syncToProcessor();
    void refreshCardLabels();

    MagicalChordsAudioProcessor& audioProcessor;

    juce::Label titleLabel;
    juce::Label subtitleLabel;

    juce::TextButton listenBtn { "START LISTENING" };
    juce::TextButton importBtn { "IMPORT AUDIO" };
    juce::TextButton resetBtn  { "GENERATE CHORDS" };

    juce::ComboBox genreBox;
    juce::ComboBox keyBox;
    juce::ComboBox scaleBox;
    juce::ToggleButton syncToggle { "DAW SYNC" };
    juce::Label bpmLabel;

    struct Card
    {
        juce::String name;
        int variation = 0;
        juce::TextButton prevButton { "<" };
        juce::TextButton nextButton { ">" };
        juce::TextButton dragButton { "DRAG MIDI >>" };
        juce::Rectangle<int> area;
        std::unique_ptr<MidiDragRelay> dragRelay;
    };

    std::array<Card, 4> cards;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicalChordsAudioProcessorEditor)
};
