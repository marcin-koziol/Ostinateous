/*
 * Ostinateous - piano-roll grid layout and Cairo painting, shared between the
 * real plugin UI and an offline PNG renderer (see tools/offline_render.cpp) -
 * same split-file "layout+paint in one header" technique the Sideous family
 * uses in its own ui/UIPainter.hpp, so the grid can be visually iterated on
 * without a display.
 *
 * Palette below is its own cool cyan/teal scheme, not a reskin of
 * sideous-drums's warm amber look - see the palette section's own comment
 * for why. The one thing still copied verbatim from sideous-drums's
 * ui/UIPainter.hpp is the per-voice accent colors, so a row named
 * "Kick"/"Snare"/etc. here reads with the exact same color as that plugin's
 * own UI - that part of the family consistency is deliberate, no shared
 * code between the two repos needed for it.
 *
 * Layout is a fixed vertical stack: title row, toolbar row, loop-range
 * ruler, grid, horizontal scrollbar - each on its own row so nothing can
 * visually collide regardless of window size (the title used to share
 * coordinate space with the first toolbar buttons and could overlap them;
 * see the session that split these into separate rows).
 *
 * The grid supports horizontal zoom + scroll: `View::zoom` >= 1 (1 = whole
 * pattern fit to the visible width) and `View::scrollTick` pans within it.
 * All rows are always visible (no vertical scroll/zoom - v1 scope), and
 * there's no row drag-reassignment or time-signature picker yet either.
 */

#pragma once

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../dsp/Fills.hpp"
#include "../dsp/Pattern.hpp"

namespace ostinateous {
namespace ui {

// -----------------------------------------------------------------------------------------------------------
// palette - light-gray/black/purple/pink, deliberately its own thing rather
// than sideous-drums's warm amber/brown "analog drum machine chassis" look,
// so the two are distinguishable at a glance even though they share the
// same overall dark-panel/monospace family styling. The one exception is
// the per-row instrument accent colors just below (kKickAccent etc.) -
// those stay identical to sideous-drums's own palette on purpose, since a
// row named "Kick"/"Snare"/etc. reading as the same color in both plugins
// is useful cross-plugin consistency, not an accidental reskin.

struct Color { double r, g, b; };

static constexpr Color kBg        { 0.075, 0.060, 0.095 };
static constexpr Color kPanelBg   { 0.165, 0.135, 0.205 };
static constexpr Color kPanelEdge { 0.62,  0.42,  0.78  };
static constexpr Color kTextMain  { 0.93,  0.91,  0.95  };
static constexpr Color kTextDim   { 0.62,  0.56,  0.68  };
static constexpr Color kBtnBg     { 0.195, 0.155, 0.245 };
static constexpr Color kGridLine  { 0.20,  0.15,  0.25  };
static constexpr Color kBarLine   { 0.44,  0.31,  0.54  };
static constexpr Color kLoopRange { 0.95,  0.35,  0.68  };

// primary chrome highlight (title, active toggles, playhead, rubber-band,
// hover borders) - the signature color of Ostinateous's own look, as
// opposed to kKickAccent below which is about instrument-row identity
static constexpr Color kAccent { 0.70, 0.42, 0.92 };

static constexpr Color kKickAccent      { 1.00, 0.42, 0.18 };
static constexpr Color kSnareAccent     { 1.00, 0.82, 0.35 };
static constexpr Color kHatClosedAccent { 0.90, 0.78, 0.55 };
static constexpr Color kHatOpenAccent   { 0.70, 0.55, 0.35 };
static constexpr Color kTomLowAccent    { 0.25, 0.75, 0.62 };
static constexpr Color kTomMidAccent    { 0.40, 0.85, 0.50 };
static constexpr Color kTomHighAccent   { 0.65, 0.92, 0.40 };
static constexpr Color kCrashAccent     { 0.45, 0.88, 0.92 };
static constexpr Color kRideAccent      { 0.40, 0.62, 0.95 };
static constexpr Color kRimAccent       { 0.95, 0.35, 0.65 };
static constexpr Color kClapAccent      { 0.95, 0.25, 0.30 };
static constexpr Color kMaracasAccent   { 0.68, 0.48, 0.95 };
static constexpr Color kDefaultAccent   { 0.70, 0.70, 0.70 };

// index-aligned with defaultRows() in dsp/Pattern.hpp
static constexpr Color kDefaultRowAccents[12] = {
    kKickAccent, kSnareAccent, kHatClosedAccent, kHatOpenAccent,
    kTomLowAccent, kTomMidAccent, kTomHighAccent,
    kCrashAccent, kRideAccent, kRimAccent, kClapAccent, kMaracasAccent,
};

inline Color rowAccent(size_t rowIndex) noexcept
{
    return rowIndex < 12 ? kDefaultRowAccents[rowIndex] : kDefaultAccent;
}

inline void setColor(cairo_t* cr, Color c, double a = 1.0) noexcept
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
}

// -----------------------------------------------------------------------------------------------------------
// text/shape helpers (same cairo-current-point gotcha as the rest of the
// family - always route text through drawText() so a stray connecting line
// never gets drawn from wherever cairo_show_text() left the pen)

inline void setFont(cairo_t* cr, double size, bool bold = true) noexcept
{
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
}

inline void drawText(cairo_t* cr, const char* text, double x, double y) noexcept
{
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
    cairo_new_path(cr);
}

inline void centeredText(cairo_t* cr, const char* text, double cx, double cy) noexcept
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    drawText(cr, text, cx - ext.width / 2.0 - ext.x_bearing, cy - ext.height / 2.0 - ext.y_bearing);
}

