/*
 * Ostinateous - output-only parameter indices. Unlike the rest of the Sideous
 * family (all automatable knobs), Ostinateous has no automatable input params -
 * the whole pattern is edited as one piece of state (see OstinateousPlugin.cpp).
 * These three exist purely to carry live transport feedback from the DSP
 * side to the UI (playhead cursor, play/stop, tempo readout), using the same
 * output-parameter + UI::parameterChanged() mechanism sideous-drums already
 * uses for its LED indicators - a real, host-notified channel, not polling.
 */

#pragma once

namespace ostinateous {

enum Params
{
    kParamOutPlayheadFrac = 0, // 0..1, fraction of pattern.totalTicks()
    kParamOutPlaying,          // 0 or 1
    kParamOutBpm,              // host tempo, for display only
    kParamCount
};

} // namespace ostinateous
