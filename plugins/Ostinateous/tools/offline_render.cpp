// throwaway dev aid, not wired into CMakeLists.txt. Compile ad hoc:
//   g++ $(pkg-config --cflags --libs cairo) -std=c++17 -o /tmp/render offline_render.cpp
// then run it and inspect the PNG it writes.
#include "../ui/GridPainter.hpp"

#include <cstdio>

using namespace ostinateous;

int main(int argc, char** argv)
{
    Pattern pattern;
    pattern.barCount = 4;

    // a small hand-made pattern so the render shows something recognizable
    pattern.notes.push_back({ 0, 0,    120, 120 }); // kick beat 1
    pattern.notes.push_back({ 0, 1920, 120, 110 }); // kick beat 3
    pattern.notes.push_back({ 1, 960,  120, 100 }); // snare beat 2
    pattern.notes.push_back({ 1, 2880, 120, 100 }); // snare beat 4
    for (int i = 0; i < 8; ++i)
        pattern.notes.push_back({ 2, (uint32_t)i * 480, 100, (uint8_t)(70 + i * 5) }); // closed hat 8ths
    pattern.notes.push_back({ 7, 3840 + 3360, 800, 127 }); // crash at the top of bar 2

    pattern.loopEnabled = true;
    pattern.loopStartTick = pattern.ticksPerBar();     // bar 2
    pattern.loopEndTick = pattern.ticksPerBar() * 3;   // through bar 3

    const float width = 1200.0f, height = 720.0f;
    ui::View view;
    ui::Layout layout = ui::buildLayout(width, height, pattern, view);

    std::printf("layout: grid at (%.1f,%.1f) size %.1fx%.1f, pxPerTick=%.4f\n",
                layout.geom.gridX, layout.geom.gridY, layout.geom.gridW, layout.geom.gridH, layout.geom.pixelsPerTick);

    std::vector<uint8_t> selected(pattern.notes.size(), 0);
    selected[2] = 1; // show one selected note in the render

    ui::PaintState state;
    state.pattern = &pattern;
    state.selected = selected.data();
    state.hoverNote = 5;
    state.snapOn = true;
    state.snapTicks = kTicksPerQuarter / 4;
    state.hostPlaying = true;
    state.hostBpm = 128.0;
    state.playheadTick = 1500;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)width, (int)height);
    cairo_t* cr = cairo_create(surface);
    ui::paint(cr, layout, state);

    const char* outPath = argc > 1 ? argv[1] : "/tmp/ostinateous_render.png";
    cairo_surface_write_to_png(surface, outPath);
    std::printf("wrote %s\n", outPath);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return 0;
}
