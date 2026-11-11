#include "PluginEditor.h"

using namespace ui;

// ============================================================ MidiDragButton

MidiDragButton::MidiDragButton (TriadProcessor& p, bool isBass) : proc (p), bass (isBass)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MidiDragButton::setReady (bool r)
{
    if (ready != r)
    {
        ready = r;
        repaint();
    }
}

void MidiDragButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (hover && ready ? panel2.brighter (0.1f) : panel2);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (ready ? accent.withAlpha (hover ? 1.0f : 0.7f) : line);
    g.drawRoundedRectangle (r, 12.0f, 1.2f);

    g.setColour (ready ? text : dim);
    g.setFont (font (17.0f, true));
    g.drawText (bass ? "Drag Bass MIDI" : "Drag Chord MIDI", getLocalBounds(), juce::Justification::centred);

    const float x = (float) getWidth() - 36.0f, cy = (float) getHeight() * 0.5f;
    g.setColour (ready ? dim : line);
    for (int i = -1; i <= 1; ++i)
        g.fillRoundedRectangle (x, cy + (float) i * 6.0f - 1.0f, 16.0f, 2.0f, 1.0f);
}

void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! ready || dragged || e.getDistanceFromDragStart() < 6)
        return;

    const auto file = proc.writeMidi (bass);
    if (file.existsAsFile())
    {
        dragged = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }
}

void MidiDragButton::mouseUp (const juce::MouseEvent&)
{
    if (ready && ! dragged)
        showMenu();
    dragged = false;
}

void MidiDragButton::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save .mid as...");
    m.addItem (2, "Show file in folder");

    juce::Component::SafePointer<MidiDragButton> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int result)
    {
        if (safe == nullptr || result == 0) return;
        auto self = safe.getComponent();
        const auto file = self->proc.writeMidi (self->bass);
        if (! file.existsAsFile()) return;

        if (result == 2)
        {
            file.revealToUser();
            return;
        }

        self->chooser = std::make_unique<juce::FileChooser> (
            "Save MIDI",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (file.getFileName()),
            "*.mid");

        self->chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [file] (const juce::FileChooser& fc)
                                    {
                                        const auto dest = fc.getResult();
                                        if (dest != juce::File())
                                            file.copyFileTo (dest.withFileExtension ("mid"));
                                    });
    });
}

// ============================================================ TriadEditor

TriadEditor::TriadEditor (TriadProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), chordMidi (p, false), bassMidi (p, true)
{
    setLookAndFeel (&laf);
    setSize (880, 612);

    for (auto* k : { &masterKnob, &sampleKnob, &chordKnob })
    {
        setupKnob (*k);
        addAndMakeVisible (*k);
    }
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "master", masterKnob);
    sampleAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "sampleVol", sampleKnob);
    chordAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "chordVol", chordKnob);
    masterKnob.setDoubleClickReturnValue (true, 0.6);
    sampleKnob.setDoubleClickReturnValue (true, 0.8);
    chordKnob.setDoubleClickReturnValue (true, 0.8);

    bassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bass", bassBtn);

    for (auto* b : { &sampleBtn, &chordsBtn, &bothBtn })
        b->setComponentID ("preview");

    static const char* instNames[8] = { "Piano", "Keys", "Rhodes", "Synth", "Pad", "Organ", "Strings", "Guitar" };
    for (int i = 0; i < 8; ++i)
    {
        instBtn[i].setButtonText (instNames[i]);
        instBtn[i].setComponentID ("chip");
        instBtn[i].onClick = [this, i] { proc.setInstrument (i); };
        addAndMakeVisible (instBtn[i]);
    }

    sampleBtn.onClick = [this] { proc.setPlayMode (proc.getPlayMode() == 1 ? 0 : 1); };
    chordsBtn.onClick = [this] { proc.setPlayMode (proc.getPlayMode() == 2 ? 0 : 2); };
    bothBtn.onClick   = [this] { proc.setPlayMode (proc.getPlayMode() == 3 ? 0 : 3); };
    clearBtn.onClick  = [this] { proc.clearSample(); };
    halfBtn.onClick   = [this] { proc.rescaleBpm (0.5); };
    doubleBtn.onClick = [this] { proc.rescaleBpm (2.0); };

    for (auto* c : std::initializer_list<juce::Component*> { &clearBtn, &halfBtn, &doubleBtn, &sampleBtn, &chordsBtn,
                                                              &bothBtn, &bassBtn, &chordMidi, &bassMidi })
        addAndMakeVisible (*c);

    applyState();
    startTimerHz (30);
}

