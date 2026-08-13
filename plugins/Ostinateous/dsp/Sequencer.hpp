/*
 * Ostinateous - maps host transport position to the pattern's own tick grid and
 * schedules sample-accurate note-on/off events for one audio block. Pure
 * logic, no DPF dependency (HostTransport mirrors DPF's TimePosition::BarBeatTick
 * field-for-field so copying from a real one at the call site is trivial),
 * so this is standalone-testable exactly like the rest of this pattern's dsp/.
 *
 * Five things this deliberately gets right, since they're the classic bug
 * class for hand-rolled tempo-synced MIDI generators:
 *  - Absolute position is derived from the host's raw sample frame counter
 *    (HostTransport::frame) and the current tempo, NOT from bar/beat/tick.
 *    bar/beat/tick is a much richer, harder-to-implement piece of a host's
 *    transport than a plain frame counter, and simple/minimal hosts (Carla's
 *    own built-in transport, when not driven by a real DAW timeline, is a
 *    real example that was hit and reproduced) may not track a genuinely
 *    incrementing bar count at all - e.g. reporting a fixed bar with beat/
 *    tick just cycling as a metronome reference. Treating that as absolute
 *    position wraps our WHOLE pattern back to its own start every one of
 *    the host's fake short "bars" instead of once per real pattern loop,
 *    which looks exactly like a note retriggering rapidly and repeatedly -
 *    that was a real, reported bug, reproduced in scratchpad
 *    test_carla_transport.cpp. `frame` is a far more fundamental quantity
 *    every host needs for basic audio processing regardless of how complete
 *    its timeline/arrangement support is, so it's a much safer foundation.
 *  - The host's frame position is read fresh every block and used to detect
 *    transport discontinuities (seek, loop, stop) and to correct for tempo
 *    changes - but it is NOT used directly as the scheduling window's start
 *    every block. Real hosts can report position with a bit of block-to-
 *    block rounding noise (buffer-size/tempo-map quantization), and
 *    re-anchoring the window to that raw value every single call turns the
 *    noise into real gaps between consecutive windows: a note whose tick
 *    lands in one of those gaps silently never gets scheduled (this was a
 *    real, reproduced bug - see scratchpad test_dropout.cpp in the session
 *    that found it). Instead, once playback is continuous (no discontinuity
 *    this block), the window picks up exactly where the last block's window
 *    ended (fExpectedNextTick), and only the window's far edge is anchored to
 *    the host's fresh position - self-correcting for tempo drift without
 *    ever leaving a gap. See process() for the exact math.
 *  - Transport discontinuities (seek, loop, stop) are detected by comparing
 *    this block's computed position against what last block's end predicted;
 *    on a mismatch (or on stop), every currently-sounding note gets forced
 *    off before anything else plays, so nothing ever hangs.
 *  - Notes longer than one audio block are tracked across blocks (a pending
 *    note-off list, not just "did this block start it") - drum hits are
 *    usually short, but nothing here assumes that; a note drawn as a full
 *    bar long gets its note-off exactly where it belongs, many blocks later.
 *  - An optional loop range (Pattern::loopEnabled/loopStartTick/loopEndTick)
 *    can restrict playback to a sub-range of the pattern instead of the
 *    whole thing - same gap-free scheduling applies, just with a shorter
 *    wraparound period and a different absolute base offset.
 */

#pragma once

#include "Pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace ostinateous {

// mirrors DPF's TimePosition shape closely enough that copying from a real
// one at the call site is trivial. bar/beat/tick are kept for potential
// future display use only - position math is deliberately driven by `frame`
// instead (see the class comment for why).
struct HostTransport
{
    bool playing = false;
    uint64_t frame = 0;      // absolute host transport position, in samples
    bool bbtValid = false;
    int32_t bar = 1;         // 1-based - informational only, NOT used for position
    int32_t beat = 1;        // 1-based, 1..beatsPerBar - informational only
    double tick = 0.0;       // 0..ticksPerBeat within the current beat - informational only
    float beatsPerBar = 4.0f;
    float beatType = 4.0f;   // denominator, e.g. 4 = quarter-note beats
    double ticksPerBeat = 960.0;
    double beatsPerMinute = 120.0; // only trusted when bbtValid (see BarBeatTick's own docs)
};

