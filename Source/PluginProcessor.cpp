#include "PluginProcessor.h"
#include "InstrumentTable.h"
#include "PluginEditor.h"

class TriadProcessor::Job : public juce::Thread
{
public:
    explicit Job (std::function<void (juce::Thread&)> f) : juce::Thread ("Sample to Chord analysis"), fn (std::move (f)) {}
    void run() override { fn (*this); }

private:
    std::function<void (juce::Thread&)> fn;
};

juce::AudioProcessorValueTreeState::ParameterLayout TriadProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "master", 1 }, "Master",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sampleVol", 1 }, "Sample volume",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "chordVol", 1 }, "Chords volume",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bass", 1 }, "Bass preview", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "instrument", 1 }, "Instrument",
                                                              juce::StringArray { "Piano", "Keys", "Rhodes", "Synth", "Pad", "Organ", "Strings", "Guitar" }, 0));
    return layout;
}

TriadProcessor::TriadProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formats.registerBasicFormats();
    pMaster = apvts.getRawParameterValue ("master");
    pSample = apvts.getRawParameterValue ("sampleVol");
    pChord  = apvts.getRawParameterValue ("chordVol");
    pBass   = apvts.getRawParameterValue ("bass");
    pInstrument = apvts.getRawParameterValue ("instrument");
}

TriadProcessor::~TriadProcessor()
{
    stopJob();
}

bool TriadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

juce::AudioProcessorEditor* TriadProcessor::createEditor() { return new TriadEditor (*this); }

void TriadProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TriadProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ------------------------------------------------------------------ audio

void TriadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int requested = requestedMode.load();
    if (requested != lastMode)
    {
        lastMode = requested;
        if (requested != 0)
        {
            renderMode = requested;   // new start: restart from the top with a short fade-in
            playPos = 0.0;
            env = 0.0f;
        }
    }
    if (renderMode == 0)
    {
        uiPosition.store (0.0);
        return;
    }

    juce::SpinLock::ScopedTryLockType lock (audioLock);
    if (! lock.isLocked() || preview == nullptr)
        return;

    const auto* sampleBuf = preview->sample.get();
    const int sampleLen = sampleBuf != nullptr ? sampleBuf->getNumSamples() : 0;
    const int total = preview->lengthSamples;
    if (total <= 0)
        return;

    const float* sl = sampleLen > 0 ? sampleBuf->getReadPointer (0) : nullptr;
    const float* sr = sampleLen > 0 ? sampleBuf->getReadPointer (sampleBuf->getNumChannels() > 1 ? 1 : 0) : nullptr;
    const float* chL = preview->chords.getReadPointer (0);
    const float* chR = preview->chords.getReadPointer (preview->chords.getNumChannels() > 1 ? 1 : 0);
    const float* bsL = preview->bass.getReadPointer (0);
    const float* bsR = preview->bass.getReadPointer (preview->bass.getNumChannels() > 1 ? 1 : 0);

    const double ratio = 44100.0 / juce::jmax (1.0, getSampleRate());
    const float tM = pMaster->load(), tS = pSample->load(), tC = pChord->load() * 0.6f;
    const bool bassOn = pBass->load() > 0.5f;
    const float envTarget = requested != 0 ? 1.0f : 0.0f;
    const double endFadeLen = 0.012 * 44100.0;

    if (! gainsInit)
    {
        gM = tM; gS = tS; gC = tC;
        gainsInit = true;
    }

    const int n = buffer.getNumSamples();
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    constexpr float smooth = 0.002f;

    for (int i = 0; i < n; ++i)
    {
        if (playPos >= (double) total)
        {
            requestedMode.store (0);
            lastMode = 0;
            renderMode = 0;
            playPos = 0.0;
            env = 0.0f;
            break;
        }

        env += (envTarget - env) * 0.004f;
        if (requested == 0 && env < 0.0005f)
        {
            renderMode = 0;
            playPos = 0.0;
            env = 0.0f;
            break;
        }

        gM += (tM - gM) * smooth;
        gS += (tS - gS) * smooth;
        gC += (tC - gC) * smooth;

        const int i0 = (int) playPos;
        const float fr = (float) (playPos - (double) i0);
        float l = 0.0f, r = 0.0f;

        if ((renderMode & 1) != 0 && sl != nullptr && i0 + 1 < sampleLen)
        {
            l += gS * (sl[i0] + (sl[i0 + 1] - sl[i0]) * fr);
            r += gS * (sr[i0] + (sr[i0 + 1] - sr[i0]) * fr);
        }

        if ((renderMode & 2) != 0 && i0 + 1 < total)
        {
            float cl = chL[i0] + (chL[i0 + 1] - chL[i0]) * fr;
            float cr = chR[i0] + (chR[i0 + 1] - chR[i0]) * fr;
            if (bassOn)
            {
                cl += 0.8f * (bsL[i0] + (bsL[i0 + 1] - bsL[i0]) * fr);
                cr += 0.8f * (bsR[i0] + (bsR[i0 + 1] - bsR[i0]) * fr);
            }
            l += gC * cl;
            r += gC * cr;
        }

        const float endFade = (float) juce::jmin (1.0, ((double) total - playPos) / endFadeLen);
        const float g = gM * env * endFade;
        outL[i] = l * g;
        if (outR != nullptr) outR[i] = r * g;
        playPos += ratio;
    }

    uiPosition.store (playPos / 44100.0);
}

