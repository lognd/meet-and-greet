#include "data.h"
#include "log.h"
#include "network.h"
#include "tui.h"

#include <ftxui/component/screen_interactive.hpp>

#include <cstdint>
#include <iostream>
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

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct Args {
    bool     master{false};
    bool     headless{false};
    uint64_t headless_id{0};
    int      udp_timeout{15};      // seconds; used in headless discovery
    std::string server_addr;       // "ip:port" override - skips UDP discovery
};

static Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--master") {
            a.master = true;
        } else if (arg == "--headless") {
            a.headless = true;
            if (i + 1 < argc) {
                try { a.headless_id = std::stoull(argv[++i]); }
                catch (...) { --i; }
            }
        } else if (arg == "--server" && i + 1 < argc) {
            a.server_addr = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            try { a.udp_timeout = std::stoi(argv[++i]); } catch (...) {}
        }
    }
    return a;
}

// ---------------------------------------------------------------------------
// Headless mode - non-interactive, machine-readable output for test harness
// ---------------------------------------------------------------------------

// Output format (one key=value per line, terminated by blank line):
//   UUID=<uuid>
//   PASS=<passphrase>
//   NEW=<0|1>
//
// Exit codes:  0 = success
//              1 = server not found / connection refused
//              2 = registration failed
//              3 = bad arguments

static mag::HttpClient resolve_server(const Args& args) {
    mag::HttpClient cli;

    if (!args.server_addr.empty()) {
        auto colon = args.server_addr.rfind(':');
        if (colon == std::string::npos) {
            std::cerr << "ERROR: bad --server format (expected ip:port)\n";
            std::exit(3);
        }
        cli.host = args.server_addr.substr(0, colon);
        cli.port = std::stoi(args.server_addr.substr(colon + 1));
    } else {
        auto server = mag::discover_server(MAG_UDP_PORT, args.udp_timeout);
        if (!server) {
            std::cerr << "ERROR: server_not_found\n";
            std::exit(1);
        }
        cli.host = server->ip;
        cli.port = server->port;
    }
    return cli;
}

static int run_headless(const Args& args) {
    mag::HttpClient cli = resolve_server(args);

    uint64_t sid = args.headless_id ? args.headless_id : 99999999ULL;
    auto student = cli.register_student(mag::encrypt_id(sid), "Headless", "Test");
    if (!student) {
        std::cerr << "ERROR: registration_failed\n";
        return 2;
    }

    std::cout << "UUID=" << student->uuid       << "\n"
              << "PASS=" << student->passphrase  << "\n"
              << "NEW="  << (student->is_new ? "1" : "0") << "\n"
              << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    enable_ansi();
    mag::log::init();
    LOG("startup");

    Args args = parse_args(argc, argv);
    LOG("headless", args.headless);
    LOG("master",   args.master);

    if (args.headless) return run_headless(args);

    if (args.master) {
        // Master mode discovers the server, then runs its own TUI.
        LOG("phase", "discover_headless");
        auto server = mag::discover_server(MAG_UDP_PORT, 30);
        if (!server) {
            std::cerr << "ERROR: server not found\n";
            return 1;
        }
        LOG("server_ip",   server->ip);
        LOG("server_port", server->port);
        mag::HttpClient cli{server->ip, server->port};
        LOG("phase", "master");
        mag::tui::screen_master(cli);
        return 0;
    }

    // Student mode: one ScreenInteractive for the whole session.
    // run_tui uses a single screen.Loop() with a swappable router component
    // so the terminal alternate-screen buffer is never toggled between screens.
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    mag::tui::run_tui(screen, MAG_UDP_PORT);

    LOG("done");
    return 0;
}