struct SchedEvent
{
    uint32_t frameOffset; // within this block, 0..frameCount-1
    bool noteOn;
    uint8_t note;
    uint8_t velocity;
};

// elapsed quarter notes since the host transport's frame 0, at the given
// tempo - a tempo-relative absolute clock built from the one position
// quantity essentially every host gets right, which is then mapped into the
// PATTERN's own tick grid (which may have a completely different time
// signature than whatever the host's arrangement uses) via kTicksPerQuarter.
inline double hostElapsedQuarterNotes(const HostTransport& t, double sampleRate) noexcept
{
    const double bpm = t.beatsPerMinute > 0.0 ? t.beatsPerMinute : 120.0;
    const double seconds = sampleRate > 0.0 ? (double)t.frame / sampleRate : 0.0;
    return seconds * (bpm / 60.0);
}

class Sequencer
{
public:
    std::vector<SchedEvent> process(const Pattern& pattern, const HostTransport& t,
                                     double sampleRate, uint32_t frameCount) noexcept
    {
        std::vector<SchedEvent> events;
        syncRowCache(pattern);

        // note: no bbtValid check here on purpose - position comes from
        // t.frame (see class comment), which every host needs for basic
        // audio processing regardless of whether it implements a genuine
        // bar/beat/tick timeline. beatsPerMinute is only trusted by the
        // caller when bbtValid is true (see HostTransport's own comment).
        if (!t.playing)
        {
            appendAllNotesOff(events);
            fWasPlaying = false;
            fPendingOffs.clear();
            return events;
        }

        const uint32_t totalTicks = pattern.totalTicks();
        if (totalTicks == 0 || frameCount == 0)
            return events;

        // active loop window: [loopBase, loopBase+loopLen) within the full
        // pattern, or the whole pattern when no valid range is set/enabled.
        uint32_t loopBase = 0, loopLen = totalTicks;
        if (pattern.loopEnabled && pattern.loopEndTick > pattern.loopStartTick
            && pattern.loopEndTick <= totalTicks)
        {
            loopBase = pattern.loopStartTick;
            loopLen = pattern.loopEndTick - pattern.loopStartTick;
        }
        const uint32_t loopEndAbs = loopBase + loopLen;

        const double quarters = hostElapsedQuarterNotes(t, sampleRate);
        const double patternTickPos = quarters * (double)kTicksPerQuarter;
        const double relWrapped = wrapInto(patternTickPos - (double)loopBase, (double)loopLen);
        const double startTick = (double)loopBase + relWrapped;

        const double secondsPerQuarter = 60.0 / (t.beatsPerMinute > 0.0 ? t.beatsPerMinute : 120.0);
        const double ticksPerSecond = (double)kTicksPerQuarter / secondsPerQuarter;
        const double blockTicks = ticksPerSecond * ((double)frameCount / sampleRate);

        const bool discontinuity = !fWasPlaying
            || std::fabs(wrapDiff(startTick, fExpectedNextTick, loopLen)) > (double)pattern.ticksPerBeat() * 0.05;

        if (discontinuity)
        {
            appendAllNotesOff(events);
            fPendingOffs.clear();
        }

        // Schedule from where we actually left off (fExpectedNextTick) rather
        // than from the host's freshly-reported position, whenever the two
        // roughly agree (see the class comment) - this is what makes the
        // union of every block's window gap-free. The far edge still tracks
        // the host's fresh estimate, so tempo changes take effect immediately
        // and any (rare, sub-tick) slack is a harmless double-covered sliver
        // rather than a skipped one.
        double schedStart = discontinuity ? startTick : fExpectedNextTick;
        const double schedEnd = startTick + blockTicks;
        double schedLen = wrapInto(schedEnd - schedStart, (double)loopLen);

        // Safety net: `frame` is supposed to be monotonic but DPF's own docs
        // warn some hosts don't guarantee it. If schedStart and the host's
        // fresh position have drifted apart by an unreasonable amount (way
        // more than one block's worth), trying to "catch up" would schedule
        // a huge window and re-fire everything in it - a burst of duplicate
        // notes, not a burst of missing ones, but just as wrong. Treat that
        // case like a discontinuity instead: accept a (harmless, rare) small
        // gap rather than a runaway window.
        if (schedLen > blockTicks * 8.0 + (double)pattern.ticksPerBeat())
        {
            schedStart = startTick;
            schedLen = wrapInto(schedEnd - schedStart, (double)loopLen);
        }

        // pending note-offs from notes started in earlier blocks: emit any
        // whose tick falls inside this block's window before scheduling new ons
        emitDueOffs(events, schedStart, schedLen, loopLen, sampleRate, frameCount);

        // split the block's window at the loop wraparound (at most once per
        // block for any sane buffer size/tempo) and schedule new note-ons
        double windowStart = schedStart;
        double windowLen = schedLen;
        while (windowLen > 0.0)
        {
            const double thisLen = std::min(windowLen, (double)loopEndAbs - windowStart);
            scheduleNoteOns(pattern, events, windowStart, thisLen, schedStart, schedLen,
                            loopLen, loopEndAbs, sampleRate, frameCount);
            windowLen -= thisLen;
            windowStart = (double)loopBase;
        }

        std::stable_sort(events.begin(), events.end(),
                          [](const SchedEvent& a, const SchedEvent& b) { return a.frameOffset < b.frameOffset; });

        fExpectedNextTick = (double)loopBase + wrapInto(startTick + blockTicks - loopBase, (double)loopLen);
        fLastStartTick = startTick;
        fWasPlaying = true;
        return events;
    }

