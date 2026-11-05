#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "ChordEngine.h"

class ChordCanvasProcessor : public juce::AudioProcessor
{
public:
    ChordCanvasProcessor();
    ~ChordCanvasProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ChordCanvas"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- エディタ向け API ---
    std::vector<chord::Slot> getSlots() const;
    void setSlots (std::vector<chord::Slot> s);
    int getRevision() const { return revision.load(); }
    void auditionSlot (const chord::Slot& s);
    juce::MidiFile buildMidiFile() const;

    std::atomic<bool>  guitarMode { false };
    std::atomic<float> strumMs { 25.0f };
    std::atomic<float> gain { 0.7f };
    std::atomic<int>   playingSlot { -1 };
    std::atomic<double> lastBpm { 120.0 };

private:
    struct Pending { juce::int64 time; juce::MidiMessage msg; };

    double totalBeatsLocked() const;
    void triggerLocked (const chord::Slot* slot, juce::int64 atTime);

    mutable juce::CriticalSection lock;
    std::vector<chord::Slot> slots;
    std::atomic<int> revision { 0 };

    juce::Synthesiser synth;
    std::vector<Pending> pending;
    std::vector<int> sounding;
    juce::int64 sampleClock = 0;
    bool wasPlaying = false;
    double expectedPpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordCanvasProcessor)
};
