/*
 * Ostinateous - factory pattern library: one hand-authored starting pattern per
 * genre, loadable from the UI's pattern browser (see ui/PatternStore.hpp for
 * the user-saved side of that same browser). These double as the "base feel"
 * each genre's generateVariation() (see Generators.hpp) riffs on - row
 * indices below match dsp/Pattern.hpp's defaultRows() order exactly:
 * 0 Kick, 1 Snare, 2 HatClosed, 3 HatOpen, 4 TomLow, 5 TomMid, 6 TomHigh,
 * 7 Crash, 8 Ride, 9 Rimshot, 10 Clap, 11 Maracas.
 */

#pragma once

#include "Pattern.hpp"

#include <cstdint>
#include <vector>

namespace ostinateous {

enum class Genre
{
    Techno = 0,
    House,
    MinimalTechno,
    Trance,
    DrumAndBass,
    Dubstep,
    Hardstyle,
    Count
};

inline const char* genreName(Genre g) noexcept
{
    switch (g)
    {
    case Genre::Techno:        return "Techno";
    case Genre::House:         return "House";
    case Genre::MinimalTechno: return "Minimal Techno";
    case Genre::Trance:        return "Trance";
    case Genre::DrumAndBass:   return "Drum & Bass";
    case Genre::Dubstep:       return "Dubstep";
    case Genre::Hardstyle:     return "Hardstyle";
    default:                   return "?";
    }
}

namespace factory_detail {

inline void addNote(Pattern& p, uint32_t row, uint32_t tick, uint32_t len, uint8_t vel)
{
    p.notes.push_back({ row, tick, len, vel });
}

// repeats a one-bar template across every bar of the pattern - factory
// patterns are meant as a steady loop; Generators.hpp is what adds
// per-bar variation on top of this same shape.
inline void repeatAcrossBars(Pattern& p, const std::vector<Note>& oneBar)
{
    const uint32_t ticksPerBar = p.ticksPerBar();
    for (uint32_t bar = 0; bar < p.barCount; ++bar)
        for (const Note& n : oneBar)
            addNote(p, n.rowIndex, n.startTick + bar * ticksPerBar, n.lengthTick, n.velocity);
}

} // namespace factory_detail

inline Pattern buildFactoryPattern(Genre genre)
{
    using namespace factory_detail;

    Pattern p;
    p.barCount = 4;
    const uint32_t q = kTicksPerQuarter;      // one beat
    const uint32_t e8 = q / 2;                // 8th note
    const uint32_t e16 = q / 4;                // 16th note

    std::vector<Note> bar;

    switch (genre)
    {
    case Genre::Techno:
        for (int beat = 0; beat < 4; ++beat)
            bar.push_back({ 0, (uint32_t)beat * q, e16, 120 }); // kick every beat
        for (int i = 0; i < 8; ++i) // closed hat on every 8th's off-beat
            bar.push_back({ 2, (uint32_t)i * e8 + e8 / 2, e16, 85 });
        bar.push_back({ 10, q,     e16, 110 }); // clap 2
        bar.push_back({ 10, 3 * q, e16, 110 }); // clap 4
        bar.push_back({ 3, 3 * q + e8, e8, 70 }); // open hat accent, "and" of 4
        break;

    case Genre::House:
        for (int beat = 0; beat < 4; ++beat)
            bar.push_back({ 0, (uint32_t)beat * q, e16, 118 });
        for (int i = 0; i < 16; ++i)
            bar.push_back({ 2, (uint32_t)i * e16, e16, (i % 4 == 2) ? (uint8_t)95 : (uint8_t)60 });
        bar.push_back({ 10, q,     e16, 112 });
        bar.push_back({ 10, 3 * q, e16, 112 });
        bar.push_back({ 9, q + e8 + e16, e16, 55 }); // syncopated rimshot ghost
        break;

    case Genre::MinimalTechno:
        for (int beat = 0; beat < 4; ++beat)
            bar.push_back({ 0, (uint32_t)beat * q, e16, 110 });
        bar.push_back({ 11, e8 + e16, e16, 45 });          // sparse maracas click
        bar.push_back({ 11, 2 * q + e8, e16, 40 });
        bar.push_back({ 9, 3 * q + e8 + e16, e16, 50 });   // one rimshot ghost near the end
        break;

    case Genre::Trance:
        for (int beat = 0; beat < 4; ++beat)
            bar.push_back({ 0, (uint32_t)beat * q, e16, 122 });
        for (int i = 0; i < 16; ++i)
            bar.push_back({ 2, (uint32_t)i * e16, e16, (i % 2 == 0) ? (uint8_t)90 : (uint8_t)55 });
        bar.push_back({ 1, q,     e16, 108 }); // snare 2
        bar.push_back({ 1, 3 * q, e16, 108 }); // snare 4
        break;

    case Genre::DrumAndBass:
        bar.push_back({ 0, 0,                 e16, 120 }); // kick "1"
        bar.push_back({ 0, q + e8 + e16,       e16, 100 }); // kick syncopated "e" of 2
        bar.push_back({ 1, q,                  e16, 118 }); // snare "2"
        bar.push_back({ 1, 3 * q + e8,         e16, 115 }); // snare "and" of 4 (classic amen placement)
        for (int i = 0; i < 16; ++i)
            bar.push_back({ 2, (uint32_t)i * e16, e16, (i % 4 == 0) ? (uint8_t)70 : (uint8_t)50 });
        bar.push_back({ 9, 2 * q + e16, e16, 45 }); // ghost rimshot
        break;

    case Genre::Dubstep:
        bar.push_back({ 0, 0, e16, 122 });                  // kick "1"
        bar.push_back({ 0, 2 * q + e8 + e16, e16, 95 });    // syncopated second kick
        bar.push_back({ 10, 2 * q, e16, 122 });             // clap/snare on "3" (half-time)
        bar.push_back({ 2, e8, e16, 50 });
        bar.push_back({ 2, q + e8, e16, 50 });
        bar.push_back({ 2, 3 * q + e8, e16, 50 });
        break;

    case Genre::Hardstyle:
        for (int beat = 0; beat < 4; ++beat)
        {
            bar.push_back({ 0, (uint32_t)beat * q, e16, 127 });
            bar.push_back({ 0, (uint32_t)beat * q + e8, e16, 90 }); // "double kick" ghost
        }
        for (int i = 0; i < 8; ++i)
            bar.push_back({ 2, (uint32_t)i * e8, e16, 75 });
        bar.push_back({ 10, q,     e16, 127 });
        bar.push_back({ 10, 3 * q, e16, 127 });
        break;

    default:
        break;
    }

    repeatAcrossBars(p, bar);
    return p;
}

} // namespace ostinateous
