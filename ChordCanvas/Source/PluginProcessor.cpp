#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct Sound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// 倍音を重ねたシンプルな音(ピアノ/ギターで減衰を変える)
struct Voice : juce::SynthesiserVoice
{
    explicit Voice (const std::atomic<bool>& g) : guitar (g) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<Sound*> (s) != nullptr; }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        freq = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        phase = 0.0;
        level = velocity;
        const bool g = guitar.load();
        env.setSampleRate (getSampleRate());
        env.setParameters (g ? juce::ADSR::Parameters { 0.002f, 0.9f, 0.12f, 0.25f }
                             : juce::ADSR::Parameters { 0.005f, 1.6f, 0.35f, 0.45f });
        env.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) env.noteOff();
        else { env.reset(); clearCurrentNote(); }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! env.isActive()) { clearCurrentNote(); return; }

        const double inc = juce::MathConstants<double>::twoPi * freq / getSampleRate();
        const bool g = guitar.load();
        for (int i = 0; i < num; ++i)
        {
            const float p = (float) phase;
            float s = std::sin (p) + 0.5f * std::sin (2 * p) + 0.25f * std::sin (3 * p);
            s += g ? 0.35f * std::sin (4 * p) + 0.2f * std::sin (5 * p) : 0.1f * std::sin (4 * p);
            s *= 0.18f * level * env.getNextSample();

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, start + i, s);

            phase += inc;
            if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
            if (! env.isActive()) { clearCurrentNote(); break; }
        }
    }

private:
    const std::atomic<bool>& guitar;
    juce::ADSR env;
    double freq = 440.0, phase = 0.0;
    float level = 0.0f;
};
} // namespace

ChordCanvasProcessor::ChordCanvasProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    synth.addSound (new Sound());
    for (int i = 0; i < 24; ++i)
        synth.addVoice (new Voice (guitarMode));

    slots = { { 0, 0, 4.0f }, { 7, 0, 4.0f }, { 9, 1, 4.0f }, { 5, 0, 4.0f } }; // C G Am F
}

void ChordCanvasProcessor::prepareToPlay (double sr, int)
{
    synth.setCurrentPlaybackSampleRate (sr);
    const juce::ScopedLock sl (lock);
    pending.clear();
    sounding.clear();
    wasPlaying = false;
}

bool ChordCanvasProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

std::vector<chord::Slot> ChordCanvasProcessor::getSlots() const
{
    const juce::ScopedLock sl (lock);
    return slots;
}

void ChordCanvasProcessor::setSlots (std::vector<chord::Slot> s)
{
    const juce::ScopedLock sl (lock);
    slots = std::move (s);
    ++revision;
}

double ChordCanvasProcessor::totalBeatsLocked() const
{
    double t = 0.0;
    for (auto& s : slots) t += s.beats;
    return t;
}

// slot == nullptr なら鳴っている音を止めるだけ
void ChordCanvasProcessor::triggerLocked (const chord::Slot* slot, juce::int64 atTime)
{
    for (int n : sounding)
        pending.push_back ({ atTime, juce::MidiMessage::noteOff (1, n) });
    sounding.clear();

    if (slot == nullptr) return;

    const auto notes = chord::voice (*slot, guitarMode.load());
    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const auto strum = (juce::int64) (strumMs.load() * 0.001 * sr);
    juce::int64 k = 0;
    for (int n : notes)
    {
        pending.push_back ({ atTime + k * strum, juce::MidiMessage::noteOn (1, n, (juce::uint8) 96) });
        sounding.push_back (n);
        ++k;
    }
}

void ChordCanvasProcessor::auditionSlot (const chord::Slot& s)
{
    const juce::ScopedLock sl (lock);
    triggerLocked (&s, sampleClock);
}

void ChordCanvasProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();

    {
        const juce::ScopedTryLock sl (lock);
        if (sl.isLocked())
        {
            bool playing = false;
            double ppq = 0.0, bpm = 120.0;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                {
                    if (auto b = pos->getBpm()) bpm = *b;
                    if (auto p = pos->getPpqPosition()) { ppq = *p; playing = pos->getIsPlaying(); }
                }
            lastBpm = bpm;

            const double total = totalBeatsLocked();
            const double sr = getSampleRate();

            if (playing && total > 0.0 && sr > 0.0)
            {
                const double beatsPerSample = bpm / 60.0 / sr;
                const double blockBeats = n * beatsPerSample;
                double posStart = std::fmod (ppq, total);
                if (posStart < 0.0) posStart += total;

                const bool jump = ! wasPlaying || std::abs (ppq - expectedPpq) > 0.02;

                // 現在のスロット
                int cur = 0;
                {
                    double acc = 0.0;
                    for (size_t i = 0; i < slots.size(); ++i)
                    {
                        if (posStart < acc + slots[i].beats) { cur = (int) i; break; }
                        acc += slots[i].beats;
                    }
                }

                if (jump)
                {
                    pending.clear();
                    triggerLocked (&slots[(size_t) cur], sampleClock);
                }

                double s = 0.0;
                for (size_t i = 0; i < slots.size(); ++i)
                {
                    for (int k = 0; k < 2; ++k)
                    {
                        const double sb = s + k * total;
                        const bool inside = jump ? (sb > posStart) : (sb >= posStart);
                        if (inside && sb < posStart + blockBeats)
                            triggerLocked (&slots[i], sampleClock + (juce::int64) ((sb - posStart) / beatsPerSample));
                    }
                    s += slots[i].beats;
                }

                playingSlot = cur;
                expectedPpq = ppq + blockBeats;
            }
            else if (wasPlaying)
            {
                pending.clear();
                triggerLocked (nullptr, sampleClock);
                playingSlot = -1;
            }
            wasPlaying = playing;

            // 発音予定を MIDI バッファへ(時刻順)
            std::stable_sort (pending.begin(), pending.end(),
                              [] (const Pending& a, const Pending& b) { return a.time < b.time; });
            size_t used = 0;
            for (; used < pending.size() && pending[used].time < sampleClock + n; ++used)
                midi.addEvent (pending[used].msg, (int) juce::jmax<juce::int64> (0, pending[used].time - sampleClock));
            pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) used);
        }
    }

    synth.renderNextBlock (buffer, midi, 0, n);
    buffer.applyGain (gain.load());
    sampleClock += n;
}

juce::MidiFile ChordCanvasProcessor::buildMidiFile() const
{
    const int tpq = 960;
    const double bpm = lastBpm.load();
    const auto sl = getSlots();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (tpq);

    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)));
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
    file.addTrack (meta);

    const double strumTicks = strumMs.load() * 0.001 * bpm / 60.0 * tpq;
    juce::MidiMessageSequence seq;
    double t = 0.0;
    for (auto& s : sl)
    {
        const double end = t + s.beats * tpq;
        int k = 0;
        for (int note : chord::voice (s, guitarMode.load()))
        {
            auto on = juce::MidiMessage::noteOn (1, note, (juce::uint8) 96);
            on.setTimeStamp (t + k * strumTicks);
            auto off = juce::MidiMessage::noteOff (1, note);
            off.setTimeStamp (end - 4.0);
            seq.addEvent (on);
            seq.addEvent (off);
            ++k;
        }
        t = end;
    }
    seq.updateMatchedPairs();
    file.addTrack (seq);
    return file;
}

void ChordCanvasProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("ChordCanvas");
    xml.setAttribute ("guitar", guitarMode.load());
    xml.setAttribute ("strum", (double) strumMs.load());
    xml.setAttribute ("gain", (double) gain.load());
    for (auto& s : getSlots())
    {
        auto* e = xml.createNewChildElement ("SLOT");
        e->setAttribute ("root", s.root);
        e->setAttribute ("quality", s.quality);
        e->setAttribute ("beats", (double) s.beats);
    }
    copyXmlToBinary (xml, dest);
}

void ChordCanvasProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("ChordCanvas"))
        {
            guitarMode = xml->getBoolAttribute ("guitar", false);
            strumMs = (float) xml->getDoubleAttribute ("strum", 25.0);
            gain = (float) xml->getDoubleAttribute ("gain", 0.7);
            std::vector<chord::Slot> s;
            for (auto* e : xml->getChildWithTagNameIterator ("SLOT"))
                s.push_back ({ juce::jlimit (0, 11, e->getIntAttribute ("root")),
                               juce::jlimit (0, (int) chord::qualities().size() - 1, e->getIntAttribute ("quality")),
                               (float) juce::jmax (0.25, e->getDoubleAttribute ("beats", 4.0)) });
            setSlots (std::move (s));
        }
}

juce::AudioProcessorEditor* ChordCanvasProcessor::createEditor() { return new ChordCanvasEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordCanvasProcessor(); }
