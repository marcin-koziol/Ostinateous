/*
 * Ostinateous - piano-roll grid UI. Layout/paint logic lives in ui/GridPainter.hpp
 * (shared with the offline PNG renderer); this file wires mouse/keyboard/
 * scroll input to Pattern edits and view (zoom/scroll) state, pushes the
 * edited Pattern to the DSP side via DPF's state mechanism (UI::setState ->
 * Plugin::setState, see OstinateousPlugin.cpp), and receives live transport
 * feedback (playhead/playing/bpm) via DPF's output-parameter notification
 * (UI::parameterChanged()) - the same mechanism sideous-drums uses for its
 * LED indicators, just carrying continuous transport data instead of
 * per-voice trigger flags.
 *
 * Interaction model:
 *  - click empty grid cell -> add a note there (snapped), select it
 *  - click an existing note -> select it (replacing selection unless a
 *    modifier... kept simple for v1, no multi-select modifier yet)
 *  - drag from an existing note -> move it (or resize, if the drag started
 *    near its right edge); dragging any selected note moves the whole
 *    selection together
 *  - drag from empty space (grid or outside it) -> rubber-band select; a
 *    short click instead (below the movement threshold) adds a note if it
 *    started on an empty grid cell
 *  - drag in the ruler -> define/replace the loop range (auto-enables it);
 *    a plain click with no movement selects that bar as the fill target
 *    instead (loop range is only ever cleared via the LOOP toggle button)
 *  - drag the scrollbar thumb, or scroll wheel over the grid -> pan;
 *    Ctrl+wheel, or the toolbar zoom buttons -> zoom
 *  - Delete/Backspace -> remove selected notes
 *  - toolbar: bar count +/-, snap on/off + resolution, loop on/off, zoom
 *  - pattern row: Prev/Next cycles [7 genres, then user-saved patterns];
 *    Generate re-rolls a fresh variation of the current genre; Save opens
 *    in-place name entry (type, Enter to confirm, Escape to cancel) then
 *    writes to the user library; Delete removes a user-saved pattern;
 *    Export MIDI writes a Standard MIDI File of the current pattern
 *    straight to the configured export folder (no native file dialog -
 *    DPF's dialog goes through xdg-desktop-portal over D-Bus, which turned
 *    out to fail silently both in this session's own test environment and
 *    in the user's real Carla session, likely portal-backend availability
 *    outside our control - so exporting never depends on it); Folder opens
 *    in-place editing of that export folder path (persisted standalone,
 *    see ui/PatternStore.hpp's loadExportFolder/saveExportFolder - not DPF
 *    plugin state, since it's a user preference that should follow them
 *    across every project, not per-instance session state)
 *  - fill row: apply a generated fill directly to the selected bar
 */

#include "DistrhoUI.hpp"
#include "Params.hpp"
#include "dsp/Fills.hpp"
#include "dsp/FactoryPatterns.hpp"
#include "dsp/Generators.hpp"
#include "dsp/MidiFile.hpp"
#include "dsp/Pattern.hpp"
#include "ui/GridPainter.hpp"
#include "ui/PatternStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;
using namespace ostinateous;

// -----------------------------------------------------------------------------------------------------------

static constexpr uint kMinWidth = 900;
static constexpr uint kMinHeight = 540;
static constexpr int kGenreCount = (int)Genre::Count;
static constexpr double kClickThresholdPx = 4.0; // press-release movement below this counts as a click, not a drag

