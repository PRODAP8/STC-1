#include "InstrumentBank.h"
#include "InstrumentTable.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>

namespace triad
{
InstrumentBank::InstrumentBank()
{
    formats.registerBasicFormats();
    data.resize ((size_t) inst::kCount);
}

bool InstrumentBank::ensureLoaded (int idx)
{
    if (idx < 0 || idx >= inst::kCount)
        return false;

    const juce::ScopedLock sl (lock);
    if (! data[(size_t) idx].empty())
        return true;

    int size = 0;
    const char* res = BinaryData::getNamedResource (inst::defs[idx].file, size);
    if (res == nullptr || size <= 0)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (res, (size_t) size, false)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf (1, n);
    reader->read (&buf, 0, n, 0, true, false);
    data[(size_t) idx].assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
    return true;
}

void InstrumentBank::addNote (int idx, float* dst, int total, int start, int len,
                              int midi, float gain, double outRate)
{
    if (len <= 0 || start >= total || ! ensureLoaded (idx))
        return;

    const auto& def = inst::defs[idx];
    const auto& d = data[(size_t) idx];
    len = std::min (len, total - start);

    int best = 0, bestDist = 1000;
    for (int r = 0; r < def.numRoots; ++r)
    {
        const int dist = std::abs (midi - def.roots[r]);
        if (dist < bestDist) { bestDist = dist; best = r; }
    }

    const int slot = (int) std::lround (inst::kSlot * inst::kRate);
    const int base = best * slot;
    if (base + slot > (int) d.size())
        return;

    auto readAt = [&] (double p) -> float
    {
        const int i = (int) p;
        const float fr = (float) (p - i);
        if (i < 0 || i + 1 >= slot) return 0.0f;
        return d[(size_t) (base + i)] + (d[(size_t) (base + i + 1)] - d[(size_t) (base + i)]) * fr;
    };

    const double step = std::pow (2.0, (midi - def.roots[best]) / 12.0) * inst::kRate / outRate;
    const double L1 = 2.9 * inst::kRate, L0 = 1.4 * inst::kRate, X = 0.25 * inst::kRate;
    const double loopLen = L1 - L0;
    const int relN = std::max (1, std::min (len, (int) (0.15 * outRate)));
    const int atkN = std::max (1, (int) (0.004 * outRate));

    double p = 0.0;
    for (int i = 0; i < len; ++i)
    {
        float s;
        if (def.looped)
        {
            if (p >= L1) p -= loopLen;
            const float a = readAt (p);
            if (p >= L1 - X)
            {
                const float w = (float) ((p - (L1 - X)) / X);
                s = (1.0f - w) * a + w * readAt (p - loopLen);
            }
            else
                s = a;
        }
        else
        {
            if (p >= (double) slot - 2.0) break;
            s = readAt (p);
        }

        const float atk = i < atkN ? (float) i / (float) atkN : 1.0f;
        const float rel = i > len - relN ? (float) (len - i) / (float) relN : 1.0f;
        dst[start + i] += gain * s * atk * rel;
        p += step;
    }
}

static void normalise (juce::AudioBuffer<float>& b, float target)
{
    const float peak = b.getMagnitude (0, b.getNumSamples());
    if (peak > 1e-6f)
        b.applyGain (target / peak);
}

void renderProgression (const Analysis& a, double sr, InstrumentBank& bank, int instrument,
                        juce::AudioBuffer<float>& chordsOut, juce::AudioBuffer<float>& bassOut)
{
    const int barSamples = (int) std::lround (a.barSeconds() * sr);
    const int offsetSamples = (int) std::lround (a.offsetSeconds * sr);
    const int total = offsetSamples + barSamples * a.bars;

    chordsOut.setSize (1, total);
    bassOut.setSize (1, total);
    chordsOut.clear();
    bassOut.clear();

    auto* c = chordsOut.getWritePointer (0);
    auto* b = bassOut.getWritePointer (0);
    const int bassInstrument = inst::kCount - 1;

    for (int bar = 0; bar < a.bars; ++bar)
    {
        const auto& chord = a.chords[(size_t) bar];
        const auto notes = chordMidiNotes (chord);
        const int start = offsetSamples + bar * barSamples;

        for (int k = 0; k < 3; ++k)
            bank.addNote (instrument, c, total, start, barSamples, notes[(size_t) k], 1.0f, sr);
        bank.addNote (bassInstrument, b, total, start, barSamples, bassMidiNote (chord), 1.0f, sr);
    }

    normalise (chordsOut, 0.8f);
    normalise (bassOut, 0.8f);
}
}