// ------------------------------------------------------------------ analysis jobs

void TriadProcessor::startJob (std::function<void (juce::Thread&)> fn)
{
    stopJob();
    job = std::make_unique<Job> (std::move (fn));
    job->startThread();
}

void TriadProcessor::stopJob()
{
    if (job != nullptr)
    {
        job->stopThread (10000);
        job.reset();
    }
}

std::unique_ptr<TriadProcessor::PreviewData> TriadProcessor::makePreview (const triad::Analysis& a,
                                                                          std::shared_ptr<const juce::AudioBuffer<float>> s)
{
    auto p = std::make_unique<PreviewData>();
    p->sample = std::move (s);
    triad::renderProgression (a, 44100.0, bank, juce::jlimit (0, inst::kCount - 2, (int) pInstrument->load()), p->chords, p->bass);
    p->lengthSamples = p->chords.getNumSamples();
    return p;
}

void TriadProcessor::publish (std::unique_ptr<PreviewData> newPreview,
                              std::shared_ptr<const triad::Features> f,
                              std::shared_ptr<const juce::AudioBuffer<float>> s,
                              const triad::Analysis& a)
{
    std::unique_ptr<PreviewData> old;
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        old = std::move (preview);
        preview = std::move (newPreview);
    }
    {
        const juce::ScopedLock sl (snapLock);
        features = std::move (f);
        sample44 = std::move (s);
        snap.state = State::ready;
        snap.message = {};
        snap.analysis = a;
    }
    ++version;
}

void TriadProcessor::fail (const juce::String& message)
{
    {
        const juce::ScopedLock sl (snapLock);
        snap.state = State::failed;
        snap.message = message;
    }
    ++version;
}

void TriadProcessor::loadSample (const juce::File& file)
{
    stopJob();
    requestedMode.store (0);

    std::unique_ptr<PreviewData> old;
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        old = std::move (preview);
    }
    {
        const juce::ScopedLock sl (snapLock);
        snap = {};
        snap.state = State::analyzing;
        snap.fileName = file.getFileName();
        features.reset();
        sample44.reset();
    }
    ++version;

    startJob ([this, file] (juce::Thread& t)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            fail ("Ye audio file nahi padhi gayi. WAV, MP3, FLAC ya AIFF try karo.");
            return;
        }

        const double srcRate = reader->sampleRate;
        const int n = (int) juce::jmin ((juce::int64) (srcRate * 90.0), reader->lengthInSamples);
        if (n < 8192 || srcRate < 8000.0)
        {
            fail ("Sample bahut chhota hai.");
            return;
        }

        juce::AudioBuffer<float> raw (2, n);
        reader->read (&raw, 0, n, 0, true, true);
        if (reader->numChannels < 2)
            raw.copyFrom (1, 0, raw, 0, 0, n);
        reader.reset();

        if (t.threadShouldExit()) return;

        auto feats = std::make_shared<triad::Features>();
        if (! triad::computeFeatures (raw, srcRate, *feats, [&t] { return t.threadShouldExit(); }))
        {
            if (! t.threadShouldExit())
                fail ("Is sample se chords nahi nikal paye.");
            return;
        }

        const double bpm = triad::detectBpm (*feats);
        const auto analysis = triad::buildAnalysisAuto (*feats, bpm);

        // 44.1 kHz stereo copy for the preview
        auto s44 = std::make_shared<juce::AudioBuffer<float>>();
        if (std::abs (srcRate - 44100.0) < 1.0)
        {
            s44->makeCopyOf (raw);
        }
        else
        {
            const double ratio = srcRate / 44100.0;
            const int outN = (int) std::floor ((double) n / ratio) - 2;
            s44->setSize (2, juce::jmax (1, outN));
            s44->clear();

            for (int c = 0; c < 2; ++c)
            {
                std::vector<float> in ((size_t) n + 16, 0.0f);
                std::copy (raw.getReadPointer (c), raw.getReadPointer (c) + n, in.begin());
                juce::LagrangeInterpolator interp;
                interp.process (ratio, in.data(), s44->getWritePointer (c), outN);
            }
        }

        if (t.threadShouldExit()) return;

        auto prev = makePreview (analysis, s44);
        if (t.threadShouldExit()) return;

        publish (std::move (prev), feats, s44, analysis);
    });
}