    // last computed absolute pattern tick (0..totalTicks), for UI playhead
    // display; only meaningful while playing (caller tracks that itself).
    double lastPatternTick() const noexcept { return fLastStartTick; }

    std::vector<SchedEvent> allNotesOff() noexcept
    {
        std::vector<SchedEvent> events;
        appendAllNotesOff(events);
        fPendingOffs.clear();
        fWasPlaying = false;
        return events;
    }

private:
    struct PendingOff { uint32_t row; double tick; };

    void syncRowCache(const Pattern& pattern)
    {
        if (fRowNote.size() != pattern.rows.size())
        {
            fRowNote.resize(pattern.rows.size());
            fSounding.assign(pattern.rows.size(), false);
        }
        for (size_t i = 0; i < pattern.rows.size(); ++i)
            fRowNote[i] = pattern.rows[i].midiNote;
    }

    void appendAllNotesOff(std::vector<SchedEvent>& events)
    {
        for (size_t row = 0; row < fSounding.size(); ++row)
        {
            if (fSounding[row])
            {
                events.push_back({ 0, false, fRowNote[row], 0 });
                fSounding[row] = false;
            }
        }
    }

    // frame offset for an absolute pattern tick known to fall inside
    // [blockStartTick, blockStartTick+blockTicks) (mod totalTicks)
    static uint32_t tickToFrame(double tick, double blockStartTick, double blockTicks,
                                 uint32_t totalTicks, uint32_t frameCount)
    {
        const double intoBlock = std::fmod(tick - blockStartTick + totalTicks, (double)totalTicks);
        const double frac = blockTicks > 0.0 ? intoBlock / blockTicks : 0.0;
        uint32_t frame = (uint32_t)(frac * (double)frameCount);
        if (frame >= frameCount)
            frame = frameCount > 0 ? frameCount - 1 : 0;
        return frame;
    }