TriadEditor::~TriadEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TriadEditor::setupKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (160);
}

void TriadEditor::resized()
{
    masterKnob.setBounds (60, 186, 120, 120);
    clearBtn.setBounds (36, 532, 168, 40);

    halfBtn.setBounds (462, 34, 36, 26);
    doubleBtn.setBounds (502, 34, 36, 26);

    sampleBtn.setBounds (258, 358, 508, 36);
    chordsBtn.setBounds (258, 400, 508, 36);
    bothBtn.setBounds (258, 442, 508, 36);
    sampleKnob.setBounds (782, 356, 40, 40);
    chordKnob.setBounds (782, 398, 40, 40);
    bassBtn.setBounds (770, 330, 72, 26);

    for (int i = 0; i < 8; ++i)
        instBtn[i].setBounds (240 + i * 78, 494, 73, 30);

    chordMidi.setBounds (240, 536, 302, 56);
    bassMidi.setBounds (558, 536, 302, 56);
}

int TriadEditor::currentBar() const
{
    if (snap.state != TriadProcessor::State::ready || proc.getPlayMode() == 0)
        return -1;
    const int b = (int) std::floor ((proc.getPlayPositionSeconds() - snap.analysis.offsetSeconds) / snap.analysis.barSeconds());
    return juce::jlimit (0, snap.analysis.bars - 1, b);
}

juce::Rectangle<float> TriadEditor::cellRect (int i) const
{
    const int rows = snap.analysis.bars / 4;
    const float cw = 584.0f / 4.0f;
    const float ch = rows == 1 ? 150.0f : 104.0f;
    const float top = rows == 1 ? 110.0f : 80.0f;
    return { 258.0f + (float) (i % 4) * cw, top + (float) (i / 4) * ch, cw, ch };
}

void TriadEditor::editChord (int bar)
{
    const auto cur = snap.analysis.chords[(size_t) bar];
    juce::PopupMenu menu;
    for (int r = 0; r < 12; ++r)
    {
        juce::PopupMenu sub;
        sub.addItem (1000 + r * 2,     juce::String (triad::noteName (r)) + " major", true, cur.root == r && ! cur.minor);
        sub.addItem (1000 + r * 2 + 1, juce::String (triad::noteName (r)) + " minor", true, cur.root == r && cur.minor);
        menu.addSubMenu (triad::noteName (r), sub);
    }

    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellRect (bar).toNearestInt())),
                        [safe, bar] (int result)
                        {
                            if (safe == nullptr || result < 1000) return;
                            const int v = result - 1000;
                            safe->proc.setChord (bar, { v / 2, (v % 2) == 1 });
                        });
}

void TriadEditor::applyState()
{
    const bool ready = snap.state == TriadProcessor::State::ready;
    for (auto* c : std::initializer_list<juce::Component*> { &halfBtn, &doubleBtn })
        c->setVisible (ready);

    sampleBtn.setEnabled (ready);
    chordsBtn.setEnabled (ready);
    bothBtn.setEnabled (ready);
    chordMidi.setReady (ready);
    bassMidi.setReady (ready);
    clearBtn.setEnabled (snap.state != TriadProcessor::State::empty);
}