void TriadProcessor::clearSample()
{
    stopJob();
    requestedMode.store (0);

    std::unique_ptr<PreviewData> old;
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        old = std::move (preview);
    }
    {
        const juce::ScopedLock sl (snapLock);
        snap = {};
        features.reset();
        sample44.reset();
    }
    ++version;
}

void TriadProcessor::rescaleBpm (double factor)
{
    std::shared_ptr<const triad::Features> fe;
    std::shared_ptr<const juce::AudioBuffer<float>> s44;
    double bpm = 0.0;
    {
        const juce::ScopedLock sl (snapLock);
        if (snap.state != State::ready || features == nullptr)
            return;
        fe = features;
        s44 = sample44;
        bpm = snap.analysis.bpm * factor;
    }

    bpm = juce::jlimit (40.0, 240.0, bpm);
    requestedMode.store (0);

    startJob ([this, fe, s44, bpm] (juce::Thread& t)
    {
        const auto a = triad::buildAnalysis (*fe, bpm);
        auto prev = makePreview (a, s44);
        if (! t.threadShouldExit())
            publish (std::move (prev), fe, s44, a);
    });
}

void TriadProcessor::setInstrument (int index)
{
    index = juce::jlimit (0, inst::kCount - 2, index);
    if (auto* prm = apvts.getParameter ("instrument"))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));

    std::shared_ptr<const triad::Features> fe;
    std::shared_ptr<const juce::AudioBuffer<float>> s44;
    triad::Analysis a;
    {
        const juce::ScopedLock sl (snapLock);
        if (snap.state != State::ready)
            return;
        a = snap.analysis;
        fe = features;
        s44 = sample44;
    }
    requestedMode.store (0);

    startJob ([this, fe, s44, a] (juce::Thread& t)
    {
        auto prev = makePreview (a, s44);
        if (! t.threadShouldExit())
            publish (std::move (prev), fe, s44, a);
    });
}

void TriadProcessor::setChord (int bar, triad::Chord chord)
{
    std::shared_ptr<const triad::Features> fe;
    std::shared_ptr<const juce::AudioBuffer<float>> s44;
    triad::Analysis a;
    {
        const juce::ScopedLock sl (snapLock);
        if (snap.state != State::ready || bar < 0 || bar >= (int) snap.analysis.chords.size())
            return;
        snap.analysis.chords[(size_t) bar] = chord;
        a = snap.analysis;
        fe = features;
        s44 = sample44;
    }
    ++version;
    requestedMode.store (0);

    startJob ([this, fe, s44, a] (juce::Thread& t)
    {
        auto prev = makePreview (a, s44);
        if (! t.threadShouldExit())
            publish (std::move (prev), fe, s44, a);
    });
}

TriadProcessor::Snapshot TriadProcessor::getSnapshot() const
{
    const juce::ScopedLock sl (snapLock);
    return snap;
}

// ------------------------------------------------------------------ MIDI export

juce::File TriadProcessor::writeMidi (bool bass)
{
    const auto s = getSnapshot();
    if (s.state != State::ready)
        return {};

    constexpr int tpq = 480;
    const int barTicks = 4 * tpq;
    const int offsetTicks = (int) std::lround (s.analysis.offsetSeconds * s.analysis.bpm / 60.0 * tpq);

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / s.analysis.bpm)), 0.0);

    for (int b = 0; b < s.analysis.bars; ++b)
    {
        const auto& c = s.analysis.chords[(size_t) b];
        const double on = (double) (offsetTicks + b * barTicks);
        const double off = on + barTicks - 8;

        if (bass)
        {
            const int note = triad::bassMidiNote (c);
            seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 105), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, note), off);
        }
        else
        {
            for (int note : triad::chordMidiNotes (c))
            {
                seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 90), on);
                seq.addEvent (juce::MidiMessage::noteOff (1, note), off);
            }
        }
    }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PRODAP Sample to Chord");
    dir.createDirectory();
    const auto stem = juce::File::createLegalFileName (juce::File (s.fileName).getFileNameWithoutExtension());
    const auto out = dir.getChildFile (stem + (bass ? "_bass.mid" : "_chords.mid"));
    out.deleteFile();

    juce::FileOutputStream os (out);
    if (! os.openedOk() || ! mf.writeTo (os))
        return {};

    return out;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriadProcessor();
}
