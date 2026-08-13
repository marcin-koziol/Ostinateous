/*
 * Ostinateous - tempo-synced MIDI drum pattern sequencer. Zero audio ports, MIDI
 * output only (same DPF shape as the bundled dpf/examples/MidiThrough). All
 * the actual sync/scheduling logic lives in dsp/Sequencer.hpp (DPF-agnostic,
 * standalone-tested); this file is just the DPF glue: read host transport,
 * hand it to the Sequencer, turn the resulting events into MidiEvents.
 *
 * The full pattern (rows, notes, bar count, time signature) is edited on the
 * UI thread and played back on the audio thread every block - unlike the
 * rest of the Sideous family, where DPF's parameter automation already
 * handles UI<->DSP thread safety for single float values, there's no
 * existing pattern in this family for "UI mutates a large custom structure,
 * audio reads it." fPattern is a std::shared_ptr<const Pattern> swapped via
 * std::atomic_load/store: the UI (via setState(), which the host or our own
 * UI can call) builds a whole new Pattern and publishes it atomically; the
 * audio thread always sees one consistent, complete snapshot, never a
 * partial edit.
 */

#include "DistrhoPlugin.hpp"
#include "Params.hpp"
#include "dsp/Pattern.hpp"
#include "dsp/Sequencer.hpp"

#include <cstring>
#include <memory>

START_NAMESPACE_DISTRHO

using namespace ostinateous;

// -----------------------------------------------------------------------------------------------------------

class OstinateousPlugin : public Plugin
{
public:
    OstinateousPlugin()
        : Plugin(kParamCount, 0, 1) // 3 output-only params (transport feedback), no programs, 1 state key ("pattern")
    {
        fPattern = std::make_shared<const Pattern>();
    }

protected:
    // ---------------------------------------------------------------------
    // Information

    const char* getLabel() const override { return "Ostinateous"; }
    const char* getDescription() const override
    {
        return "Tempo-synced MIDI drum pattern sequencer with a piano-roll "
               "grid editor, bar/time-signature control, and fill generators.";
    }
    const char* getMaker() const override { return "Ostinateous"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    // ---------------------------------------------------------------------
    // Init

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsOutput;
        switch (index)
        {
        case kParamOutPlayheadFrac:
            parameter.name = "Playhead";
            parameter.symbol = "playhead_frac";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamOutPlaying:
            parameter.name = "Playing";
            parameter.symbol = "playing";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamOutBpm:
            parameter.name = "Tempo";
            parameter.symbol = "bpm";
            parameter.unit = "BPM";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 999.0f;
            parameter.ranges.def = 120.0f;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index != 0)
            return;

        state.hints = 0; // not host-exposed as a generic text parameter - our
                          // own UI is the only intended editor
        state.key = "pattern";
        state.defaultValue = Pattern().serialize().c_str();
        state.label = "Pattern";
        state.description = "The whole sequenced pattern: rows, notes, bar count, time signature.";
    }

    // ---------------------------------------------------------------------
    // Internal data

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamOutPlayheadFrac: return fOutPlayheadFrac;
        case kParamOutPlaying:      return fOutPlaying;
        case kParamOutBpm:          return fOutBpm;
        default:                    return 0.0f;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "pattern") == 0)
        {
            const std::shared_ptr<const Pattern> pattern = std::atomic_load(&fPattern);
            return String(pattern->serialize().c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "pattern") == 0)
        {
            std::shared_ptr<const Pattern> newPattern = std::make_shared<const Pattern>(Pattern::deserialize(value));
            std::atomic_store(&fPattern, newPattern);
        }
    }

    // ---------------------------------------------------------------------
    // Audio/MIDI Processing

    void activate() override
    {
        fSequencer = Sequencer();
    }

    void run(const float**, float**, uint32_t frames) override
    {
        const std::shared_ptr<const Pattern> pattern = std::atomic_load(&fPattern);

        const TimePosition& pos = getTimePosition();
        HostTransport t;
        t.playing = pos.playing;
        t.frame = pos.frame;
        t.bbtValid = pos.bbt.valid;
        // beatsPerMinute (and the rest of bbt) is only meaningful when
        // bbt.valid - per TimePosition::BarBeatTick's own docs, "if false
        // you must not read from this struct". Position itself comes from
        // t.frame regardless (see Sequencer.hpp's class comment for why),
        // so all we need from bbt when it's absent/untrustworthy is a sane
        // tempo fallback.
        if (pos.bbt.valid)
        {
            t.bar = pos.bbt.bar;
            t.beat = pos.bbt.beat;
            t.tick = pos.bbt.tick;
            t.beatsPerBar = pos.bbt.beatsPerBar;
            t.beatType = pos.bbt.beatType;
            t.ticksPerBeat = pos.bbt.ticksPerBeat;
            t.beatsPerMinute = pos.bbt.beatsPerMinute > 0.0 ? pos.bbt.beatsPerMinute : 120.0;
        }
        else
        {
            t.beatsPerMinute = 120.0;
        }

        const std::vector<SchedEvent> events = fSequencer.process(*pattern, t, getSampleRate(), frames);

        for (const SchedEvent& e : events)
        {
            MidiEvent ev;
            ev.frame = e.frameOffset;
            ev.size = 3;
            ev.data[0] = e.noteOn ? 0x90 : 0x80;
            ev.data[1] = e.note;
            ev.data[2] = e.velocity;
            writeMidiEvent(ev);
        }

        fOutPlaying = (t.playing && t.bbtValid) ? 1.0f : 0.0f;
        fOutBpm = (float)t.beatsPerMinute;
        const uint32_t totalTicks = pattern->totalTicks();
        fOutPlayheadFrac = totalTicks > 0 ? (float)(fSequencer.lastPatternTick() / (double)totalTicks) : 0.0f;
    }

private:
    std::shared_ptr<const Pattern> fPattern;
    Sequencer fSequencer;

    float fOutPlayheadFrac = 0.0f;
    float fOutPlaying = 0.0f;
    float fOutBpm = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstinateousPlugin)
};

// -----------------------------------------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new OstinateousPlugin();
}

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
