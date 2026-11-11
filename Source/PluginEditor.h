#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

class MidiDragButton : public juce::Component
{
public:
    MidiDragButton (TriadProcessor&, bool isBass);

    void setReady (bool r);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { dragged = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void showMenu();

    TriadProcessor& proc;
    const bool bass;
    bool ready = false, hover = false, dragged = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class TriadEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit TriadEditor (TriadProcessor&);
    ~TriadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }

private:
    void timerCallback() override;
    void browse();
    void applyState();
    void setupKnob (juce::Slider&);
    int currentBar() const;
    juce::Rectangle<float> cellRect (int index) const;
    void editChord (int bar);

    TriadProcessor& proc;
    ui::TriadLookAndFeel laf;

    juce::Slider masterKnob, sampleKnob, chordKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAtt, sampleAtt, chordAtt;
    juce::TextButton clearBtn { "Clear" }, halfBtn { "/2" }, doubleBtn { "x2" };
    juce::TextButton sampleBtn { "Sample (original)" }, chordsBtn { "Chords" }, bothBtn { "Sample + chords" };
    juce::ToggleButton bassBtn { "Bass" };
    juce::TextButton instBtn[8];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassAtt;
    MidiDragButton chordMidi, bassMidi;
    std::unique_ptr<juce::FileChooser> chooser;

    TriadProcessor::Snapshot snap;
    int lastVersion = -1, lastMode = -1, lastBar = -2;
    bool hover = false;

    juce::Rectangle<int> dropRect { 258, 76, 584, 218 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadEditor)
};
