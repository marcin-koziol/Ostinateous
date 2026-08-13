/*
 * Ostinateous - the pattern data model: rows (instruments), notes, bar count and
 * time signature. Deliberately DPF-agnostic (no DistrhoPlugin.hpp dependency)
 * so it's plain, host-independent data plus pure functions, testable with a
 * standalone g++ compile - same philosophy as the Sideous family's dsp/ code.
 *
 * Tick-based storage (960 ticks/quarter, the common SMF convention) rather
 * than step-indexed: "snap to grid" is just rounding at edit/insert time, not
 * a storage constraint, so free positioning with snap off falls out for free.
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ostinateous {

static constexpr uint32_t kTicksPerQuarter = 960;

struct Row
{
    std::string name;
    uint8_t midiNote;
    bool muted = false;
    bool soloed = false;
};

struct Note
{
    uint32_t rowIndex;
    uint32_t startTick;  // absolute tick within the pattern, 0-based
    uint32_t lengthTick;
    uint8_t velocity;    // 1..127
};

// default 12-voice layout, mirrors sideous-drums's DrumMap.hpp note map
// (kick=36, snare=38, ...) so a pattern authored here already matches that
// plugin's fixed mapping out of the box.
inline const std::vector<Row>& defaultRows()
{
    static const std::vector<Row> rows = {
        { "Kick",       36 },
        { "Snare",      38 },
        { "Hat Closed", 42 },
        { "Hat Open",   46 },
        { "Tom Low",    41 },
        { "Tom Mid",    45 },
        { "Tom High",   48 },
        { "Crash",      49 },
        { "Ride",       51 },
        { "Rimshot",    37 },
        { "Clap",       39 },
        { "Maracas",    70 },
    };
    return rows;
}

// true if any row has soloed set - while true, only soloed rows are
// audible, same convention as any mixer's mute/solo interaction
inline bool anyRowSoloed(const std::vector<Row>& rows) noexcept
{
    for (const Row& r : rows)
        if (r.soloed)
            return true;
    return false;
}

inline bool rowAudible(const std::vector<Row>& rows, uint32_t rowIndex) noexcept
{
    if (rowIndex >= rows.size())
        return false;
    const Row& r = rows[rowIndex];
    if (r.muted)
        return false;
    if (anyRowSoloed(rows) && !r.soloed)
        return false;
    return true;
}

struct Pattern
{
    std::vector<Row> rows = defaultRows();
    std::vector<Note> notes;
    uint32_t barCount = 4;
    uint32_t timeSigNum = 4;
    uint32_t timeSigDenom = 4;

    // an optional sub-range of the pattern to loop over instead of the whole
    // thing (e.g. auditioning just a fill) - independent of host transport
    // looping, which this plugin has no control over. loopEndTick <=
    // loopStartTick (the default) means "no range set"; the Sequencer falls
    // back to looping the full pattern whenever that's true, whether or not
    // loopEnabled is set, so toggling the button on before ever dragging a
    // range is a safe no-op rather than undefined behavior.
    bool loopEnabled = false;
    uint32_t loopStartTick = 0;
    uint32_t loopEndTick = 0;

    // ticks in one "beat" (one timeSigDenom-th note) at this pattern's time signature
    uint32_t ticksPerBeat() const noexcept
    {
        return kTicksPerQuarter * 4 / (timeSigDenom > 0 ? timeSigDenom : 4);
    }
    uint32_t ticksPerBar() const noexcept { return ticksPerBeat() * (timeSigNum > 0 ? timeSigNum : 4); }
    uint32_t totalTicks() const noexcept { return ticksPerBar() * (barCount > 0 ? barCount : 1); }

    // plain-text "key=value" + one "note=..." line per note - same spirit as
    // the Sideous family's PresetStore.hpp symbol=value format, just with a
    // repeated line kind instead of one-line-per-param. Used both for DPF
    // state (host session save/reload) and the on-disk pattern library.
    std::string serialize() const
    {
        std::string out;
        char buf[256];

        std::snprintf(buf, sizeof(buf), "bar_count=%u\ntime_sig_num=%u\ntime_sig_denom=%u\n",
                      barCount, timeSigNum, timeSigDenom);
        out += buf;

        std::snprintf(buf, sizeof(buf), "loop_enabled=%u\nloop_start=%u\nloop_end=%u\n",
                      loopEnabled ? 1u : 0u, loopStartTick, loopEndTick);
        out += buf;

        for (size_t i = 0; i < rows.size(); ++i)
        {
            std::snprintf(buf, sizeof(buf), "row=%u,%u,%u,%s\n", (unsigned)rows[i].midiNote,
                          rows[i].muted ? 1u : 0u, rows[i].soloed ? 1u : 0u, rows[i].name.c_str());
            out += buf;
        }

        for (const Note& n : notes)
        {
            std::snprintf(buf, sizeof(buf), "note=%u,%u,%u,%u\n",
                          n.rowIndex, n.startTick, n.lengthTick, (unsigned)n.velocity);
            out += buf;
        }

        return out;
    }

    static Pattern deserialize(const char* text)
    {
        Pattern p;
        p.rows.clear(); // rows are fully specified in the text; only fall back to defaults if none were found

        if (text == nullptr)
        {
            p.rows = defaultRows();
            return p;
        }

        const char* line = text;
        while (*line != '\0')
        {
            const char* nl = std::strchr(line, '\n');
            const size_t len = nl != nullptr ? (size_t)(nl - line) : std::strlen(line);

            if (len > 0)
            {
                std::string entry(line, len);

                if (entry.rfind("bar_count=", 0) == 0)
                    p.barCount = (uint32_t)std::strtoul(entry.c_str() + 10, nullptr, 10);
                else if (entry.rfind("time_sig_num=", 0) == 0)
                    p.timeSigNum = (uint32_t)std::strtoul(entry.c_str() + 13, nullptr, 10);
                else if (entry.rfind("time_sig_denom=", 0) == 0)
                    p.timeSigDenom = (uint32_t)std::strtoul(entry.c_str() + 15, nullptr, 10);
                else if (entry.rfind("loop_enabled=", 0) == 0)
                    p.loopEnabled = std::strtoul(entry.c_str() + 13, nullptr, 10) != 0;
                else if (entry.rfind("loop_start=", 0) == 0)
                    p.loopStartTick = (uint32_t)std::strtoul(entry.c_str() + 11, nullptr, 10);
                else if (entry.rfind("loop_end=", 0) == 0)
                    p.loopEndTick = (uint32_t)std::strtoul(entry.c_str() + 9, nullptr, 10);
                else if (entry.rfind("row=", 0) == 0)
                {
                    // row=<note>,<muted>,<soloed>,<name>
                    const char* rest = entry.c_str() + 4;
                    unsigned note = 0, muted = 0, soloed = 0;
                    int nameOffset = 0;
                    if (std::sscanf(rest, "%u,%u,%u,%n", &note, &muted, &soloed, &nameOffset) >= 3 && nameOffset > 0)
                    {
                        Row r;
                        r.midiNote = (uint8_t)note;
                        r.muted = muted != 0;
                        r.soloed = soloed != 0;
                        r.name = rest + nameOffset;
                        p.rows.push_back(r);
                    }
                }
                else if (entry.rfind("note=", 0) == 0)
                {
                    // note=<rowIndex>,<startTick>,<lengthTick>,<velocity>
                    const char* rest = entry.c_str() + 5;
                    unsigned rowIndex = 0, startTick = 0, lengthTick = 0, velocity = 100;
                    if (std::sscanf(rest, "%u,%u,%u,%u", &rowIndex, &startTick, &lengthTick, &velocity) == 4)
                    {
                        Note n;
                        n.rowIndex = rowIndex;
                        n.startTick = startTick;
                        n.lengthTick = lengthTick;
                        n.velocity = (uint8_t)velocity;
                        p.notes.push_back(n);
                    }
                }
            }

            line = nl != nullptr ? nl + 1 : line + len;
        }

        if (p.rows.empty())
            p.rows = defaultRows();

        return p;
    }
};

} // namespace ostinateous
