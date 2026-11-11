#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include "Analysis.h"

namespace triad
{
// Multisampled instruments (rendered from the MIT-licensed MuseScore General soundfont,
// embedded in the plugin as Ogg). Notes are pitch-shifted from the nearest recorded root.
class InstrumentBank
{
public:
    InstrumentBank();

    void addNote (int instrument, float* dst, int total, int start, int len,
                  int midiNote, float gain, double outRate);

private:
    bool ensureLoaded (int instrument);

    juce::AudioFormatManager formats;
    juce::CriticalSection lock;
    std::vector<std::vector<float>> data;
};

// Chords use `instrument` (0..7), the bass preview always uses the bass instrument.
void renderProgression (const Analysis&, double sampleRate, InstrumentBank&, int instrument,
                        juce::AudioBuffer<float>& chordsOut, juce::AudioBuffer<float>& bassOut);
}
