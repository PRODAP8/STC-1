#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <functional>
#include <vector>

namespace triad
{
const char* noteName (int pitchClass);

struct Chord
{
    int root = 0;
    bool minor = false;

    juce::String name() const;
    std::array<int, 3> pitchClasses() const;
};

std::array<int, 3> chordMidiNotes (const Chord&);
int bassMidiNote (const Chord&);

struct Features
{
    double frameSeconds = 0.0;
    double firstFrameCentre = 0.0;
    double durationSeconds = 0.0;
    std::vector<std::array<float, 12>> chroma;
    std::vector<float> weight;   // per chroma frame: tonal (high) vs noisy/percussive (low)
    double tuningSemitones = 0.0;
    std::vector<float> energy;      // per chroma frame loudness
    double activeSeconds = 0.0;     // length without the silent / reverb tail
    std::vector<std::array<float, 12>> bass;
    std::vector<float> onset;
    double onsetRate = 0.0;
    int keyRoot = 0;
    bool keyMinor = false;
    std::array<double, 24> keyCorr {};   // [0..11] major tonic, [12..23] minor tonic
};

struct Analysis
{
    double bpm = 120.0;
    int bars = 4;
    std::vector<Chord> chords;
    int keyRoot = 0;
    bool keyMinor = false;
    double durationSeconds = 0.0;
    double offsetSeconds = 0.0;   // where bar 1 starts inside the sample

    double barSeconds() const { return 240.0 / bpm; }
};

// Heavy part (STFT, chroma, onset envelope, key). Run on a background thread.
bool computeFeatures (const juce::AudioBuffer<float>& audio, double sampleRate,
                      Features& out, const std::function<bool()>& shouldExit);

double detectBpm (const Features&);

// Cheap part: bars + one chord per bar for a given BPM.
Analysis buildAnalysis (const Features&, double bpm);

// Same, but fixes the common double-tempo mistake (fast hi-hats make a 72 BPM loop look like 144):
// if every pair of bars repeats the same chord, the bars are really half as long.
Analysis buildAnalysisAuto (const Features&, double bpm);
}
