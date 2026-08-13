/*
 * Ostinateous - algorithmic per-genre pattern variation. Unlike
 * FactoryPatterns.hpp's single fixed loop per genre, generateVariation()
 * rolls fresh, genre-appropriate randomized note choices every call (same
 * seed => same output, so it's reproducible/testable) - independently per
 * bar, so a multi-bar result isn't just N identical copies of one bar.
 *
 * Deliberately simple probabilistic rules per genre rather than anything
 * ML-ish: each genre is a handful of "this hit happens with probability P,
 * at roughly this velocity" statements, matching how these styles are
 * actually characterized (four-on-the-floor kick, backbeat clap, etc.).
 */

#pragma once

#include "Pattern.hpp"
#include "FactoryPatterns.hpp"

#include <cstdint>
#include <random>

namespace ostinateous {

namespace generator_detail {

// small helper bundling the RNG + the two distributions every rule uses
struct Roller
{
    std::mt19937 rng;
    std::uniform_real_distribution<float> chance{ 0.0f, 1.0f };

    explicit Roller(uint32_t seed) : rng(seed) {}

    bool hit(float probability) { return chance(rng) < probability; }

    uint8_t vel(int lo, int hi)
    {
        std::uniform_int_distribution<int> d(lo, hi);
        return (uint8_t)d(rng);
    }
};

inline void note(Pattern& p, uint32_t row, uint32_t tick, uint32_t len, uint8_t vel)
{
    p.notes.push_back({ row, tick, len, vel });
}

} // namespace generator_detail

inline Pattern generateVariation(Genre genre, uint32_t barCount, uint32_t seed)
{
    using namespace generator_detail;

    Pattern p;
    p.barCount = barCount > 0 ? barCount : 4;
    Roller r(seed);

    const uint32_t q = kTicksPerQuarter;
    const uint32_t e8 = q / 2;
    const uint32_t e16 = q / 4;
    const uint32_t ticksPerBar = p.ticksPerBar();

    for (uint32_t bar = 0; bar < p.barCount; ++bar)
    {
        const uint32_t base = bar * ticksPerBar;

        switch (genre)
        {
        case Genre::Techno:
            for (int beat = 0; beat < 4; ++beat)
                if (r.hit(0.98f))
                    note(p, 0, base + beat * q, e16, r.vel(110, 127));
            for (int i = 0; i < 8; ++i)
                if (r.hit(0.85f))
                    note(p, 2, base + i * e8 + e8 / 2, e16, r.vel(65, 95));
            if (r.hit(0.9f)) note(p, 10, base + q, e16, r.vel(100, 120));
            if (r.hit(0.9f)) note(p, 10, base + 3 * q, e16, r.vel(100, 120));
            if (r.hit(0.35f)) note(p, 3, base + 3 * q + e8, e8, r.vel(60, 85));
            if (r.hit(0.15f)) note(p, 9, base + q + e8 + e16, e16, r.vel(40, 60));
            break;

        case Genre::House:
            for (int beat = 0; beat < 4; ++beat)
                if (r.hit(0.97f))
                    note(p, 0, base + beat * q, e16, r.vel(108, 125));
            for (int i = 0; i < 16; ++i)
                if (r.hit(i % 4 == 2 ? 0.9f : 0.55f))
                    note(p, 2, base + i * e16, e16, i % 4 == 2 ? r.vel(85, 105) : r.vel(45, 70));
            if (r.hit(0.9f)) note(p, 10, base + q, e16, r.vel(100, 118));
            if (r.hit(0.9f)) note(p, 10, base + 3 * q, e16, r.vel(100, 118));
            if (r.hit(0.3f)) note(p, 9, base + q + e8 + e16, e16, r.vel(45, 65));
            if (r.hit(0.2f)) note(p, 11, base + 2 * q + e16, e16, r.vel(35, 55));
            break;

        case Genre::MinimalTechno:
            for (int beat = 0; beat < 4; ++beat)
                if (r.hit(0.95f))
                    note(p, 0, base + beat * q, e16, r.vel(95, 115));
            for (int i = 0; i < 16; ++i)
                if (r.hit(0.12f))
                    note(p, 11, base + i * e16, e16, r.vel(30, 55));
            if (r.hit(0.2f))
                note(p, 9, base + (uint32_t)(r.chance(r.rng) * 15) * e16, e16, r.vel(35, 55));
            break;

        case Genre::Trance:
            for (int beat = 0; beat < 4; ++beat)
                if (r.hit(0.99f))
                    note(p, 0, base + beat * q, e16, r.vel(115, 127));
            for (int i = 0; i < 16; ++i)
                if (r.hit(i % 2 == 0 ? 0.85f : 0.5f))
                    note(p, 2, base + i * e16, e16, i % 2 == 0 ? r.vel(80, 100) : r.vel(45, 70));
            if (r.hit(0.9f)) note(p, 1, base + q, e16, r.vel(100, 118));
            if (r.hit(0.9f)) note(p, 1, base + 3 * q, e16, r.vel(100, 118));
            if (bar == 0 && r.hit(0.4f)) note(p, 7, base, q, r.vel(90, 120));
            break;

        case Genre::DrumAndBass:
            if (r.hit(0.95f)) note(p, 0, base, e16, r.vel(110, 127));
            if (r.hit(0.7f)) note(p, 0, base + q + e8 + e16, e16, r.vel(90, 115));
            if (r.hit(0.9f)) note(p, 1, base + q, e16, r.vel(105, 122));
            if (r.hit(0.85f)) note(p, 1, base + 3 * q + e8, e16, r.vel(100, 120));
            for (int i = 0; i < 16; ++i)
                if (r.hit(i % 4 == 0 ? 0.8f : 0.45f))
                    note(p, 2, base + i * e16, e16, r.vel(45, 75));
            if (r.hit(0.3f)) note(p, 9, base + 2 * q + e16, e16, r.vel(35, 55));
            if (r.hit(0.15f)) note(p, 1, base + e8 + e16, e16, r.vel(30, 50)); // ghost snare
            break;

        case Genre::Dubstep:
            if (r.hit(0.97f)) note(p, 0, base, e16, r.vel(115, 127));
            if (r.hit(0.5f)) note(p, 0, base + 2 * q + e8 + e16, e16, r.vel(85, 110));
            if (r.hit(0.95f)) note(p, 10, base + 2 * q, e16, r.vel(112, 127));
            for (int i = 0; i < 8; ++i)
                if (r.hit(0.35f))
                    note(p, 2, base + i * e8, e16, r.vel(35, 60));
            break;

        case Genre::Hardstyle:
            for (int beat = 0; beat < 4; ++beat)
            {
                if (r.hit(0.99f)) note(p, 0, base + beat * q, e16, r.vel(120, 127));
                if (r.hit(0.75f)) note(p, 0, base + beat * q + e8, e16, r.vel(85, 105));
            }
            for (int i = 0; i < 8; ++i)
                if (r.hit(0.75f))
                    note(p, 2, base + i * e8, e16, r.vel(65, 90));
            if (r.hit(0.95f)) note(p, 10, base + q, e16, 127);
            if (r.hit(0.95f)) note(p, 10, base + 3 * q, e16, 127);
            break;

        default:
            break;
        }
    }

    return p;
}

} // namespace ostinateous