inline void rightAlignedText(cairo_t* cr, const char* text, double rightX, double cy) noexcept
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    drawText(cr, text, rightX - ext.width - ext.x_bearing, cy - ext.height / 2.0 - ext.y_bearing);
}

inline void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) noexcept
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0,      M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI_2,   M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI,     3.0 * M_PI_2);
    cairo_close_path(cr);
}

// -----------------------------------------------------------------------------------------------------------
// layout

struct Rect { float x, y, w, h; };

inline bool hitRect(const Rect& r, double mx, double my) noexcept
{
    return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
}

struct Toolbar
{
    Rect barMinus, barPlus, barLabel;
    Rect snapToggle;
    Rect snapRes[4]; // 1/4, 1/8, 1/16, 1/32
    Rect loopToggle;
    Rect zoomOut, zoomIn, zoomFit, zoomLabel;
    Rect velocity; // default velocity for newly-inserted/painted notes - scroll wheel adjusts, no click action
};

// pattern library: Prev/Next cycles through [7 genres, then user-saved
// patterns alphabetically]; Generate re-rolls a fresh algorithmic variation
// of whichever genre is currently selected (see dsp/Generators.hpp);
// Save/Delete manage the user library (see ui/PatternStore.hpp)
struct PatternRow
{
    Rect prev, next, nameLabel;
    Rect generate, save, del;
    Rect exportMidi, folder;
    Rect scale; // UI scale (UiScale.hpp), label from PaintState::scaleLabel
};

// bar-targeted fills (see dsp/Fills.hpp) - selectedBar is set by clicking in
// the ruler (a plain click, not a drag, which still sets the loop range)
struct FillRow
{
    Rect barLabel;
    Rect fillBtn[6]; // indexed by FillType
};

// the view (zoom/scroll) is UI-owned state, not derived from the pattern -
// GridUI persists this across repaints/edits and feeds it back into
// buildLayout() every time, the same way it already does for snap settings.
struct View
{
    float zoom = 1.0f;        // >= 1; 1 = whole pattern fit to grid width
    uint32_t scrollTick = 0;  // leftmost visible tick
};

struct GridGeometry
{
    float rowHeaderW = 150.0f;
    float rowH = 34.0f;
    float rulerH = 16.0f;
    float scrollbarH = 14.0f;
    float gridX = 0.0f, gridY = 0.0f, gridW = 0.0f, gridH = 0.0f;
    float rulerY = 0.0f;
    float scrollbarY = 0.0f;
    float pixelsPerTick = 0.0f;
    uint32_t scrollTick = 0;
    uint32_t visibleTicks = 0;
};

struct Layout
{
    float width = 0.0f, height = 0.0f;
    GridGeometry geom;
    Toolbar toolbar;
    PatternRow patternRow;
    FillRow fillRow;
    Rect titleRow;
};

inline float clampScrollTick(uint32_t totalTicks, uint32_t visibleTicks, float scrollTick) noexcept
{
    const float maxScroll = (float)totalTicks > (float)visibleTicks ? (float)(totalTicks - visibleTicks) : 0.0f;
    return std::max(0.0f, std::min(scrollTick, maxScroll));
}

