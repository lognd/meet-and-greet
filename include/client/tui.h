#pragma once

#include "data.h"
#include "network.h"
#include <ftxui/component/screen_interactive.hpp>
#include <vector>

// All screen functions share one ScreenInteractive created in main() so the
// terminal is never torn down and re-entered between screens (no flash).
namespace mag::tui {

// Spin until the server responds to MAG_WHO broadcasts.
ServerInfo screen_discover(ftxui::ScreenInteractive&, int udp_port);

// Registration form + passphrase display.  Returns the registered Student.
Student screen_register(ftxui::ScreenInteractive&, HttpClient&);

// Spinner + countdown. Polls GET /targets every 5 s. Returns once targets
// are assigned; returns empty vector if user quits (Ctrl+C).
std::vector<Target> screen_wait(ftxui::ScreenInteractive&, HttpClient&,
                                const Student&);

// Full hunt loop: target menu -> passphrase entry -> ice-breaker -> repeat.
// Handles symmetric meetings (partner found us) via stats polling.
void screen_hunt(ftxui::ScreenInteractive&, HttpClient&, const Student&,
                 std::vector<Target> targets);

// Final stats display.
void screen_stats(ftxui::ScreenInteractive&, HttpClient&, const Student&);

// Master-mode screen.
void screen_master(HttpClient&);

} // namespace mag::tui
