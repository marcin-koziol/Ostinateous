/*
 * Ostinateous - bar-targeted fills: "put this kind of fill in bar N" -
 * applyFill() clears whatever notes currently occupy that one bar and
 * replaces them with a deterministic (seeded) generated fill, leaving every
 * other bar untouched. This is the direct answer to "for bar 3, insert a
 * snare roll" - select a bar in the UI, pick a fill, it's applied instantly.
 */

#pragma once

#include "Pattern.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace ostinateous {

enum class FillType
{
    SnareRoll = 0,
    KickFill,
    TomFill,
    HalfTimeBreak,
    BuildUp,
    Clear,
    Count
};

inline const char* fillName(FillType f) noexcept
{
    switch (f)
    {
    case FillType::SnareRoll:     return "Snare Roll";
    case FillType::KickFill:      return "Kick Fill";
    case FillType::TomFill:       return "Tom Fill";
    case FillType::HalfTimeBreak: return "Half-Time";
    case FillType::BuildUp:       return "Build-Up";
    case FillType::Clear:         return "Clear Bar";
    default:                      return "?";
    }
}

// removes every note whose startTick falls within [barIndex*ticksPerBar,
// (barIndex+1)*ticksPerBar) - notes are attributed to the bar they start in,
// same convention the UI's bar +/- controls already use when shrinking.
inline void clearBar(Pattern& p, uint32_t barIndex)
{
    const uint32_t ticksPerBar = p.ticksPerBar();
    const uint32_t lo = barIndex * ticksPerBar;
    const uint32_t hi = lo + ticksPerBar;

    std::vector<Note> kept;
    kept.reserve(p.notes.size());
    for (const Note& n : p.notes)
        if (n.startTick < lo || n.startTick >= hi)
            kept.push_back(n);
    p.notes = kept;
}

inline void applyFill(Pattern& p, uint32_t barIndex, FillType fill, uint32_t seed = 1)
{
    if (barIndex >= p.barCount)
        return;

    clearBar(p, barIndex);

    const uint32_t q = kTicksPerQuarter;
    const uint32_t e8 = q / 2;
    const uint32_t e16 = q / 4;
    const uint32_t e32 = q / 8;
    const uint32_t base = barIndex * p.ticksPerBar();

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> velJitter(-8, 8);
    auto vel = [&](int center) {
        return (uint8_t)std::clamp(center + velJitter(rng), 1, 127);
    };

    switch (fill)
    {
    case FillType::SnareRoll:
    {
        // first half of the bar: 16ths ramping up; second half: 32nds,
        // ending right at the bar boundary (a classic tom/snare roll shape)
        for (int i = 0; i < 8; ++i)
        {
            const int velCenter = 60 + (i * 6);
            p.notes.push_back({ 1, base + (uint32_t)i * e16, e16, vel(velCenter) });
        }
        for (int i = 0; i < 16; ++i)
        {
            const int velCenter = 95 + (i * 2);
            p.notes.push_back({ 1, base + 2 * q + (uint32_t)i * e32, e32, vel(std::min(velCenter, 127)) });
        }
        break;
    }

    case FillType::KickFill:
    {
        for (int i = 0; i < 8; ++i)
        {
            if (i % 3 == 2)
                continue; // leave a little syncopated space rather than a solid 8th run
            p.notes.push_back({ 0, base + (uint32_t)i * e8, e16, vel(110) });
        }
        p.notes.push_back({ 0, base + 3 * q + e8 + e16, e16, vel(120) }); // extra kick right at the end
        break;
    }

    case FillType::TomFill:
    {
        // descending run: High -> Mid -> Low, repeated with increasing
        // density across the bar, landing on Low right at the bar end
        static constexpr uint32_t toms[3] = { 6, 5, 4 }; // TomHigh, TomMid, TomLow
        int step = 0;
        for (uint32_t t = 0; t < p.ticksPerBar(); t += e16, ++step)
        {
            const uint32_t row = toms[step % 3];
            p.notes.push_back({ row, base + t, e16, vel(90 + step * 2) });
        }
        break;
    }

    case FillType::HalfTimeBreak:
        p.notes.push_back({ 0, base,         e16, vel(120) }); // kick "1"
        p.notes.push_back({ 10, base + 2 * q, e16, vel(122) }); // clap "3"
        p.notes.push_back({ 2, base + e8,         e16, vel(50) });
        p.notes.push_back({ 2, base + 3 * q + e8, e16, vel(50) });
        break;

    case FillType::BuildUp:
    {
        // hats double in density every beat, velocity rising throughout,
        // open hat accent on the very last 16th as a release
        int division = 1; // hits per 8th-note in this beat
        for (int beat = 0; beat < 4; ++beat)
        {
            const uint32_t step = e8 / (uint32_t)division;
            for (uint32_t t = 0; t < 2 * e8; t += step)
            {
                const int idx = (int)(t / (step > 0 ? step : 1));
                p.notes.push_back({ 2, base + (uint32_t)beat * q + t, step > 4 ? step - 2 : e32,
                                     vel(50 + beat * 15 + idx) });
            }
            division *= 2;
        }
        p.notes.push_back({ 3, base + p.ticksPerBar() - e16, e16, vel(120) });
        break;
    }

    case FillType::Clear:
    default:
        break; // clearBar() already emptied it
    }
}

} // namespace ostinateous
