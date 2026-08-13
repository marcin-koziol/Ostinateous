#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Ostinateous"
#define DISTRHO_PLUGIN_NAME    "Ostinateous"
#define DISTRHO_PLUGIN_URI     "https://github.com/marcin-koziol/ostinateous"
#define DISTRHO_PLUGIN_CLAP_ID "ostinateous.ostinateous"

#define DISTRHO_PLUGIN_BRAND_ID  Osts
#define DISTRHO_PLUGIN_UNIQUE_ID Osq2

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      0
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1

#define DISTRHO_UI_USE_CAIRO             1
#define DISTRHO_UI_DEFAULT_WIDTH         1200
#define DISTRHO_UI_DEFAULT_HEIGHT        720
#define DISTRHO_UI_USER_RESIZABLE        1
// automaticallyScale (see GridUI.cpp's UI constructor call to
// setGeometryConstraints) leans on DPF's own host-DPI-scale compositing
// instead of hand-rolled cairo_scale()/mouse-coordinate math here.
// DISTRHO_UI_FILE_BROWSER/USE_FILE_BROWSER deliberately NOT enabled: DPF's
// native file dialog goes through xdg-desktop-portal over D-Bus, which
// failed silently both in this session's test environment and the user's
// real Carla session (portal-backend availability is outside our control) -
// MIDI export writes straight to a user-configurable folder instead (see
// GridUI.cpp's exportMidiNow()/ui::loadExportFolder), no dialog needed.

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