class OstinateousUI : public UI
{
public:
    OstinateousUI()
        : UI(1200, 800)
    {
        // automaticallyScale=false deliberately: for a resizable grid editor,
        // resizing the window should reveal more grid (more bars/rows at the
        // same pixel density, handled by buildLayout() reading the new
        // width/height every repaint) rather than uniformly blowing up a
        // fixed-size image, which was DPF's behavior with it enabled and
        // showed no more musical content - just bigger everything.
        setGeometryConstraints(kMinWidth, kMinHeight, false, false);
        fRng.seed(std::random_device{}());
        fUserPatternNames = ui::listPatterns();
        fExportFolder = ui::loadExportFolder();
        loadLibraryEntry(0);
        rebuildLayout();
    }

protected:
    // ---------------------------------------------------------------------

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "pattern") == 0)
        {
            fPattern = Pattern::deserialize(value);
            fSelected.assign(fPattern.notes.size(), 0);
            clampView();
            rebuildLayout();
            repaint();
        }
    }

    void parameterChanged(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamOutPlayheadFrac:
            fPlayheadFrac = value;
            break;
        case kParamOutPlaying:
            fHostPlaying = value > 0.5f;
            break;
        case kParamOutBpm:
            fHostBpm = value;
            break;
        default:
            return;
        }
        repaint();
    }

    void onResize(const ResizeEvent& ev) override
    {
        UI::onResize(ev);
        rebuildLayout();
    }


    void onCairoDisplay(const CairoGraphicsContext& context) override
    {
        ui::PaintState state;
        state.pattern = &fPattern;
        state.selected = fSelected.empty() ? nullptr : fSelected.data();
        state.hoverNote = fHoverNote;
        state.hoverToolbarBtn = fHoverToolbarBtn;
        state.hoverEmptyRow = fHoverEmptyRow;
        state.hoverEmptyTick = fHoverEmptyTick;
        state.hoverMuteRow = fHoverMuteRow;
        state.hoverSoloRow = fHoverSoloRow;
        state.snapOn = fSnapOn;
        state.snapTicks = fSnapTicks;
        state.rubberBandActive = fDragMode == DragMode::RubberBand;
        state.rubberX0 = (float)fDragStartX;
        state.rubberY0 = (float)fDragStartY;
        state.rubberX1 = (float)fLastMouseX;
        state.rubberY1 = (float)fLastMouseY;
        state.hostPlaying = fHostPlaying;
        state.hostBpm = fHostBpm;
        state.playheadTick = (uint32_t)((double)fPlayheadFrac * (double)fPattern.totalTicks());
        state.loopDragActive = fDragMode == DragMode::LoopRange;
        state.loopDragStartTick = fLoopDragStartTick;
        state.loopDragEndTick = fLoopDragEndTick;
        state.scrollbarDragActive = fDragMode == DragMode::Scrollbar;

        state.currentName = fCurrentName.c_str();
        state.isUserPattern = fIsUserPattern;
        state.editTarget = fEditTarget == EditTarget::PatternName ? ui::PaintState::EditTarget::PatternName
                          : fEditTarget == EditTarget::ExportFolder ? ui::PaintState::EditTarget::ExportFolder
                          : ui::PaintState::EditTarget::None;
        state.editBuffer = fEditBuffer.c_str();
        state.hoverPatternBtn = fHoverPatternBtn;
        state.toast = fToast.empty() ? nullptr : fToast.c_str();

        state.selectedBar = fSelectedBar;
        state.hoverFillBtn = fHoverFillBtn;

        state.defaultVelocity = fDefaultVelocity;
        state.hoverVelocityBox = fHoverVelocityBox;

        ui::paint(context.handle, fLayout, state);
    }

    // ---------------------------------------------------------------------
    // input

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const double mx = ev.pos.getX();
        const double my = ev.pos.getY();

        if (ev.press)
        {
            if (fEditTarget != EditTarget::None)
            {
                // clicking anywhere outside the name/folder field while
                // editing cancels the edit, same as Escape - avoids getting
                // stuck in text-entry mode if the user clicks away
                const bool onNameField = ui::hitRect(fLayout.patternRow.nameLabel, mx, my);
                if (!onNameField)
                {
                    fEditTarget = EditTarget::None;
                    repaint();
                }
            }

            const int patternBtn = hitTestPatternRow(mx, my);
            if (patternBtn >= 0)
            {
                handlePatternRowClick(patternBtn);
                repaint();
                return true;
            }

            const int fillBtn = hitTestFillRow(mx, my);
            if (fillBtn >= 0)
            {
                applyFillToSelectedBar((FillType)fillBtn);
                repaint();
                return true;
            }

            const int toolbarBtn = hitTestToolbar(mx, my);
            if (toolbarBtn >= 0)
            {
                handleToolbarClick(toolbarBtn);
                repaint();
                return true;
            }

            const int muteRow = hitTestRowMute(mx, my);
            if (muteRow >= 0)
            {
                fPattern.rows[(size_t)muteRow].muted = !fPattern.rows[(size_t)muteRow].muted;
                pushPatternToDSP();
                repaint();
                return true;
            }

            const int soloRow = hitTestRowSolo(mx, my);
            if (soloRow >= 0)
            {
                fPattern.rows[(size_t)soloRow].soloed = !fPattern.rows[(size_t)soloRow].soloed;
                pushPatternToDSP();
                repaint();
                return true;
            }

            if (fPattern.totalTicks() > fLayout.geom.visibleTicks
                && ui::hitRect(ui::scrollbarRect(fLayout), mx, my))
            {
                fDragMode = DragMode::Scrollbar;
                fDragStartX = mx;
                fScrollAtDragStart = fView.scrollTick;
                repaint();
                return true;
            }

            if (my >= fLayout.geom.rulerY && my <= fLayout.geom.rulerY + fLayout.geom.rulerH
                && mx >= fLayout.geom.gridX && mx <= fLayout.geom.gridX + fLayout.geom.gridW)
            {
                fDragMode = DragMode::LoopRange;
                fDragStartX = mx;
                fLoopDragStartTick = ui::snapTick(ui::xToTick(fLayout, mx), fSnapTicks, fSnapOn);
                fLoopDragEndTick = fLoopDragStartTick;
                repaint();
                return true;
            }

            bool nearRightEdge = false;
            const int noteIdx = ui::hitTestNote(fLayout, fPattern, mx, my, &nearRightEdge);

            if (noteIdx >= 0)
            {
                if (fSelected.size() != fPattern.notes.size())
                    fSelected.assign(fPattern.notes.size(), 0);

                if (ev.mod & kModifierControl)
                {
                    // Ctrl+click: toggle this note's membership in the
                    // selection without disturbing the rest of it, and
                    // don't start a drag - the click was about selecting,
                    // not moving
                    fSelected[(size_t)noteIdx] = fSelected[(size_t)noteIdx] ? 0 : 1;
                    repaint();
                    return true;
                }

                if (!fSelected[(size_t)noteIdx])
                {
                    std::fill(fSelected.begin(), fSelected.end(), 0);
                    fSelected[(size_t)noteIdx] = 1;
                }

                fDragMode = nearRightEdge ? DragMode::Resize : DragMode::Move;
                fDragStartX = mx;
                fDragStartY = my;
                fDragOrigNotes = fPattern.notes;
                repaint();
                return true;
            }

            // row header clicks that miss the M/S buttons (e.g. on the name
            // text) do nothing rather than falling through to the grid
            // logic below - without this guard, xToTick() clamps any x left
            // of gridX to tick 0, so a header click would silently add a
            // stray note at the start of that row
            const int row = mx >= fLayout.geom.gridX ? ui::yToRow(fLayout, fPattern, my) : -1;
            if (row >= 0)
            {
                if (ev.mod & kModifierShift)
                {
                    // Shift+drag: paint notes across cells, locked to the
                    // row the drag started in (fills a lane, e.g. a full
                    // bar of 1/16 hats, without spilling into neighbors)
                    fDragMode = DragMode::Paint;
                    fPaintRow = (uint32_t)row;
                    paintNoteAt((uint32_t)row, mx);
                    repaint();
                    return true;
                }

            }
            else if (mx < fLayout.geom.gridX && my >= fLayout.geom.gridY)
            {
                // in the row header column but not on a button: swallow the
                // click so it doesn't fall through to rubber-band/add-note
                return true;
            }

            // empty space, inside or outside the grid rows: could turn out
            // to be either a click (add a note, if inside a valid row) or a
            // rubber-band drag - decided on release by how far it moved, so
            // dragging over empty grid area always rubber-band selects
            // instead of only working when started outside the sequencer
            fDragMode = DragMode::RubberBand;
            fDragStartX = mx;
            fDragStartY = my;
            fClickCandidateRow = row;
            std::fill(fSelected.begin(), fSelected.end(), 0);
            repaint();
            return true;
        }

        // release
        if (fDragMode == DragMode::RubberBand)
        {
            const double dragDist = std::hypot(mx - fDragStartX, my - fDragStartY);
            if (dragDist < kClickThresholdPx)
            {
                if (fClickCandidateRow >= 0)
                {
                    // short click on an empty cell: add a note there
                    Note n;
                    n.rowIndex = (uint32_t)fClickCandidateRow;
                    n.startTick = ui::snapTick(ui::xToTick(fLayout, fDragStartX), fSnapTicks, fSnapOn);
                    n.lengthTick = fSnapOn ? fSnapTicks : kTicksPerQuarter / 4;
                    n.velocity = fDefaultVelocity;
                    fPattern.notes.push_back(n);
                    fSelected.assign(fPattern.notes.size(), 0);
                    fSelected.back() = 1;
                    pushPatternToDSP();
                }
                // else: short click outside any row - nothing to add,
                // selection was already cleared on press
            }
            else
            {
                selectNotesInRubberBand();
            }
            fDragMode = DragMode::None;
            repaint();
            return true;
        }
        if (fDragMode == DragMode::LoopRange)
        {
            const uint32_t lo = std::min(fLoopDragStartTick, fLoopDragEndTick);
            const uint32_t hi = std::max(fLoopDragStartTick, fLoopDragEndTick);
            if (hi > lo)
            {
                fPattern.loopStartTick = lo;
                fPattern.loopEndTick = hi;
                fPattern.loopEnabled = true;
                pushPatternToDSP();
            }
            else
            {
                // plain click, no drag: select the bar under the cursor as
                // the fill target instead (loop is only cleared via LOOP)
                const uint32_t ticksPerBar = fPattern.ticksPerBar();
                if (ticksPerBar > 0)
                    fSelectedBar = std::min(fLoopDragStartTick / ticksPerBar, fPattern.barCount - 1);
            }
            fDragMode = DragMode::None;
            repaint();
            return true;
        }
        if (fDragMode == DragMode::Scrollbar)
        {
            fDragMode = DragMode::None;
            repaint();
            return true;
        }
        if (fDragMode != DragMode::None)
        {
            fDragMode = DragMode::None;
            pushPatternToDSP();
            repaint();
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const double mx = ev.pos.getX();
        const double my = ev.pos.getY();
        fLastMouseX = mx;
        fLastMouseY = my;

        if (fDragMode == DragMode::Move || fDragMode == DragMode::Resize)
        {
            const double dxTicks = (mx - fDragStartX) / (fLayout.geom.pixelsPerTick > 0.0f ? fLayout.geom.pixelsPerTick : 1.0);
            const int rowAtCursor = ui::yToRow(fLayout, fPattern, my);
            const int dragStartRow = ui::yToRow(fLayout, fPattern, fDragStartY);
            const int rowDelta = (fDragMode == DragMode::Move && rowAtCursor >= 0 && dragStartRow >= 0)
                                ? rowAtCursor - dragStartRow : 0;

            for (size_t i = 0; i < fPattern.notes.size() && i < fSelected.size(); ++i)
            {
                if (!fSelected[i])
                    continue;
                const Note& orig = fDragOrigNotes[i];
                Note& cur = fPattern.notes[i];

                if (fDragMode == DragMode::Move)
                {
                    const int64_t newStart = (int64_t)orig.startTick + (int64_t)std::llround(dxTicks);
                    cur.startTick = ui::snapTick((uint32_t)std::max<int64_t>(0, newStart), fSnapTicks, fSnapOn);

                    const int64_t newRow = (int64_t)orig.rowIndex + rowDelta;
                    cur.rowIndex = (uint32_t)std::clamp<int64_t>(newRow, 0, (int64_t)fPattern.rows.size() - 1);
                }
                else // Resize
                {
                    const int64_t newLen = (int64_t)orig.lengthTick + (int64_t)std::llround(dxTicks);
                    const uint32_t snapped = ui::snapTick((uint32_t)std::max<int64_t>(1, newLen), fSnapTicks, fSnapOn);
                    cur.lengthTick = std::max<uint32_t>(fSnapOn ? fSnapTicks : 1u, snapped);
                }
            }
            repaint();
            return true;
        }

        if (fDragMode == DragMode::LoopRange)
        {
            fLoopDragEndTick = ui::snapTick(ui::xToTick(fLayout, mx), fSnapTicks, fSnapOn);
            repaint();
            return true;
        }

        if (fDragMode == DragMode::Paint)
        {
            paintNoteAt(fPaintRow, mx);
            repaint();
            return true;
        }

        if (fDragMode == DragMode::Scrollbar)
        {
            const uint32_t totalTicks = fPattern.totalTicks();
            if (totalTicks > 0 && fLayout.geom.gridW > 0.0f)
            {
                const double dxTicksPerPixel = (double)totalTicks / (double)fLayout.geom.gridW;
                const double dTicks = (mx - fDragStartX) * dxTicksPerPixel;
                fView.scrollTick = (uint32_t)std::max(0.0, (double)fScrollAtDragStart + dTicks);
                clampView();
                rebuildLayout();
            }
            repaint();
            return true;
        }

        if (fDragMode == DragMode::RubberBand)
        {
            repaint();
            return true;
        }

        const int newHoverNote = ui::hitTestNote(fLayout, fPattern, mx, my);
        const int newHoverToolbar = hitTestToolbar(mx, my);
        const int newHoverPattern = hitTestPatternRow(mx, my);
        const int newHoverFill = hitTestFillRow(mx, my);
        const bool newHoverVelocity = ui::hitRect(fLayout.toolbar.velocity, mx, my);

        int newHoverEmptyRow = -1;
        uint32_t newHoverEmptyTick = 0;
        if (newHoverNote < 0 && mx >= fLayout.geom.gridX)
        {
            const int row = ui::yToRow(fLayout, fPattern, my);
            if (row >= 0)
            {
                newHoverEmptyRow = row;
                newHoverEmptyTick = ui::snapTick(ui::xToTick(fLayout, mx), fSnapTicks, fSnapOn);
            }
        }
        const int newHoverMuteRow = hitTestRowMute(mx, my);
        const int newHoverSoloRow = hitTestRowSolo(mx, my);

        if (newHoverNote != fHoverNote || newHoverToolbar != fHoverToolbarBtn
            || newHoverPattern != fHoverPatternBtn || newHoverFill != fHoverFillBtn
            || newHoverVelocity != fHoverVelocityBox
            || newHoverEmptyRow != fHoverEmptyRow || newHoverEmptyTick != fHoverEmptyTick
            || newHoverMuteRow != fHoverMuteRow || newHoverSoloRow != fHoverSoloRow)
        {
            fHoverNote = newHoverNote;
            fHoverToolbarBtn = newHoverToolbar;
            fHoverPatternBtn = newHoverPattern;
            fHoverFillBtn = newHoverFill;
            fHoverVelocityBox = newHoverVelocity;
            fHoverEmptyRow = newHoverEmptyRow;
            fHoverEmptyTick = newHoverEmptyTick;
            fHoverMuteRow = newHoverMuteRow;
            fHoverSoloRow = newHoverSoloRow;
            repaint();
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const double mx = ev.pos.getX();
        const double my = ev.pos.getY();
        // some backends report a plain vertical wheel via delta.x rather
        // than delta.y (confirmed in this environment: the existing pan
        // logic below already falls back the same way) - prefer y, but
        // don't assume it's actually populated
        const double wheelDelta = ev.delta.getY() != 0.0 ? ev.delta.getY() : ev.delta.getX();
        const int wheelStep = wheelDelta > 0.0 ? 1 : (wheelDelta < 0.0 ? -1 : 0);

        if (ui::hitRect(fLayout.toolbar.velocity, mx, my))
        {
            fDefaultVelocity = (uint8_t)std::clamp((int)fDefaultVelocity + wheelStep, 1, 127);
            repaint();
            return true;
        }

        const int hoveredNote = ui::hitTestNote(fLayout, fPattern, mx, my);
        if (hoveredNote >= 0 && wheelStep != 0)
        {
            // scrolling over a note adjusts its velocity (and every other
            // selected note's, if the hovered one is part of the current
            // multi-selection) instead of panning - matches the Ardour-style
            // per-note velocity display painted on each note
            const bool hoveredIsSelected = (size_t)hoveredNote < fSelected.size() && fSelected[(size_t)hoveredNote];
            const int step = wheelStep * 3;
            if (hoveredIsSelected)
            {
                for (size_t i = 0; i < fPattern.notes.size() && i < fSelected.size(); ++i)
                    if (fSelected[i])
                        fPattern.notes[i].velocity = (uint8_t)std::clamp((int)fPattern.notes[i].velocity + step, 1, 127);
            }
            else
            {
                Note& n = fPattern.notes[(size_t)hoveredNote];
                n.velocity = (uint8_t)std::clamp((int)n.velocity + step, 1, 127);
            }
            pushPatternToDSP();
            repaint();
            return true;
        }

        if (mx < fLayout.geom.gridX || mx > fLayout.geom.gridX + fLayout.geom.gridW
            || my < fLayout.geom.gridY || my > fLayout.geom.gridY + fLayout.geom.gridH)
            return false;

        if (ev.mod & kModifierControl)
        {
            const float factor = ev.delta.getY() > 0.0 ? 1.25f : (ev.delta.getY() < 0.0 ? 0.8f : 1.0f);
            setZoomAroundCursor(fView.zoom * factor, mx);
        }
        else
        {
            const uint32_t totalTicks = fPattern.totalTicks();
            const double dTicks = (ev.delta.getX() != 0.0 ? ev.delta.getX() : ev.delta.getY())
                                 * (double)totalTicks * 0.02;
            fView.scrollTick = (uint32_t)std::max(0.0, (double)fView.scrollTick + dTicks);
            clampView();
            rebuildLayout();
        }
        repaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (fEditTarget != EditTarget::None)
        {
            if (!ev.press)
                return true; // swallow releases too while editing

            if (ev.key == kKeyEnter || ev.key == kKeyPadEnter)
            {
                finishTextEdit();
                repaint();
                return true;
            }
            if (ev.key == kKeyEscape)
            {
                fEditTarget = EditTarget::None;
                repaint();
                return true;
            }
            if (ev.key == kKeyBackspace || ev.key == kKeyDelete)
            {
                if (!fEditBuffer.empty())
                    fEditBuffer.pop_back();
                repaint();
                return true;
            }
            return true; // swallow everything else while editing
        }

        if (!ev.press)
            return false;

        if (ev.key == kKeyDelete || ev.key == kKeyBackspace)
        {
            if (fSelected.size() == fPattern.notes.size())
            {
                std::vector<Note> kept;
                for (size_t i = 0; i < fPattern.notes.size(); ++i)
                    if (!fSelected[i])
                        kept.push_back(fPattern.notes[i]);
                if (kept.size() != fPattern.notes.size())
                {
                    fPattern.notes = kept;
                    fSelected.assign(fPattern.notes.size(), 0);
                    pushPatternToDSP();
                    repaint();
                    return true;
                }
            }
        }
        return false;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (fEditTarget == EditTarget::None)
            return false;

        // pattern names are short; folder paths need much more room
        const size_t maxLen = fEditTarget == EditTarget::ExportFolder ? 240 : 32;
        if (ev.character >= 32 && fEditBuffer.size() < maxLen)
            fEditBuffer += ev.string;

        repaint();
        return true;
    }

private:
    void rebuildLayout()
    {
        fLayout = ui::buildLayout((float)getWidth(), (float)getHeight(), fPattern, fView);
    }

    void clampView()
    {
        const uint32_t totalTicks = fPattern.totalTicks();
        fView.zoom = std::max(1.0f, fView.zoom);
        if (totalTicks == 0)
        {
            fView.scrollTick = 0;
            return;
        }
        if (fView.scrollTick > totalTicks)
            fView.scrollTick = totalTicks;
        if (fSelectedBar >= fPattern.barCount)
            fSelectedBar = fPattern.barCount > 0 ? fPattern.barCount - 1 : 0;
    }

    void setZoomAroundCursor(float newZoom, double cursorX)
    {
        newZoom = std::max(1.0f, std::min(newZoom, 32.0f));
        const uint32_t tickUnderCursor = ui::xToTick(fLayout, cursorX);

        fView.zoom = newZoom;
        rebuildLayout();

        const float pxPerTick = fLayout.geom.pixelsPerTick;
        if (pxPerTick > 0.0f)
        {
            const double newScroll = (double)tickUnderCursor - (cursorX - fLayout.geom.gridX) / pxPerTick;
            fView.scrollTick = (uint32_t)std::max(0.0, newScroll);
        }
        clampView();
        rebuildLayout();
    }

    void pushPatternToDSP()
    {
        setState("pattern", fPattern.serialize().c_str());
    }

    // -1 none, 0=bar-,1=bar+,2=snapToggle,3..6=snapRes[0..3],7=loopToggle,8=zoomOut,9=zoomIn,10=zoomFit
    int hitTestToolbar(double mx, double my) const noexcept
    {
        const ui::Toolbar& tb = fLayout.toolbar;
        if (ui::hitRect(tb.barMinus, mx, my)) return 0;
        if (ui::hitRect(tb.barPlus, mx, my)) return 1;
        if (ui::hitRect(tb.snapToggle, mx, my)) return 2;
        for (int i = 0; i < 4; ++i)
            if (ui::hitRect(tb.snapRes[i], mx, my))
                return 3 + i;
        if (ui::hitRect(tb.loopToggle, mx, my)) return 7;
        if (ui::hitRect(tb.zoomOut, mx, my)) return 8;
        if (ui::hitRect(tb.zoomIn, mx, my)) return 9;
        if (ui::hitRect(tb.zoomFit, mx, my)) return 10;
        return -1;
    }

    // -1 none, 0=prev,1=next,2=generate,3=save/set,4=delete,5=exportMidi,6=folder
    int hitTestPatternRow(double mx, double my) const noexcept
    {
        const ui::PatternRow& pr = fLayout.patternRow;
        if (ui::hitRect(pr.prev, mx, my)) return 0;
        if (ui::hitRect(pr.next, mx, my)) return 1;
        if (ui::hitRect(pr.generate, mx, my)) return 2;
        if (ui::hitRect(pr.save, mx, my)) return 3;
        if (ui::hitRect(pr.del, mx, my)) return 4;
        if (ui::hitRect(pr.exportMidi, mx, my)) return 5;
        if (ui::hitRect(pr.folder, mx, my)) return 6;
        return -1;
    }

    int hitTestFillRow(double mx, double my) const noexcept
    {
        const ui::FillRow& fr = fLayout.fillRow;
        for (int i = 0; i < 6; ++i)
            if (ui::hitRect(fr.fillBtn[i], mx, my))
                return i;
        return -1;
    }

    int hitTestRowMute(double mx, double my) const noexcept
    {
        const int row = ui::yToRow(fLayout, fPattern, my);
        if (row < 0)
            return -1;
        return ui::hitRect(ui::rowMuteRect(fLayout, row), mx, my) ? row : -1;
    }

    int hitTestRowSolo(double mx, double my) const noexcept
    {
        const int row = ui::yToRow(fLayout, fPattern, my);
        if (row < 0)
            return -1;
        return ui::hitRect(ui::rowSoloRect(fLayout, row), mx, my) ? row : -1;
    }

    void handleToolbarClick(int btn)
    {
        static constexpr uint32_t kResTicks[4] = {
            kTicksPerQuarter, kTicksPerQuarter / 2, kTicksPerQuarter / 4, kTicksPerQuarter / 8
        };

        switch (btn)
        {
        case 0:
            if (fPattern.barCount > 1)
            {
                --fPattern.barCount;
                const uint32_t total = fPattern.totalTicks();
                std::vector<Note> kept;
                for (const Note& n : fPattern.notes)
                    if (n.startTick < total)
                        kept.push_back(n);
                fPattern.notes = kept;
                fSelected.assign(fPattern.notes.size(), 0);
                clampView();
                rebuildLayout();
                pushPatternToDSP();
            }
            break;
        case 1:
            ++fPattern.barCount;
            clampView();
            rebuildLayout();
            pushPatternToDSP();
            break;
        case 2:
            fSnapOn = !fSnapOn;
            break;
        case 3: case 4: case 5: case 6:
            fSnapTicks = kResTicks[btn - 3];
            break;
        case 7:
            fPattern.loopEnabled = !fPattern.loopEnabled;
            pushPatternToDSP();
            break;
        case 8:
            setZoomAroundCursor(fView.zoom * 0.8f, fLayout.geom.gridX + fLayout.geom.gridW * 0.5f);
            break;
        case 9:
            setZoomAroundCursor(fView.zoom * 1.25f, fLayout.geom.gridX + fLayout.geom.gridW * 0.5f);
            break;
        case 10:
            fView.zoom = 1.0f;
            fView.scrollTick = 0;
            rebuildLayout();
            break;
        default:
            break;
        }
    }

    // ---------------------------------------------------------------------
    // pattern library

    int libraryCount() const noexcept { return kGenreCount + (int)fUserPatternNames.size(); }

    void loadLibraryEntry(int index)
    {
        const int count = libraryCount();
        if (count <= 0)
            return;
        index = ((index % count) + count) % count;
        fLibraryIndex = index;
        fToast.clear();

        if (index < kGenreCount)
        {
            fCurrentGenre = (Genre)index;
            fPattern = buildFactoryPattern(fCurrentGenre);
            fCurrentName = genreName(fCurrentGenre);
            fIsUserPattern = false;
        }
        else
        {
            const std::string& name = fUserPatternNames[(size_t)(index - kGenreCount)];
            Pattern loaded;
            if (ui::loadPattern(name, loaded))
            {
                fPattern = loaded;
                fCurrentName = name;
                fIsUserPattern = true;
            }
        }

        fSelected.assign(fPattern.notes.size(), 0);
        fSelectedBar = 0;
        fView = ui::View();
        clampView();
        rebuildLayout();
        pushPatternToDSP();
    }

    void handlePatternRowClick(int id)
    {
        switch (id)
        {
        case 0: loadLibraryEntry(fLibraryIndex - 1); break;
        case 1: loadLibraryEntry(fLibraryIndex + 1); break;
        case 2:
        {
            std::uniform_int_distribution<uint32_t> seedDist;
            fPattern = generateVariation(fCurrentGenre, fPattern.barCount > 0 ? fPattern.barCount : 4, seedDist(fRng));
            fCurrentName = genreName(fCurrentGenre);
            fIsUserPattern = false;
            fSelected.assign(fPattern.notes.size(), 0);
            fSelectedBar = 0;
            clampView();
            rebuildLayout();
            pushPatternToDSP();
            break;
        }
        case 3:
            if (fEditTarget == EditTarget::None)
            {
                fEditTarget = EditTarget::PatternName;
                fEditBuffer = fIsUserPattern ? fCurrentName : std::string();
            }
            else
            {
                finishTextEdit();
            }
            break;
        case 4:
            if (fIsUserPattern)
            {
                ui::deletePattern(fCurrentName);
                fUserPatternNames = ui::listPatterns();
                loadLibraryEntry(0);
            }
            break;
        case 5:
            exportMidiNow();
            break;
        case 6:
            if (fEditTarget == EditTarget::None)
            {
                fEditTarget = EditTarget::ExportFolder;
                fEditBuffer = fExportFolder;
            }
            else
            {
                finishTextEdit();
            }
            break;
        default:
            break;
        }
    }

    // confirms whichever in-place text field is currently active - pattern
    // name (Save) or export folder (Folder), the two never overlap
    void finishTextEdit()
    {
        if (fEditTarget == EditTarget::PatternName)
        {
            std::string name = ui::sanitizeName(fEditBuffer.empty() ? fCurrentName : fEditBuffer);
            ui::savePattern(name, fPattern);
            fUserPatternNames = ui::listPatterns();
            fCurrentName = name;
            fIsUserPattern = true;
            fToast = "Saved";

            for (size_t i = 0; i < fUserPatternNames.size(); ++i)
                if (fUserPatternNames[i] == name)
                {
                    fLibraryIndex = kGenreCount + (int)i;
                    break;
                }
        }
        else if (fEditTarget == EditTarget::ExportFolder)
        {
            std::string path = fEditBuffer;
            const size_t start = path.find_first_not_of(' ');
            if (start == std::string::npos)
                path = fExportFolder; // blank input: leave it unchanged
            else
                path = path.substr(start, path.find_last_not_of(' ') - start + 1);

            fExportFolder = path;
            fToast = ui::saveExportFolder(path) ? "Export folder set" : "Couldn't save folder setting";
        }
        fEditTarget = EditTarget::None;
    }

    double currentExportBpm() const noexcept
    {
        return fHostBpm > 0.0f ? (double)fHostBpm : 120.0;
    }

    bool writeMidiFileTo(const std::string& path)
    {
        const std::vector<uint8_t> bytes = writeStandardMidiFile(fPattern, currentExportBpm());
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open())
            return false;
        out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        return out.good();
    }

    // writes straight to the (user-configurable, see the Folder button) export
    // folder with an auto-generated name - no native save dialog involved,
    // see the file-level comment for why that's deliberate
    void exportMidiNow()
    {
        ui::createDirectories(fExportFolder);

        std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));

        char filename[128];
        std::snprintf(filename, sizeof(filename), "%s-%s.mid", ui::sanitizeName(fCurrentName).c_str(), stamp);

        const std::string path = fExportFolder + "/" + filename;
        fToast = writeMidiFileTo(path) ? (std::string("Exported: ") + filename) : "Export failed - check folder path";
    }

    // ---------------------------------------------------------------------
    // fills

    void applyFillToSelectedBar(FillType fill)
    {
        std::uniform_int_distribution<uint32_t> seedDist;
        applyFill(fPattern, fSelectedBar, fill, seedDist(fRng));
        fSelected.assign(fPattern.notes.size(), 0);
        pushPatternToDSP();
    }

    // Shift+drag paint: adds a note at (row, snapped tick under mx) unless
    // one is already there - called once on press and again on every motion
    // event while painting, so it's idempotent by construction rather than
    // needing to track "already painted this stroke" separately.
    void paintNoteAt(uint32_t row, double mx)
    {
        const uint32_t tick = ui::snapTick(ui::xToTick(fLayout, mx), fSnapTicks, fSnapOn);
        for (const Note& n : fPattern.notes)
            if (n.rowIndex == row && n.startTick == tick)
                return;

        Note n;
        n.rowIndex = row;
        n.startTick = tick;
        n.lengthTick = fSnapOn ? fSnapTicks : kTicksPerQuarter / 4;
        n.velocity = fDefaultVelocity;
        fPattern.notes.push_back(n);
        fSelected.assign(fPattern.notes.size(), 0);
    }

    void selectNotesInRubberBand()
    {
        const float x0 = (float)std::min(fDragStartX, fLastMouseX);
        const float x1 = (float)std::max(fDragStartX, fLastMouseX);
        const float y0 = (float)std::min(fDragStartY, fLastMouseY);
        const float y1 = (float)std::max(fDragStartY, fLastMouseY);

        fSelected.assign(fPattern.notes.size(), 0);
        for (size_t i = 0; i < fPattern.notes.size(); ++i)
        {
            const Note& n = fPattern.notes[i];
            const float nx0 = ui::tickToX(fLayout, n.startTick);
            const float nx1 = ui::tickToX(fLayout, n.startTick + n.lengthTick);
            const float ny0 = fLayout.geom.gridY + (float)n.rowIndex * fLayout.geom.rowH;
            const float ny1 = ny0 + fLayout.geom.rowH;

            if (nx0 <= x1 && nx1 >= x0 && ny0 <= y1 && ny1 >= y0)
                fSelected[i] = 1;
        }
    }

    Pattern fPattern;
    ui::Layout fLayout;
    ui::View fView;
    std::vector<uint8_t> fSelected;
    std::vector<Note> fDragOrigNotes;

    int fHoverNote = -1;
    int fHoverToolbarBtn = -1;
    int fHoverEmptyRow = -1;
    uint32_t fHoverEmptyTick = 0;
    int fHoverMuteRow = -1;
    int fHoverSoloRow = -1;

    bool fSnapOn = true;
    uint32_t fSnapTicks = kTicksPerQuarter / 4; // 1/16 default

    float fPlayheadFrac = 0.0f;
    bool fHostPlaying = false;
    float fHostBpm = 0.0f;

    // pattern library
    std::mt19937 fRng;
    Genre fCurrentGenre = Genre::Techno;
    int fLibraryIndex = 0;
    std::vector<std::string> fUserPatternNames;
    std::string fCurrentName = "Techno";
    bool fIsUserPattern = false;
    enum class EditTarget { None, PatternName, ExportFolder };
    EditTarget fEditTarget = EditTarget::None;
    std::string fEditBuffer;
    std::string fExportFolder;
    std::string fToast;
    int fHoverPatternBtn = -1;

    // fills
    uint32_t fSelectedBar = 0;
    int fHoverFillBtn = -1;

    // note insertion / velocity editing
    uint8_t fDefaultVelocity = 100;
    bool fHoverVelocityBox = false;

    enum class DragMode { None, Move, Resize, RubberBand, LoopRange, Scrollbar, Paint };
    DragMode fDragMode = DragMode::None;
    double fDragStartX = 0.0, fDragStartY = 0.0;
    double fLastMouseX = 0.0, fLastMouseY = 0.0;
    uint32_t fLoopDragStartTick = 0, fLoopDragEndTick = 0;
    uint32_t fScrollAtDragStart = 0;
    uint32_t fPaintRow = 0;
    int fClickCandidateRow = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstinateousUI)
};

// -----------------------------------------------------------------------------------------------------------

UI* createUI()
{
    return new OstinateousUI();
}

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
