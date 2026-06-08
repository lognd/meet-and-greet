#pragma once

#include "data.h"
#include "network.h"
#include <ftxui/component/screen_interactive.hpp>

// All TUI logic runs inside run_tui() using a single ScreenInteractive::Loop()
// so the terminal alternate-screen buffer is never toggled between screens
// (no flash).
namespace mag::tui {

// Student-facing full UI: discover -> register -> wait -> hunt -> stats.
void run_tui(ftxui::ScreenInteractive& screen, int udp_port);

// Staff-only master mode.  Uses its own ScreenInteractive internally.
void screen_master(HttpClient& cli);

} // namespace mag::tui
