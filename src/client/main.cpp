// Meet and Greet client.
// Compile: cmake -B build -DMAG_XORKEY="..." && cmake --build build
// Run:     ./build/mag_client [--master]

#include "data.h"
#include "network.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  define CLEAR_SCREEN() system("cls")
#else
#  define CLEAR_SCREEN() std::cout << "\033[2J\033[H"
#endif

// ---- terminal helpers --------------------------------------------------------

static void set_bold(bool on) {
    std::cout << (on ? "\033[1m" : "\033[0m");
}

static void set_color(int code) {
    std::cout << "\033[" << code << "m";
}

static void reset_color() {
    std::cout << "\033[0m";
}

static std::string ordinal_suffix(int n) {
    if (n <= 0) return std::to_string(n);
    int mod100 = n % 100;
    if (mod100 >= 11 && mod100 <= 13) return std::to_string(n) + "th";
    switch (n % 10) {
        case 1: return std::to_string(n) + "st";
        case 2: return std::to_string(n) + "nd";
        case 3: return std::to_string(n) + "rd";
        default: return std::to_string(n) + "th";
    }
}

// Read a trimmed line from stdin.
static std::string read_line(const std::string& prompt) {
    std::cout << prompt;
    std::string s;
    std::getline(std::cin, s);
    // trim
    auto start = s.find_first_not_of(" \t\r\n");
    auto end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static void print_banner() {
    CLEAR_SCREEN();
    set_color(36); set_bold(true);
    std::cout << "====================================\n";
    std::cout << "     MEET AND GREET  (MAG v1.0)     \n";
    std::cout << "====================================\n";
    reset_color();
    std::cout << "\n";
}

static void print_box(const std::string& title, const std::vector<std::string>& lines) {
    set_color(33); set_bold(true);
    std::cout << "[ " << title << " ]\n";
    reset_color();
    for (const auto& l : lines) std::cout << "  " << l << "\n";
    std::cout << "\n";
}

// ---- announcement poller ----------------------------------------------------

struct AnnPoller {
    mag::HttpClient& cli;
    std::atomic<bool> stop{false};
    double last_since{0.0};
    std::thread thr;

    AnnPoller(mag::HttpClient& c) : cli(c) {
        thr = std::thread([this]{ run(); });
    }

    ~AnnPoller() {
        stop = true;
        if (thr.joinable()) thr.join();
    }

    void run() {
        using namespace std::chrono_literals;
        while (!stop) {
            auto anns = cli.get_announcements(last_since);
            for (const auto& a : anns) {
                // Interrupt display with a full-screen announcement
                CLEAR_SCREEN();
                set_color(31); set_bold(true);
                std::cout << "\n\n  *** ANNOUNCEMENT ***\n\n";
                reset_color();
                std::cout << "  " << a.message << "\n\n";
                set_color(33);
                std::cout << "  (press Enter to continue)\n";
                reset_color();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                last_since = a.sent_at;
            }
            std::this_thread::sleep_for(5s);
        }
    }
};

// ---- master mode state ------------------------------------------------------

struct PhantomStudent {
    std::string uuid;
    std::string forename;
    std::string surname;
    uint64_t student_id;
    std::string passphrase;
    bool found{false};
};

static std::vector<PhantomStudent> g_phantoms;
static const char* MASTER_STATE_FILE = "master_state.json";

// We do a minimal JSON serialise/deserialise without a full library in main.cpp
// (nlohmann/json is only linked in http.cpp; include it here as well).
#include <nlohmann/json.hpp>
using json = nlohmann::json;

static void save_phantoms() {
    json arr = json::array();
    for (const auto& p : g_phantoms) {
        arr.push_back({
            {"uuid", p.uuid},
            {"forename", p.forename},
            {"surname", p.surname},
            {"student_id", p.student_id},
            {"passphrase", p.passphrase},
            {"found", p.found},
        });
    }
    std::ofstream f(MASTER_STATE_FILE);
    f << arr.dump(2);
}

static void load_phantoms() {
    std::ifstream f(MASTER_STATE_FILE);
    if (!f) return;
    try {
        json arr = json::parse(f);
        for (const auto& p : arr) {
            g_phantoms.push_back({
                p.value("uuid", ""),
                p.value("forename", ""),
                p.value("surname", ""),
                p.value("student_id", uint64_t{0}),
                p.value("passphrase", ""),
                p.value("found", false),
            });
        }
    } catch (...) {}
}

// ---- discovery phase --------------------------------------------------------

static mag::ServerInfo do_discovery() {
    print_banner();
    std::cout << "Searching for server on the LAN...\n";
    std::cout << "(If the server is not running yet, I will keep trying.)\n\n";

    while (true) {
        auto info = mag::discover_server(MAG_UDP_PORT, 30);
        if (info) return *info;
        std::cout << "  Still searching...\n";
    }
}

// ---- registration phase -----------------------------------------------------

static mag::Student do_register(mag::HttpClient& cli) {
    print_banner();
    print_box("REGISTRATION", {
        "Please enter your details below.",
        "Format your name as it appears on your student ID.",
    });

    while (true) {
        std::string id_str = read_line("Student ID (numbers only): ");
        uint64_t sid = 0;
        try { sid = std::stoull(id_str); }
        catch (...) { std::cout << "  Invalid ID. Please try again.\n\n"; continue; }

        std::string enc = mag::encrypt_id(sid);

        std::string forename = read_line("First name: ");
        std::string surname  = read_line("Last name:  ");

        if (forename.empty() || surname.empty()) {
            std::cout << "  Name cannot be blank. Try again.\n\n";
            continue;
        }

        std::cout << "\nConnecting...\n";
        auto result = cli.register_student(enc, forename, surname);
        if (!result) {
            std::cout << "  Network error. Retrying...\n\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        mag::Student s = *result;

        if (!s.is_new) {
            print_banner();
            print_box("WELCOME BACK", {
                "Found your account: " + s.forename + " " + s.surname,
            });
            if (s.forename != forename || s.surname != surname) {
                std::cout << "  You entered a different name:\n";
                std::cout << "    Stored:  " << s.forename << " " << s.surname << "\n";
                std::cout << "    Entered: " << forename << " " << surname << "\n\n";
                std::string choice = read_line("Update to new name? [y/N]: ");
                if (!choice.empty() && (choice[0] == 'y' || choice[0] == 'Y')) {
                    if (cli.update_name(s.uuid, forename, surname)) {
                        s.forename = forename;
                        s.surname  = surname;
                        std::cout << "  Name updated.\n\n";
                    }
                }
            }
        }

        CLEAR_SCREEN();
        print_banner();
        set_color(32); set_bold(true);
        std::cout << "Welcome, " << s.forename << " " << s.surname << "!\n\n";
        reset_color();
        std::cout << "Your secret passphrase is:\n\n";
        set_color(33); set_bold(true);
        std::cout << "    >>> " << s.passphrase << " <<<\n\n";
        reset_color();
        std::cout << "Write this down or memorize it.\n";
        std::cout << "Other students will ask for it when they find you.\n\n";
        read_line("Press Enter when ready...");
        return s;
    }
}

// ---- waiting phase ----------------------------------------------------------

static std::vector<mag::Target> do_wait(mag::HttpClient& cli, const mag::Student& student) {
    using namespace std::chrono_literals;
    print_banner();
    std::cout << "Waiting for everyone to join and for targets to be assigned...\n";
    std::cout << "Your passphrase: ";
    set_color(33); set_bold(true);
    std::cout << student.passphrase;
    reset_color();
    std::cout << "\n\n";

    while (true) {
        auto targets = cli.get_targets(student.uuid);
        if (!targets.empty()) return targets;

        // Show remaining time
        auto tinfo = cli.get_time();
        if (tinfo && tinfo->remaining >= 0) {
            int secs = static_cast<int>(tinfo->remaining);
            int m = secs / 60, s = secs % 60;
            std::cout << "\r  Time remaining: " << m << "m " << s << "s    " << std::flush;
        } else {
            std::cout << "\r  Still waiting...    " << std::flush;
        }
        std::this_thread::sleep_for(5s);
    }
}

// ---- hunting phase ----------------------------------------------------------

static void do_hunt(mag::HttpClient& cli, const mag::Student& student) {
    using namespace std::chrono_literals;

    auto targets = do_wait(cli, student);

    AnnPoller poller(cli);

    size_t current = 0;
    int completed  = 0;
    int total      = static_cast<int>(targets.size());

    auto show_target = [&]() {
        CLEAR_SCREEN();
        print_banner();

        // Progress
        set_color(32);
        std::cout << "Progress: " << completed << " / " << total << " found\n\n";
        reset_color();

        // Countdown
        auto tinfo = cli.get_time();
        if (tinfo && tinfo->remaining >= 0) {
            int secs = static_cast<int>(tinfo->remaining);
            int m = secs / 60, s = secs % 60;
            set_color(31);
            std::cout << "Time remaining: " << m << "m " << s << "s\n\n";
            reset_color();
        }

        // Current target
        const auto& t = targets[current];
        print_box("YOUR CURRENT TARGET", {
            t.forename + " " + t.surname,
            "Passphrase hint: " + t.passphrase_hint + "-...",
        });

        std::cout << "Controls:\n";
        std::cout << "  [n] Next target    [p] Previous target\n";
        std::cout << "  [e] Enter passphrase (when you find them)\n";
        std::cout << "  [q] Show my passphrase\n\n";
        std::cout << "> ";
    };

    while (completed < total) {
        show_target();
        std::string cmd = read_line("");

        if (cmd == "n" || cmd == "N") {
            current = (current + 1) % targets.size();
        } else if (cmd == "p" || cmd == "P") {
            current = (current == 0) ? targets.size() - 1 : current - 1;
        } else if (cmd == "q" || cmd == "Q") {
            CLEAR_SCREEN();
            print_banner();
            std::cout << "Your passphrase: ";
            set_color(33); set_bold(true);
            std::cout << student.passphrase;
            reset_color();
            std::cout << "\n\n";
            read_line("Press Enter to go back...");
        } else if (cmd == "e" || cmd == "E") {
            std::string passphrase = read_line("Enter their passphrase: ");
            std::string target_uuid, target_forename, error;
            auto questions = cli.meet(student.uuid, passphrase, target_uuid, target_forename, error);

            if (questions.empty()) {
                std::cout << "\n  Error: " << error << "\n";
                read_line("Press Enter to try again...");
                continue;
            }

            // Found! Collect answers.
            CLEAR_SCREEN();
            print_banner();
            set_color(32); set_bold(true);
            std::cout << "You found " << target_forename << "!\n\n";
            reset_color();
            std::cout << "Please answer these get-to-know-you questions together:\n\n";

            std::vector<mag::Question> answers;
            int qi = 1;
            for (const auto& q : questions) {
                std::cout << qi++ << ". " << q << "\n";
                std::string ans = read_line("   Your answer: ");
                answers.push_back({q, ans});
                std::cout << "\n";
            }

            std::cout << "Submitting...\n";
            int new_completed = 0, new_total = 0;
            bool ok = cli.submit_answers(student.uuid, target_uuid, answers, new_completed, new_total);
            if (ok) {
                completed = new_completed;
                // Remove found target from display list
                targets.erase(targets.begin() + static_cast<int>(current));
                if (!targets.empty()) current %= targets.size();

                CLEAR_SCREEN();
                print_banner();
                set_color(32); set_bold(true);
                std::cout << "Meeting recorded! " << completed << " / " << new_total << " done.\n\n";
                reset_color();
                if (completed < new_total)
                    read_line("Keep going! Press Enter to continue...");
            } else {
                std::cout << "  Failed to record meeting (server error).\n";
                read_line("Press Enter to retry...");
            }
        }
    }
}

// ---- stats phase ------------------------------------------------------------

static void do_stats(mag::HttpClient& cli, const mag::Student& student) {
    CLEAR_SCREEN();
    print_banner();
    set_color(32); set_bold(true);
    std::cout << "You found all your targets!\n\n";
    reset_color();

    auto stats = cli.get_stats(student.uuid);
    if (stats) {
        std::cout << "  Meetings completed: " << stats->completed << " / " << stats->total << "\n";
        if (stats->place > 0) {
            set_color(33); set_bold(true);
            std::cout << "  Finish place: " << ordinal_suffix(stats->place) << "!\n";
            reset_color();
        }
    }
    std::cout << "\nGreat work!\n";
}

// ---- master mode ------------------------------------------------------------

static void run_master(mag::HttpClient& cli) {
    load_phantoms();
    print_banner();
    set_color(35); set_bold(true);
    std::cout << "MASTER CLIENT MODE\n\n";
    reset_color();

    while (true) {
        std::cout << "Commands:\n";
        std::cout << "  [r] Register phantom student\n";
        std::cout << "  [l] List phantoms\n";
        std::cout << "  [m] Mark phantom as found\n";
        std::cout << "  [q] Quit\n\n";
        std::string cmd = read_line("> ");

        if (cmd == "r" || cmd == "R") {
            std::string forename = read_line("First name: ");
            std::string surname  = read_line("Last name:  ");
            std::string id_str   = read_line("Student ID: ");
            uint64_t sid = 0;
            try { sid = std::stoull(id_str); }
            catch (...) { std::cout << "Invalid ID.\n\n"; continue; }

            std::string enc = mag::encrypt_id(sid);
            auto result = cli.register_student(enc, forename, surname);
            if (!result) { std::cout << "Network error.\n\n"; continue; }

            g_phantoms.push_back({result->uuid, result->forename, result->surname, sid, result->passphrase, false});
            save_phantoms();
            std::cout << "Registered: " << forename << " " << surname
                      << "  passphrase: " << result->passphrase << "\n\n";

        } else if (cmd == "l" || cmd == "L") {
            if (g_phantoms.empty()) { std::cout << "No phantoms yet.\n\n"; continue; }
            for (size_t i = 0; i < g_phantoms.size(); ++i) {
                const auto& p = g_phantoms[i];
                std::cout << "  [" << i << "] " << p.forename << " " << p.surname
                          << "  id=" << p.student_id
                          << "  pass=" << p.passphrase
                          << (p.found ? "  [FOUND]" : "") << "\n";
            }
            std::cout << "\n";

        } else if (cmd == "m" || cmd == "M") {
            std::string idx_str = read_line("Phantom index: ");
            int idx = -1;
            try { idx = std::stoi(idx_str); } catch (...) {}
            if (idx < 0 || idx >= static_cast<int>(g_phantoms.size())) {
                std::cout << "Invalid index.\n\n"; continue;
            }
            g_phantoms[idx].found = true;
            save_phantoms();
            std::cout << "Marked as found.\n\n";

        } else if (cmd == "q" || cmd == "Q") {
            break;
        }
    }
}

// ---- main -------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Enable ANSI on Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

    bool master_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--master") master_mode = true;
    }

    // Discovery
    mag::ServerInfo server = do_discovery();

    mag::HttpClient cli;
    cli.host = server.ip;
    cli.port = server.port;

    if (master_mode) {
        run_master(cli);
        return 0;
    }

    // Register
    mag::Student student = do_register(cli);

    // Hunt
    do_hunt(cli, student);

    // Stats
    do_stats(cli, student);

    read_line("\nPress Enter to exit...");
    return 0;
}