void TriadEditor::timerCallback()
{
    const int v = proc.getVersion();
    if (v != lastVersion)
    {
        lastVersion = v;
        snap = proc.getSnapshot();
        applyState();
        repaint();
    }

    const int instSel = (int) proc.apvts.getRawParameterValue ("instrument")->load();
    for (int i = 0; i < 8; ++i)
        instBtn[i].setToggleState (i == instSel, juce::dontSendNotification);

    const int mode = proc.getPlayMode();
    sampleBtn.setToggleState (mode == 1, juce::dontSendNotification);
    chordsBtn.setToggleState (mode == 2, juce::dontSendNotification);
    bothBtn.setToggleState (mode == 3, juce::dontSendNotification);

    const int bar = currentBar();
    if (mode != lastMode || bar != lastBar)
    {
        lastMode = mode;
        lastBar = bar;
        repaint (dropRect.expanded (20));
    }
}

void TriadEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // sidebar
    const juce::Rectangle<float> side (20.0f, 20.0f, 200.0f, 572.0f);
    g.setColour (panel);
    g.fillRoundedRectangle (side, 14.0f);

    g.setColour (dim);
    g.setFont (font (13.0f, true));
    g.drawText ("PRODAP", 36, 38, 160, 18, juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (font (31.0f, true));
    g.drawText ("SAMPLE", 36, 60, 180, 36, juce::Justification::centredLeft);
    g.drawText ("TO CHORD", 36, 92, 180, 36, juce::Justification::centredLeft);
    g.setColour (accent);
    g.fillRoundedRectangle (36.0f, 136.0f, 44.0f, 3.0f, 1.5f);

    g.setColour (dim);
    g.setFont (font (12.0f, true));
    g.drawText ("VOLUME", 36, 310, 168, 16, juce::Justification::centred);

    const bool ready = snap.state == TriadProcessor::State::ready;
    for (int i = 0; i < 2; ++i)
    {
        const int n = i == 0 ? 4 : 8;
        const juce::Rectangle<float> chip (36.0f + (float) i * 92.0f, 350.0f, 76.0f, 30.0f);
        const bool on = ready && snap.analysis.bars == n;
        g.setColour (on ? accent.withAlpha (0.18f) : panel2);
        g.fillRoundedRectangle (chip, 9.0f);
        g.setColour (on ? accent : line);
        g.drawRoundedRectangle (chip.reduced (0.5f), 9.0f, 1.0f);
        g.setColour (on ? accent : dim);
        g.setFont (font (12.0f, true));
        g.drawText (juce::String (n) + " BARS", chip, juce::Justification::centred);
    }
    g.setColour (dim);
    g.setFont (font (11.0f));
    g.drawText ("auto detect", 36, 386, 168, 14, juce::Justification::centred);

    // main panel
    g.setColour (panel);
    g.fillRoundedRectangle (240.0f, 20.0f, 620.0f, 290.0f, 14.0f);
    g.fillRoundedRectangle (240.0f, 324.0f, 620.0f, 160.0f, 14.0f);

    g.setColour (dim);
    g.setFont (font (11.0f, true));
    g.drawText ("PREVIEW", 258, 332, 120, 18, juce::Justification::centredLeft);

    if (ready)
    {
        const auto& a = snap.analysis;

        const juce::Rectangle<float> pill (258.0f, 34.0f, 110.0f, 26.0f);
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (accent);
        g.setFont (font (13.0f, true));
        g.drawText (juce::String (triad::noteName (a.keyRoot)) + (a.keyMinor ? " minor" : " major"),
                    pill, juce::Justification::centred);

        g.setColour (text);
        g.setFont (font (14.0f, true));
        const double rounded = std::round (a.bpm * 10.0) / 10.0;
        g.drawText (juce::String (rounded, rounded == std::floor (rounded) ? 0 : 1) + " BPM",
                    376, 34, 82, 26, juce::Justification::centredLeft);

        g.setColour (dim);
        g.setFont (font (12.0f));
        g.drawText (snap.fileName, 548, 34, 294, 26, juce::Justification::centredRight);

        g.setColour (line);
        g.drawLine (258.0f, 70.0f, 842.0f, 70.0f);

        const int rows = a.bars / 4;
        const int playing = currentBar();

        for (int i = 0; i < a.bars; ++i)
        {
            const auto& c = a.chords[(size_t) i];
            const auto cell = cellRect (i);
            const bool on = i == playing;

            if (on)
            {
                g.setColour (accent.withAlpha (0.16f));
                g.fillRoundedRectangle (cell.reduced (4.0f), 10.0f);
                g.setColour (accent);
                g.drawRoundedRectangle (cell.reduced (4.0f), 10.0f, 1.5f);
            }

            g.setColour (dim);
            g.setFont (font (12.0f));
            g.drawText (juce::String (i + 1), cell.reduced (14.0f, 10.0f), juce::Justification::topLeft);

            g.setColour (text);
            g.setFont (font (rows == 1 ? 40.0f : 34.0f, true));
            g.drawText (c.name(), cell.withTrimmedBottom (16.0f), juce::Justification::centred);

            const auto pcs = c.pitchClasses();
            g.setColour (dim);
            g.setFont (font (12.0f));
            g.drawText (juce::String (triad::noteName (pcs[0])) + "  " + triad::noteName (pcs[1]) + "  " + triad::noteName (pcs[2]),
                        cell.withTrimmedTop (cell.getHeight() * 0.62f), juce::Justification::centredTop);
        }

        g.setColour (dim);
        g.setFont (font (11.0f));
        g.drawText ("galat lage to chord pe click karke badlo", 258, 292, 584, 14, juce::Justification::centred);
    }
    else
    {
        const auto r = dropRect.toFloat();
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, 12.0f);
        const float dashes[] = { 7.0f, 6.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (hover ? accent : line.brighter (0.2f));
        g.strokePath (dashed, juce::PathStrokeType (1.5f));

        g.setColour (text);
        g.setFont (font (24.0f, true));
        auto mid = dropRect.withSizeKeepingCentre (dropRect.getWidth(), 70);

        if (snap.state == TriadProcessor::State::analyzing)
        {
            g.drawText ("Analyzing...", mid.removeFromTop (36), juce::Justification::centred);
            g.setColour (dim);
            g.setFont (font (13.0f));
            g.drawText (snap.fileName, mid, juce::Justification::centredTop);
        }
        else if (snap.state == TriadProcessor::State::failed)
        {
            g.setFont (font (18.0f, true));
            g.drawText (snap.message, mid.removeFromTop (40), juce::Justification::centred);
            g.setColour (dim);
            g.setFont (font (13.0f));
            g.drawText ("click to browse again", mid, juce::Justification::centredTop);
        }
        else
        {
            g.drawText ("Drop audio here", mid.removeFromTop (36), juce::Justification::centred);
            g.setColour (dim);
            g.setFont (font (13.0f));
            g.drawText ("or click to browse", mid, juce::Justification::centredTop);
        }
    }
}

// ------------------------------------------------------------ file loading

bool TriadEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;ogg"))
            return true;
    return false;
}

void TriadEditor::filesDropped (const juce::StringArray& files, int, int)
{
    hover = false;
    repaint();
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;ogg"))
        {
            proc.loadSample (juce::File (f));
            return;
        }
}

void TriadEditor::mouseUp (const juce::MouseEvent& e)
{
    const bool idle = snap.state == TriadProcessor::State::empty || snap.state == TriadProcessor::State::failed;
    if (idle && dropRect.contains (e.getPosition()))
    {
        browse();
        return;
    }

    if (snap.state == TriadProcessor::State::ready)
        for (int i = 0; i < snap.analysis.bars; ++i)
            if (cellRect (i).contains (e.position))
            {
                editChord (i);
                return;
            }
}

void TriadEditor::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio sample", juce::File(), "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<TriadEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f.existsAsFile())
                                  safe->proc.loadSample (f);
                          });
}
