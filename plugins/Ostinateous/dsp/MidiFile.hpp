/*
 * Ostinateous - hand-rolled Standard MIDI File (Format 0, single track) writer.
 * DPF-agnostic like the rest of dsp/, so it's standalone-testable the same
 * way (see scratchpad test files from the session that built this).
 *
 * Deliberately skips running-status (a stream-compression trick, not
 * required for a valid file - every importer, including Ardour, handles
 * non-running-status fine) to keep the encoder simple and obviously correct.
 */

#pragma once

#include "Pattern.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ostinateous {

namespace midifile_detail {

inline void writeU16BE(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)(v & 0xFF));
}

inline void writeU32BE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)(v & 0xFF));
}

// standard MIDI variable-length quantity: 7 bits per byte, high bit set on
// every byte except the last
inline void writeVLQ(std::vector<uint8_t>& out, uint32_t value)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = (uint8_t)(value & 0x7F);
    value >>= 7;
    while (value > 0)
    {
        buf[n++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (int i = n - 1; i >= 0; --i)
        out.push_back(buf[i]);
}

struct RawEvent
{
    uint32_t tick;
    int kind; // 0 = meta (tempo/timesig), 1 = note-off, 2 = note-on - tie-break order at the same tick
    std::vector<uint8_t> bytes; // event bytes, NOT including delta-time
};

} // namespace midifile_detail

// bpm: written into the tempo meta event; the pattern's own tick grid
// (kTicksPerQuarter) is used as the file's division, so no rescaling is
// needed anywhere else.
inline std::vector<uint8_t> writeStandardMidiFile(const Pattern& pattern, double bpm)
{
    using namespace midifile_detail;

    std::vector<RawEvent> events;

    // tempo meta: FF 51 03 <24-bit microseconds per quarter note>
    {
        const double mpq = bpm > 0.0 ? (60000000.0 / bpm) : 500000.0;
        const uint32_t mpqInt = (uint32_t)(mpq + 0.5);
        RawEvent e;
        e.tick = 0;
        e.kind = 0;
        e.bytes = { 0xFF, 0x51, 0x03,
                    (uint8_t)((mpqInt >> 16) & 0xFF),
                    (uint8_t)((mpqInt >> 8) & 0xFF),
                    (uint8_t)(mpqInt & 0xFF) };
        events.push_back(std::move(e));
    }

    // time signature meta: FF 58 04 <numerator> <log2(denominator)> <MIDI clocks/metronome click> <32nd-notes/quarter>
    {
        uint8_t denomLog2 = 2; // default: /4
        uint32_t d = pattern.timeSigDenom > 0 ? pattern.timeSigDenom : 4;
        denomLog2 = 0;
        while ((1u << denomLog2) < d && denomLog2 < 8)
            ++denomLog2;

        RawEvent e;
        e.tick = 0;
        e.kind = 0;
        e.bytes = { 0xFF, 0x58, 0x04,
                    (uint8_t)(pattern.timeSigNum > 0 ? pattern.timeSigNum : 4),
                    denomLog2, 24, 8 };
        events.push_back(std::move(e));
    }

    const uint32_t totalTicks = pattern.totalTicks();
    for (const Note& n : pattern.notes)
    {
        if (n.rowIndex >= pattern.rows.size())
            continue;
        const uint8_t note = pattern.rows[n.rowIndex].midiNote;
        const uint8_t vel = n.velocity > 0 ? n.velocity : 1;

        uint32_t offTick = n.startTick + n.lengthTick;
        if (offTick > totalTicks)
            offTick = totalTicks; // clamp so nothing writes past the pattern's own length
        if (offTick <= n.startTick)
            offTick = n.startTick + 1; // zero/negative length guard - always a real, audible note

        RawEvent on;
        on.tick = n.startTick;
        on.kind = 2;
        on.bytes = { 0x90, note, vel };
        events.push_back(std::move(on));

        RawEvent off;
        off.tick = offTick;
        off.kind = 1;
        off.bytes = { 0x80, note, 0 };
        events.push_back(std::move(off));
    }

    std::stable_sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b)
    {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return a.kind < b.kind; // meta, then note-off, then note-on at the same tick
    });

    std::vector<uint8_t> track;
    uint32_t lastTick = 0;
    for (const RawEvent& e : events)
    {
        writeVLQ(track, e.tick - lastTick);
        track.insert(track.end(), e.bytes.begin(), e.bytes.end());
        lastTick = e.tick;
    }
    // end of track: FF 2F 00, at the pattern's own end so the file's total
    // length matches one full loop exactly
    writeVLQ(track, totalTicks > lastTick ? totalTicks - lastTick : 0);
    track.push_back(0xFF); track.push_back(0x2F); track.push_back(0x00);

    std::vector<uint8_t> out;
    out.reserve(14 + 8 + track.size());

    out.insert(out.end(), { 'M', 'T', 'h', 'd' });
    writeU32BE(out, 6);
    writeU16BE(out, 0); // format 0
    writeU16BE(out, 1); // one track
    writeU16BE(out, (uint16_t)kTicksPerQuarter); // division = ticks per quarter note

    out.insert(out.end(), { 'M', 'T', 'r', 'k' });
    writeU32BE(out, (uint32_t)track.size());
    out.insert(out.end(), track.begin(), track.end());

    return out;
}

} // namespace ostinateous
