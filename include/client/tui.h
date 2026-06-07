#pragma once

#include "data.h"
#include "network.h"
#include <vector>

// Each function runs a full-screen FTXUI loop and returns when complete.
namespace mag::tui {

// Spin until the server responds to MAG_WHO broadcasts.
ServerInfo screen_discover(int udp_port);

// Registration form.  Shows the passphrase after a successful register.
// Returns the registered Student (may be a reconnect).
Student screen_register(HttpClient& cli);

// Spinner + countdown.  Polls GET /targets every 5 s.  Returns once targets
// are assigned.
std::vector<Target> screen_wait(HttpClient& cli, const Student& student);

// Full hunt loop: target menu -> passphrase entry -> Q&A -> repeat until done.
void screen_hunt(HttpClient& cli, const Student& student,
                 std::vector<Target> targets);

// Final stats display.
void screen_stats(HttpClient& cli, const Student& student);

// Master-mode screen: register phantom students, list them, mark found.
void screen_master(HttpClient& cli);

} // namespace mag::tui