    static bool tickInWindow(double tick, double blockStartTick, double blockTicks, uint32_t totalTicks)
    {
        // is `tick` within blockTicks ticks after blockStartTick, wrapping mod totalTicks?
        const double d = std::fmod(tick - blockStartTick + totalTicks, (double)totalTicks);
        return d >= 0.0 && d < blockTicks;
    }

    void emitDueOffs(std::vector<SchedEvent>& events, double blockStartTick, double blockTicks,
                      uint32_t totalTicks, double, uint32_t frameCount)
    {
        for (size_t i = 0; i < fPendingOffs.size(); )
        {
            if (tickInWindow(fPendingOffs[i].tick, blockStartTick, blockTicks, totalTicks))
            {
                const uint32_t row = fPendingOffs[i].row;
                events.push_back({ tickToFrame(fPendingOffs[i].tick, blockStartTick, blockTicks, totalTicks, frameCount),
                                    false, fRowNote[row], 0 });
                fSounding[row] = false;
                fPendingOffs.erase(fPendingOffs.begin() + (long)i);
            }
            else
            {
                ++i;
            }
        }
    }

    void scheduleNoteOns(const Pattern& pattern, std::vector<SchedEvent>& events,
                          double winStart, double winLen,
                          double blockStartTick, double blockTicks, uint32_t loopLen, uint32_t loopEndAbs,
                          double sampleRate, uint32_t frameCount)
    {
        (void)sampleRate;
        for (const Note& n : pattern.notes)
        {
            if (n.rowIndex >= pattern.rows.size())
                continue;
            if (!rowAudible(pattern.rows, n.rowIndex))
                continue; // muted, or another row is soloed and this one isn't
            if ((double)n.startTick < winStart || (double)n.startTick >= winStart + winLen)
                continue;

            const uint8_t note = fRowNote[n.rowIndex];
            const uint32_t onFrame = tickToFrame((double)n.startTick, blockStartTick, blockTicks, loopLen, frameCount);
            events.push_back({ onFrame, true, note, n.velocity });
            fSounding[n.rowIndex] = true;

            // clamp so a note never wraps past the active loop's end point -
            // simplest correct-enough behavior for v1, avoids a second
            // wraparound case (and keeps a note from bleeding past a loop
            // range boundary into ticks that never get visited)
            const double maxLen = (double)loopEndAbs - (double)n.startTick;
            const double offTick = (double)n.startTick + std::min((double)n.lengthTick, std::max(1.0, maxLen));

            if (tickInWindow(offTick, blockStartTick, blockTicks, loopLen))
            {
                uint32_t offFrame = tickToFrame(offTick, blockStartTick, blockTicks, loopLen, frameCount);
                if (offFrame < onFrame)
                    offFrame = onFrame; // keep ordering sane if rounding put it earlier
                events.push_back({ offFrame, false, note, 0 });
                fSounding[n.rowIndex] = false;
            }
            else
            {
                fPendingOffs.push_back({ n.rowIndex, offTick });
            }
        }
    }

    static double wrapDiff(double a, double b, double period) noexcept
    {
        double d = a - b;
        d = std::fmod(d + period * 1.5, period) - period * 0.5;
        return d;
    }

    // forward distance from 0, i.e. `x` wrapped into [0, period) - used to
    // turn a possibly-negative or possibly-huge raw tick difference into the
    // short forward span it actually represents once a wraparound is allowed.
    static double wrapInto(double x, double period) noexcept
    {
        if (period <= 0.0)
            return 0.0;
        double d = std::fmod(x, period);
        if (d < 0.0)
            d += period;
        return d;
    }

    bool fWasPlaying = false;
    double fExpectedNextTick = 0.0;
    double fLastStartTick = 0.0;
    std::vector<bool> fSounding;
    std::vector<uint8_t> fRowNote;
    std::vector<PendingOff> fPendingOffs;
};

} // namespace ostinateous