inline Layout buildLayout(float width, float height, const Pattern& pattern, View view = View())
{
    Layout L;
    L.width = width;
    L.height = height;

    const float margin = 12.0f;

    // --- title row ---
    const float titleH = 22.0f;
    L.titleRow = { margin, margin, width - 2.0f * margin, titleH };

    // --- toolbar row ---
    const float toolbarY = margin + titleH + 6.0f;
    const float toolbarH = 26.0f;
    {
        Toolbar& tb = L.toolbar;
        float x = margin;

        tb.barMinus = { x, toolbarY, 26.0f, toolbarH }; x += 26.0f + 4.0f;
        tb.barLabel = { x, toolbarY, 76.0f, toolbarH }; x += 76.0f + 4.0f;
        tb.barPlus  = { x, toolbarY, 26.0f, toolbarH }; x += 26.0f + 18.0f;

        tb.snapToggle = { x, toolbarY, 82.0f, toolbarH }; x += 82.0f + 10.0f;

        for (int i = 0; i < 4; ++i)
        {
            const float w = 42.0f;
            tb.snapRes[i] = { x, toolbarY, w, toolbarH };
            x += w + 4.0f;
        }

        x += 16.0f;
        tb.loopToggle = { x, toolbarY, 64.0f, toolbarH }; x += 64.0f + 18.0f;

        tb.zoomOut  = { x, toolbarY, 26.0f, toolbarH }; x += 26.0f + 4.0f;
        tb.zoomLabel = { x, toolbarY, 64.0f, toolbarH }; x += 64.0f + 4.0f;
        tb.zoomIn   = { x, toolbarY, 26.0f, toolbarH }; x += 26.0f + 8.0f;
        tb.zoomFit  = { x, toolbarY, 44.0f, toolbarH }; x += 44.0f + 16.0f;

        tb.velocity = { x, toolbarY, 84.0f, toolbarH };
    }

    // --- pattern library row ---
    const float patternY = toolbarY + toolbarH + 6.0f;
    const float patternH = 26.0f;
    {
        PatternRow& pr = L.patternRow;
        float x = margin;

        pr.prev = { x, patternY, 26.0f, patternH }; x += 26.0f + 4.0f;
        pr.nameLabel = { x, patternY, 190.0f, patternH }; x += 190.0f + 4.0f;
        pr.next = { x, patternY, 26.0f, patternH }; x += 26.0f + 16.0f;

        pr.generate = { x, patternY, 92.0f, patternH }; x += 92.0f + 8.0f;
        pr.save = { x, patternY, 60.0f, patternH }; x += 60.0f + 4.0f;
        pr.del = { x, patternY, 60.0f, patternH }; x += 60.0f + 16.0f;

        pr.exportMidi = { x, patternY, 110.0f, patternH }; x += 110.0f + 4.0f;
        pr.folder = { x, patternY, 70.0f, patternH }; x += 70.0f + 16.0f;
        pr.scale = { x, patternY, 96.0f, patternH };
    }

    // --- fill row ---
    const float fillY = patternY + patternH + 6.0f;
    const float fillH = 26.0f;
    {
        FillRow& fr = L.fillRow;
        float x = margin;

        fr.barLabel = { x, fillY, 66.0f, fillH }; x += 66.0f + 12.0f;

        static constexpr float kFillW[6] = { 78.0f, 70.0f, 66.0f, 66.0f, 70.0f, 66.0f };
        for (int i = 0; i < 6; ++i)
        {
            fr.fillBtn[i] = { x, fillY, kFillW[i], fillH };
            x += kFillW[i] + 4.0f;
        }
    }

    // --- grid geometry ---
    GridGeometry& g = L.geom;
    g.rulerY = fillY + fillH + 6.0f;
    g.gridY = g.rulerY + g.rulerH + 8.0f;
    g.gridX = margin + g.rowHeaderW;
    g.gridW = width - g.gridX - margin;

    // row height stretches to fill whatever vertical space is actually
    // available, instead of a fixed 34px always leaving the rest of a taller
    // window as unused black space below the grid/scrollbar - clamped so
    // rows never get illegibly short or absurdly tall
    const float bottomReserve = 6.0f + g.scrollbarH + margin;
    const size_t rowCount = pattern.rows.size();
    if (rowCount > 0)
    {
        const float availableH = std::max(0.0f, height - bottomReserve - g.gridY);
        g.rowH = std::clamp(availableH / (float)rowCount, 24.0f, 56.0f);
    }
    g.gridH = (float)rowCount * g.rowH;
    if (g.gridY + g.gridH > height - bottomReserve)
        g.gridH = std::max(0.0f, height - bottomReserve - g.gridY);
    g.scrollbarY = g.gridY + g.gridH + 6.0f;

    const uint32_t totalTicks = pattern.totalTicks();
    const float baseZoom = std::max(1.0f, view.zoom);
    const float basePixelsPerTick = totalTicks > 0 ? g.gridW / (float)totalTicks : 1.0f;
    g.pixelsPerTick = basePixelsPerTick * baseZoom;
    g.visibleTicks = g.pixelsPerTick > 0.0f ? (uint32_t)(g.gridW / g.pixelsPerTick) : totalTicks;
    if (g.visibleTicks == 0)
        g.visibleTicks = totalTicks;
    g.scrollTick = (uint32_t)clampScrollTick(totalTicks, g.visibleTicks, (float)view.scrollTick);

    return L;
}

inline Rect scrollbarRect(const Layout& L) noexcept
{
    return { L.geom.gridX, L.geom.scrollbarY, L.geom.gridW, L.geom.scrollbarH };
}

inline uint32_t xToTick(const Layout& L, double x) noexcept
{
    const double rel = (x - L.geom.gridX) / (L.geom.pixelsPerTick > 0.0f ? L.geom.pixelsPerTick : 1.0f);
    const double abs = (double)L.geom.scrollTick + rel;
    return abs < 0.0 ? 0u : (uint32_t)abs;
}

inline float tickToX(const Layout& L, uint32_t tick) noexcept
{
    return L.geom.gridX + ((float)tick - (float)L.geom.scrollTick) * L.geom.pixelsPerTick;
}

inline int yToRow(const Layout& L, const Pattern& pattern, double y) noexcept
{
    if (y < L.geom.gridY)
        return -1;
    const int row = (int)((y - L.geom.gridY) / L.geom.rowH);
    return row >= 0 && row < (int)pattern.rows.size() ? row : -1;
}

// mute/solo button rects, per row header - single source of truth shared by
// hit-testing and rendering. Right-aligned within the row header column,
// scaled a bit with the current (stretchy) row height but capped so they
// stay usable-sized rather than growing huge on a tall window.
inline Rect rowMuteRect(const Layout& L, int rowIndex) noexcept
{
    const float h = std::min(20.0f, L.geom.rowH - 8.0f);
    const float y = L.geom.gridY + (float)rowIndex * L.geom.rowH + (L.geom.rowH - h) * 0.5f;
    return { L.geom.gridX - 50.0f, y, 22.0f, h };
}

inline Rect rowSoloRect(const Layout& L, int rowIndex) noexcept
{
    const Rect m = rowMuteRect(L, rowIndex);
    return { m.x + m.w + 4.0f, m.y, 22.0f, m.h };
}

