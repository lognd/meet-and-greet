#include "data.h"
#include "network.h"
#include "tui.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
static void enable_ansi() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
static void enable_ansi() {}
#endif

int main(int argc, char* argv[]) {
    enable_ansi();

    bool master_mode = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--master") master_mode = true;

    // Phase 1: discover server via UDP broadcast
    mag::ServerInfo server = mag::tui::screen_discover(MAG_UDP_PORT);

    mag::HttpClient cli{server.ip, server.port};

    if (master_mode) {
        mag::tui::screen_master(cli);
        return 0;
    }

    // Phase 2: register (or reconnect)
    mag::Student student = mag::tui::screen_register(cli);

    // Phase 3: wait for targets to be assigned
    auto targets = mag::tui::screen_wait(cli, student);

    // Phase 4: hunt and meet all targets
    mag::tui::screen_hunt(cli, student, targets);

    // Phase 5: stats
    mag::tui::screen_stats(cli, student);

    return 0;
}
