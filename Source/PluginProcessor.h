#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include "Analysis.h"
#include "InstrumentBank.h"

class TriadProcessor : public juce::AudioProcessor
{
public:
    enum class State { empty, analyzing, ready, failed };

    struct Snapshot
    {
        State state = State::empty;
        juce::String fileName;
        juce::String message;
        triad::Analysis analysis;
    };

    TriadProcessor();
    ~TriadProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- UI-facing API (message thread) ----
    void loadSample (const juce::File&);
    void clearSample();
    void rescaleBpm (double factor);
    void setChord (int bar, triad::Chord chord);
    void setInstrument (int index);
    Snapshot getSnapshot() const;
    int getVersion() const { return version.load(); }

    // 0 = stopped, 1 = sample, 2 = chords, 3 = sample + chords
    int getPlayMode() const { return requestedMode.load(); }
    void setPlayMode (int m) { requestedMode.store (m); }
    double getPlayPositionSeconds() const { return uiPosition.load(); }

    juce::File writeMidi (bool bass);

    juce::AudioProcessorValueTreeState apvts;

private:
    struct PreviewData
    {
        std::shared_ptr<const juce::AudioBuffer<float>> sample; // stereo, 44.1 kHz
        juce::AudioBuffer<float> chords, bass;                  // mono, 44.1 kHz
        int lengthSamples = 0;
    };

    class Job;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::unique_ptr<PreviewData> makePreview (const triad::Analysis&,
                                              std::shared_ptr<const juce::AudioBuffer<float>>);

    void startJob (std::function<void (juce::Thread&)>);
    void stopJob();
    void publish (std::unique_ptr<PreviewData>, std::shared_ptr<const triad::Features>,
                  std::shared_ptr<const juce::AudioBuffer<float>>, const triad::Analysis&);
    void fail (const juce::String& message);

    juce::AudioFormatManager formats;
    std::unique_ptr<Job> job;

    mutable juce::CriticalSection snapLock;
    Snapshot snap;
    std::shared_ptr<const triad::Features> features;
    std::shared_ptr<const juce::AudioBuffer<float>> sample44;

    juce::SpinLock audioLock;
    std::unique_ptr<PreviewData> preview;

    std::atomic<int> version { 0 };
    std::atomic<int> requestedMode { 0 };
    std::atomic<double> uiPosition { 0.0 };

    std::atomic<float>* pMaster = nullptr;
    std::atomic<float>* pSample = nullptr;
    std::atomic<float>* pChord = nullptr;
    std::atomic<float>* pBass = nullptr;
    std::atomic<float>* pInstrument = nullptr;
    triad::InstrumentBank bank;

    double playPos = 0.0;
    int lastMode = 0;
    int renderMode = 0;
    float env = 0.0f;
    bool gainsInit = false;
    float gM = 0.0f, gS = 0.0f, gC = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriadProcessor)
};