inline uint32_t snapTick(uint32_t tick, uint32_t snapTicks, bool snapOn) noexcept
{
    if (!snapOn || snapTicks == 0)
        return tick;
    // floor, not round-to-nearest: a click anywhere within a cell should
    // place/move a note at that cell's start, not jump to whichever grid
    // line is closer - rounding made clicking just past a cell's midpoint
    // snap forward into the next one, which read as overly aggressive
    return (tick / snapTicks) * snapTicks;
}

// index into pattern.notes, or -1. `nearRightEdge` (optional) reports whether
// the hit was close enough to the note's right edge to mean "resize" rather
// than "move" - shared by paint() (hover feedback) and UI input hit-testing.
inline int hitTestNote(const Layout& L, const Pattern& pattern, double mx, double my,
                        bool* nearRightEdge = nullptr) noexcept
{
    const int row = yToRow(L, pattern, my);
    if (row < 0)
        return -1;

    for (int i = (int)pattern.notes.size() - 1; i >= 0; --i)
    {
        const Note& n = pattern.notes[(size_t)i];
        if ((int)n.rowIndex != row)
            continue;

        const float x0 = tickToX(L, n.startTick);
        const float x1 = tickToX(L, n.startTick + n.lengthTick);
        if (mx >= x0 && mx <= x1)
        {
            if (nearRightEdge != nullptr)
                *nearRightEdge = (x1 - mx) < 6.0f;
            return i;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------------------------------------------
// paint

struct PaintState
{
    const Pattern* pattern = nullptr;
    const uint8_t* selected = nullptr; // sized to pattern->notes, may be nullptr (none selected);
                                        // uint8_t not bool - std::vector<bool> has no .data()

    int hoverNote = -1;
    int hoverToolbarBtn = -1; // -1 none, 0=bar-,1=bar+,2=snapToggle,3..6=snapRes[0..3],7=loopToggle,8=zoomOut,9=zoomIn,10=zoomFit

    // empty-cell hover preview: where a click would place a note right now
    // (already snapped) - only meaningful when hoverNote < 0
    int hoverEmptyRow = -1;
    uint32_t hoverEmptyTick = 0;

    int hoverMuteRow = -1;
    int hoverSoloRow = -1;

    bool snapOn = true;
    uint32_t snapTicks = 480;

    // velocity for newly-inserted/painted notes (scroll wheel over the
    // toolbar's VEL box adjusts this); hoverVelocityBox just highlights it
    uint8_t defaultVelocity = 100;
    bool hoverVelocityBox = false;

    bool rubberBandActive = false;
    float rubberX0 = 0, rubberY0 = 0, rubberX1 = 0, rubberY1 = 0;

    bool hostPlaying = false;
    double hostBpm = 0.0;
    uint32_t playheadTick = 0; // only meaningful if hostPlaying

    // live loop-range drag preview (ruler); when not dragging, the painter
    // just shows pattern->loopEnabled/loopStartTick/loopEndTick as-is
    bool loopDragActive = false;
    uint32_t loopDragStartTick = 0, loopDragEndTick = 0;

    // scrollbar drag preview (thumb currently being dragged) - purely visual,
    // GridUI is the source of truth for the actual scroll position via View
    bool scrollbarDragActive = false;

    // pattern library
    const char* currentName = "Techno";
    bool isUserPattern = false;    // controls whether Delete is enabled
    int hoverPatternBtn = -1;      // -1 none, 0=prev,1=next,2=generate,3=save,4=delete,5=exportMidi,6=folder,7=UI scale
    const char* scaleLabel = "";   // UI-scale button text (UiScale.hpp formatLabel)
    const char* toast = nullptr;   // brief confirmation message (e.g. "Saved!"), or nullptr

    // in-place text entry, shared by "Save As" (pattern name) and "Folder"
    // (export path) - editTarget names which one so the box can be labeled
    // unambiguously; the two never overlap since only one can be active
    enum class EditTarget { None, PatternName, ExportFolder };
    EditTarget editTarget = EditTarget::None;
    const char* editBuffer = "";

    // bar-targeted fills
    uint32_t selectedBar = 0;
    int hoverFillBtn = -1; // -1 none, 0..5 = FillType
};

inline void paintToolbar(cairo_t* cr, const Layout& L, const PaintState& state)
{
    const Toolbar& tb = L.toolbar;
    const Pattern& p = *state.pattern;

    auto button = [&](const Rect& r, const char* label, bool active, int hoverId, int myId)
    {
        roundedRect(cr, r.x, r.y, r.w, r.h, 4.0);
        setColor(cr, active ? kAccent : kBtnBg, hoverId == myId ? 1.0 : (active ? 0.85 : 0.7));
        cairo_fill_preserve(cr);
        setColor(cr, kPanelEdge, 0.8);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        setFont(cr, 10.5);
        setColor(cr, active ? kBg : kTextMain);
        centeredText(cr, label, r.x + r.w / 2.0, r.y + r.h / 2.0);
    };

    button(tb.barMinus, "-", false, state.hoverToolbarBtn, 0);
    button(tb.barPlus,  "+", false, state.hoverToolbarBtn, 1);

    setFont(cr, 11.0, false);
    setColor(cr, kTextMain);
    char barsBuf[32];
    std::snprintf(barsBuf, sizeof(barsBuf), "%u bars", p.barCount);
    centeredText(cr, barsBuf, tb.barLabel.x + tb.barLabel.w / 2.0, tb.barLabel.y + tb.barLabel.h / 2.0);

    button(tb.snapToggle, state.snapOn ? "SNAP ON" : "SNAP OFF", state.snapOn, state.hoverToolbarBtn, 2);

    const char* resLabels[4] = { "1/4", "1/8", "1/16", "1/32" };
    const uint32_t resTicks[4] = { kTicksPerQuarter, kTicksPerQuarter / 2, kTicksPerQuarter / 4, kTicksPerQuarter / 8 };
    for (int i = 0; i < 4; ++i)
        button(tb.snapRes[i], resLabels[i], state.snapTicks == resTicks[i], state.hoverToolbarBtn, 3 + i);

    button(tb.loopToggle, "LOOP", p.loopEnabled, state.hoverToolbarBtn, 7);

    button(tb.zoomOut, "-", false, state.hoverToolbarBtn, 8);
    button(tb.zoomIn,  "+", false, state.hoverToolbarBtn, 9);
    button(tb.zoomFit, "FIT", false, state.hoverToolbarBtn, 10);

    setFont(cr, 11.0, false);
    setColor(cr, kTextDim);
    char zoomBuf[32];
    std::snprintf(zoomBuf, sizeof(zoomBuf), "%.0f%%", (double)(L.geom.pixelsPerTick > 0.0f
                  ? (L.geom.pixelsPerTick / (L.geom.gridW / std::max(1.0f, (float)p.totalTicks()))) * 100.0f
                  : 100.0f));
    centeredText(cr, zoomBuf, tb.zoomLabel.x + tb.zoomLabel.w / 2.0, tb.zoomLabel.y + tb.zoomLabel.h / 2.0);

    roundedRect(cr, tb.velocity.x, tb.velocity.y, tb.velocity.w, tb.velocity.h, 4.0);
    setColor(cr, kBtnBg, state.hoverVelocityBox ? 1.0 : 0.7);
    cairo_fill_preserve(cr);
    setColor(cr, state.hoverVelocityBox ? kAccent : kPanelEdge, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    setFont(cr, 10.5, false);
    setColor(cr, kTextMain);
    char velBuf[24];
    std::snprintf(velBuf, sizeof(velBuf), "VEL %u", (unsigned)state.defaultVelocity);
    centeredText(cr, velBuf, tb.velocity.x + tb.velocity.w / 2.0, tb.velocity.y + tb.velocity.h / 2.0);
}

inline void paintPatternRow(cairo_t* cr, const Layout& L, const PaintState& state)
{
    const PatternRow& pr = L.patternRow;

    auto button = [&](const Rect& r, const char* label, bool active, bool enabled, int hoverId, int myId)
    {
        roundedRect(cr, r.x, r.y, r.w, r.h, 4.0);
        setColor(cr, active ? kAccent : kBtnBg, !enabled ? 0.35 : (hoverId == myId ? 1.0 : (active ? 0.85 : 0.7)));
        cairo_fill_preserve(cr);
        setColor(cr, kPanelEdge, enabled ? 0.8 : 0.4);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        setFont(cr, 10.5);
        setColor(cr, active ? kBg : kTextMain, enabled ? 1.0 : 0.45);
        centeredText(cr, label, r.x + r.w / 2.0, r.y + r.h / 2.0);
    };

    const bool editing = state.editTarget != PaintState::EditTarget::None;
    const bool editingFolder = state.editTarget == PaintState::EditTarget::ExportFolder;

    button(pr.prev, "<", false, true, state.hoverPatternBtn, 0);
    button(pr.next, ">", false, true, state.hoverPatternBtn, 1);

    roundedRect(cr, pr.nameLabel.x, pr.nameLabel.y, pr.nameLabel.w, pr.nameLabel.h, 4.0);
    setColor(cr, kPanelBg);
    cairo_fill_preserve(cr);
    setColor(cr, editing ? kAccent : kPanelEdge, editing ? 1.0 : 0.6);
    cairo_set_line_width(cr, editing ? 2.0 : 1.0);
    cairo_stroke(cr);

    // clip so a long folder path can't visually spill into the buttons
    // beside this box - it just scrolls/truncates from the left instead
    cairo_save(cr);
    cairo_rectangle(cr, pr.nameLabel.x, pr.nameLabel.y, pr.nameLabel.w, pr.nameLabel.h);
    cairo_clip(cr);

    setColor(cr, kTextMain);
    if (editingFolder)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Folder: %s_", state.editBuffer);
        setFont(cr, 9.5, false);
        // keep the tail (where the cursor is) visible once it outgrows the box
        const size_t maxChars = 34;
        const char* shown = std::strlen(buf) > maxChars ? buf + (std::strlen(buf) - maxChars) : buf;
        drawText(cr, shown, pr.nameLabel.x + 8.0, pr.nameLabel.y + pr.nameLabel.h * 0.68f);
    }
    else if (state.editTarget == PaintState::EditTarget::PatternName)
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%s_", state.editBuffer);
        setFont(cr, 11.0, false);
        drawText(cr, buf, pr.nameLabel.x + 8.0, pr.nameLabel.y + pr.nameLabel.h * 0.68f);
    }
    else
    {
        setFont(cr, 11.0, false);
        drawText(cr, state.currentName, pr.nameLabel.x + 8.0, pr.nameLabel.y + pr.nameLabel.h * 0.68f);
    }
    cairo_restore(cr);

    button(pr.generate, "GENERATE", false, true, state.hoverPatternBtn, 2);
    button(pr.save, editingFolder ? "SET" : "SAVE", editing, true, state.hoverPatternBtn, 3);
    button(pr.del, "DEL", false, state.isUserPattern, state.hoverPatternBtn, 4);

    button(pr.exportMidi, "EXPORT MIDI", false, true, state.hoverPatternBtn, 5);
    button(pr.folder, "FOLDER", editingFolder, true, state.hoverPatternBtn, 6);
    button(pr.scale, state.scaleLabel, false, true, state.hoverPatternBtn, 7);

    if (state.toast != nullptr)
    {
        setFont(cr, 10.5, false);
        setColor(cr, kAccent);
        drawText(cr, state.toast, pr.folder.x + pr.folder.w + 12.0f, pr.folder.y + pr.folder.h * 0.68f);
    }
}

inline void paintFillRow(cairo_t* cr, const Layout& L, const PaintState& state)
{
    const Pattern& p = *state.pattern;
    const FillRow& fr = L.fillRow;

    setFont(cr, 11.0, false);
    setColor(cr, kTextMain);
    char barBuf[24];
    std::snprintf(barBuf, sizeof(barBuf), "BAR %u", state.selectedBar + 1);
    drawText(cr, barBuf, fr.barLabel.x, fr.barLabel.y + fr.barLabel.h * 0.68f);

    for (int i = 0; i < 6; ++i)
    {
        const Rect& r = fr.fillBtn[i];
        roundedRect(cr, r.x, r.y, r.w, r.h, 4.0);
        setColor(cr, kBtnBg, state.hoverFillBtn == i ? 1.0 : 0.7);
        cairo_fill_preserve(cr);
        setColor(cr, kPanelEdge, 0.8);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        setFont(cr, 9.5);
        setColor(cr, kTextMain);
        centeredText(cr, fillName((FillType)i), r.x + r.w / 2.0, r.y + r.h / 2.0);
    }
    (void)p;
}

inline void paintTitleRow(cairo_t* cr, const Layout& L, const PaintState& state)
{
    setFont(cr, 18.0);
    setColor(cr, kAccent);
    drawText(cr, "OSTINATEOUS", L.titleRow.x, L.titleRow.y + L.titleRow.h * 0.85f);

    setFont(cr, 11.5, false);
    setColor(cr, state.hostPlaying ? kAccent : kTextDim);
    char statusBuf[48];
    if (state.hostBpm > 0.0)
        std::snprintf(statusBuf, sizeof(statusBuf), "%s  %.1f BPM", state.hostPlaying ? "PLAYING" : "STOPPED", state.hostBpm);
    else
        std::snprintf(statusBuf, sizeof(statusBuf), "no host transport");
    rightAlignedText(cr, statusBuf, L.titleRow.x + L.titleRow.w, L.titleRow.y + L.titleRow.h * 0.75f);
}

inline void paintRuler(cairo_t* cr, const Layout& L, const PaintState& state)
{
    const Pattern& p = *state.pattern;
    const GridGeometry& g = L.geom;

    setColor(cr, kPanelBg);
    cairo_rectangle(cr, g.gridX, g.rulerY, g.gridW, g.rulerH);
    cairo_fill(cr);

    // selected-bar highlight (the fill target) - drawn first so the loop
    // range and tick marks still show clearly on top of it
    if (state.selectedBar < p.barCount)
    {
        const float x0 = tickToX(L, state.selectedBar * p.ticksPerBar());
        const float x1 = tickToX(L, (state.selectedBar + 1) * p.ticksPerBar());
        setColor(cr, kTextMain, 0.14);
        cairo_rectangle(cr, x0, g.rulerY, std::max(1.0f, x1 - x0), g.rulerH);
        cairo_fill(cr);
    }

    // bar tick marks + numbers
    const uint32_t ticksPerBar = p.ticksPerBar();
    const uint32_t totalTicks = p.totalTicks();
    setFont(cr, 8.5, false);
    for (uint32_t tick = 0; tick <= totalTicks; tick += ticksPerBar)
    {
        const float x = tickToX(L, tick);
        if (x < g.gridX - 1.0f || x > g.gridX + g.gridW + 1.0f)
            continue;
        setColor(cr, kTextDim, 0.7);
        cairo_move_to(cr, x, g.rulerY);
        cairo_line_to(cr, x, g.rulerY + g.rulerH);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        if (tick < totalTicks)
        {
            char numBuf[8];
            std::snprintf(numBuf, sizeof(numBuf), "%u", (unsigned)(tick / ticksPerBar) + 1);
            setColor(cr, kTextDim, 0.9);
            drawText(cr, numBuf, x + 3.0f, g.rulerY + g.rulerH * 0.75f);
        }
    }

    // active/dragged loop range highlight
    const bool showDrag = state.loopDragActive;
    const uint32_t loopStart = showDrag ? state.loopDragStartTick : p.loopStartTick;
    const uint32_t loopEnd = showDrag ? state.loopDragEndTick : p.loopEndTick;
    const bool showLoop = showDrag ? (loopEnd > loopStart) : (p.loopEnabled && p.loopEndTick > p.loopStartTick);

    if (showLoop)
    {
        const float x0 = tickToX(L, loopStart);
        const float x1 = tickToX(L, loopEnd);
        setColor(cr, kLoopRange, showDrag ? 0.55 : 0.4);
        cairo_rectangle(cr, x0, g.rulerY + 2.0f, std::max(2.0f, x1 - x0), g.rulerH - 4.0f);
        cairo_fill(cr);
        setColor(cr, kLoopRange, 0.9);
        cairo_set_line_width(cr, 1.5);
        cairo_rectangle(cr, x0, g.rulerY + 2.0f, std::max(2.0f, x1 - x0), g.rulerH - 4.0f);
        cairo_stroke(cr);

        // faint tint down through the grid rows so the active loop range is
        // visible at a glance without staring at the ruler
        if (!showDrag)
        {
            setColor(cr, kLoopRange, 0.06);
            cairo_rectangle(cr, x0, g.gridY, std::max(1.0f, x1 - x0), g.gridH);
            cairo_fill(cr);
        }
    }

    setColor(cr, kPanelEdge, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, g.gridX, g.rulerY, g.gridW, g.rulerH);
    cairo_stroke(cr);
}

inline void paintScrollbar(cairo_t* cr, const Layout& L, const PaintState& state)
{
    const Pattern& p = *state.pattern;
    const GridGeometry& g = L.geom;
    const uint32_t totalTicks = p.totalTicks();
    if (totalTicks == 0 || g.visibleTicks >= totalTicks)
        return; // fully zoomed out - nothing to scroll, keep the row empty

    setColor(cr, kPanelBg);
    cairo_rectangle(cr, g.gridX, g.scrollbarY, g.gridW, g.scrollbarH);
    cairo_fill(cr);

    const float thumbX = g.gridX + ((float)g.scrollTick / (float)totalTicks) * g.gridW;
    const float thumbW = std::max(20.0f, ((float)g.visibleTicks / (float)totalTicks) * g.gridW);

    roundedRect(cr, thumbX, g.scrollbarY + 1.0f, thumbW, g.scrollbarH - 2.0f, 3.0);
    setColor(cr, kTextDim, state.scrollbarDragActive ? 1.0 : 0.8);
    cairo_fill(cr);
}

inline void paint(cairo_t* cr, const Layout& L, const PaintState& state)
{
    setColor(cr, kBg);
    cairo_paint(cr);

    paintTitleRow(cr, L, state);
    paintToolbar(cr, L, state);
    paintPatternRow(cr, L, state);
    paintFillRow(cr, L, state);
    paintRuler(cr, L, state);

    const Pattern& p = *state.pattern;
    const GridGeometry& g = L.geom;

    // row headers + grid row backgrounds
    for (size_t row = 0; row < p.rows.size(); ++row)
    {
        const float y = g.gridY + (float)row * g.rowH;
        const Color accent = rowAccent(row);
        const bool audible = rowAudible(p.rows, (uint32_t)row);

        setColor(cr, (row % 2 == 0) ? kPanelBg : kBg);
        cairo_rectangle(cr, 12.0, y, L.width - 24.0, g.rowH);
        cairo_fill(cr);

        setFont(cr, 11.5);
        setColor(cr, accent, audible ? 1.0 : 0.35);
        drawText(cr, p.rows[row].name.c_str(), 16.0, y + g.rowH * 0.42f);

        setFont(cr, 9.0, false);
        setColor(cr, kTextDim, audible ? 1.0 : 0.5);
        char noteBuf[16];
        std::snprintf(noteBuf, sizeof(noteBuf), "MIDI %d", p.rows[row].midiNote);
        drawText(cr, noteBuf, 16.0, y + g.rowH * 0.82f);

        const Row& r = p.rows[row];
        const Rect muteR = rowMuteRect(L, (int)row);
        const Rect soloR = rowSoloRect(L, (int)row);

        roundedRect(cr, muteR.x, muteR.y, muteR.w, muteR.h, 3.0);
        setColor(cr, r.muted ? kLoopRange : kBtnBg, state.hoverMuteRow == (int)row ? 1.0 : (r.muted ? 0.9 : 0.6));
        cairo_fill_preserve(cr);
        setColor(cr, kPanelEdge, 0.7);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        setFont(cr, 9.5);
        setColor(cr, r.muted ? kBg : kTextDim);
        centeredText(cr, "M", muteR.x + muteR.w / 2.0, muteR.y + muteR.h / 2.0);

        roundedRect(cr, soloR.x, soloR.y, soloR.w, soloR.h, 3.0);
        setColor(cr, r.soloed ? kAccent : kBtnBg, state.hoverSoloRow == (int)row ? 1.0 : (r.soloed ? 0.9 : 0.6));
        cairo_fill_preserve(cr);
        setColor(cr, kPanelEdge, 0.7);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        setFont(cr, 9.5);
        setColor(cr, r.soloed ? kBg : kTextDim);
        centeredText(cr, "S", soloR.x + soloR.w / 2.0, soloR.y + soloR.h / 2.0);
    }

    // vertical grid lines: bar lines brightest, beat lines medium, and (when
    // the current snap resolution is finer than a beat) faint subdivision
    // lines at exactly the selected snap resolution - so what you see is
    // what you'll snap to, not always just a fixed beat grid.
    const uint32_t ticksPerBeat = p.ticksPerBeat();
    const uint32_t ticksPerBar = p.ticksPerBar();
    const uint32_t totalTicks = p.totalTicks();
    const uint32_t step = (state.snapOn && state.snapTicks > 0 && state.snapTicks < ticksPerBeat)
                         ? state.snapTicks : ticksPerBeat;
    if (totalTicks > 0 && step > 0)
    {
        const uint32_t firstTick = (g.scrollTick / step) * step;
        for (uint32_t tick = firstTick; tick <= totalTicks; tick += step)
        {
            const float x = tickToX(L, tick);
            if (x < g.gridX - 1.0f)
                continue;
            if (x > g.gridX + g.gridW + 1.0f)
                break;

            const bool isBar = (tick % ticksPerBar) == 0;
            const bool isBeat = !isBar && (tick % ticksPerBeat) == 0;
            setColor(cr, isBar ? kBarLine : kGridLine, isBar ? 0.9 : (isBeat ? 0.5 : 0.22));
            cairo_set_line_width(cr, isBar ? 1.5 : (isBeat ? 1.0 : 0.75));
            cairo_move_to(cr, x, g.gridY);
            cairo_line_to(cr, x, g.gridY + g.gridH);
            cairo_stroke(cr);
        }
    }

    // empty-cell hover preview - a faint outline at the snapped position a
    // click would place a note, so the (floor-snapped) target is obvious
    // before you commit to it
    if (state.hoverEmptyRow >= 0 && (size_t)state.hoverEmptyRow < p.rows.size())
    {
        const uint32_t len = state.snapOn ? state.snapTicks : kTicksPerQuarter / 4;
        const float x0 = tickToX(L, state.hoverEmptyTick);
        const float x1 = tickToX(L, state.hoverEmptyTick + len);
        const float y = g.gridY + (float)state.hoverEmptyRow * g.rowH + 3.0f;
        const float h = g.rowH - 6.0f;

        roundedRect(cr, x0, y, std::max(3.0f, x1 - x0), h, 3.0);
        setColor(cr, rowAccent((size_t)state.hoverEmptyRow), 0.28);
        cairo_fill_preserve(cr);
        setColor(cr, rowAccent((size_t)state.hoverEmptyRow), 0.55);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    // notes
    for (size_t i = 0; i < p.notes.size(); ++i)
    {
        const Note& n = p.notes[i];
        if (n.rowIndex >= p.rows.size())
            continue;

        const float x1raw = tickToX(L, n.startTick + n.lengthTick);
        const float x0raw = tickToX(L, n.startTick);
        if (x1raw < g.gridX || x0raw > g.gridX + g.gridW)
            continue; // scrolled out of view

        const float x0 = x0raw;
        const float x1 = x1raw;
        const float y = g.gridY + (float)n.rowIndex * g.rowH + 3.0f;
        const float h = g.rowH - 6.0f;
        const Color accent = rowAccent(n.rowIndex);
        const bool audible = rowAudible(p.rows, n.rowIndex);
        const float fade = audible ? 1.0f : 0.4f;

        const bool selected = state.selected != nullptr && state.selected[i];
        const bool hovered = (int)i == state.hoverNote;

        roundedRect(cr, x0, y, std::max(3.0f, x1 - x0), h, 3.0);
        setColor(cr, accent, (hovered ? 0.95 : 0.8) * fade);
        cairo_fill_preserve(cr);
        setColor(cr, selected ? kTextMain : accent, (selected ? 1.0 : 0.6) * fade);
        cairo_set_line_width(cr, selected ? 2.0 : 1.0);
        cairo_stroke(cr);

        // velocity indicator - a bar at the note's left edge, height
        // proportional to velocity/127, same idea as Ardour's per-note
        // velocity line, so loudness is visible without a separate lane
        const float velFrac = std::clamp((float)n.velocity / 127.0f, 0.0f, 1.0f);
        const float velBarW = std::min(4.0f, std::max(2.0f, (x1 - x0) * 0.3f));
        const float velBarH = h * velFrac;
        setColor(cr, kBg, 0.9);
        cairo_rectangle(cr, x0 + 1.5f, y + h - velBarH, velBarW, velBarH);
        cairo_fill(cr);
    }

    // playhead
    if (state.hostPlaying && totalTicks > 0)
    {
        const float x = tickToX(L, state.playheadTick % totalTicks);
        if (x >= g.gridX && x <= g.gridX + g.gridW)
        {
            setColor(cr, kTextMain, 0.9);
            cairo_set_line_width(cr, 2.0);
            cairo_move_to(cr, x, g.gridY);
            cairo_line_to(cr, x, g.gridY + g.gridH);
            cairo_stroke(cr);
        }
    }

    // rubber-band selection rect
    if (state.rubberBandActive)
    {
        const float x = std::min(state.rubberX0, state.rubberX1);
        const float y = std::min(state.rubberY0, state.rubberY1);
        const float w = std::fabs(state.rubberX1 - state.rubberX0);
        const float h = std::fabs(state.rubberY1 - state.rubberY0);
        setColor(cr, kAccent, 0.15);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill_preserve(cr);
        setColor(cr, kAccent, 0.8);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    // outer grid border
    setColor(cr, kPanelEdge, 0.7);
    cairo_set_line_width(cr, 1.5);
    cairo_rectangle(cr, g.gridX, g.gridY, g.gridW, g.gridH);
    cairo_stroke(cr);

    paintScrollbar(cr, L, state);
}

} // namespace ui
} // namespace ostinateous
