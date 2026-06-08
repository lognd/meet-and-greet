#include "tui.h"
#include "log.h"
#include "network.h"
#include "data.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace ftxui;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static Element header(const std::string& title) {
    return vbox({
        text("  MEET AND GREET  ") | bold | color(Color::Cyan) | hcenter,
        text(" " + title + " ")   | bold | color(Color::Yellow) | hcenter,
        separator(),
    });
}

static void stop_and_join(std::atomic<bool>& flag, std::thread& t) {
    flag = true;
    if (t.joinable()) t.join();
}

// ---------------------------------------------------------------------------
// Announcement overlay
// ---------------------------------------------------------------------------

struct AnnState {
    std::mutex  mtx;
    std::string message;
    double      last_since{0.0};
    bool        show_modal{false};
    bool        stop{false};
};

// Wraps inner with an announcement modal. Returns the poll thread.
// Caller must set ann.stop=true and join the thread before the enclosing
// screen.Loop() call's screen is used again, to prevent PostEvent on stale
// state. dismiss_btn is captured by value so the lambda is safe after return.
static std::thread with_announcements(
    Component inner,
    Component& out,
    mag::HttpClient& cli,
    AnnState& ann,
    ScreenInteractive& screen)
{
    auto dismiss_btn = Button("  Dismiss  ", [&ann] {
        std::lock_guard<std::mutex> lk(ann.mtx);
        ann.show_modal = false;
    });

    auto modal_body = Renderer(dismiss_btn, [&ann, dismiss_btn] {
        std::string msg;
        { std::lock_guard<std::mutex> lk(ann.mtx); msg = ann.message; }
        return vbox({
            text("  *** ANNOUNCEMENT ***  ") | bold | color(Color::Red) | hcenter,
            separator(),
            text(""),
            paragraph(msg) | hcenter,
            text(""),
            dismiss_btn->Render() | hcenter,
        }) | border | color(Color::Red);
    });

    out = Modal(std::move(inner), modal_body, &ann.show_modal);

    return std::thread([&cli, &ann, &screen] {
        while (true) {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(500ms);
                { std::lock_guard<std::mutex> lk(ann.mtx); if (ann.stop) return; }
            }
            auto anns = cli.get_announcements(ann.last_since);
            for (const auto& a : anns) {
                std::lock_guard<std::mutex> lk(ann.mtx);
                if (ann.stop) return;
                ann.message    = a.message;
                ann.last_since = a.sent_at;
                ann.show_modal = true;
                screen.PostEvent(Event::Custom);
            }
        }
    });
}

// ---------------------------------------------------------------------------
// screen_discover
// ---------------------------------------------------------------------------

namespace mag::tui {

ServerInfo screen_discover(ScreenInteractive& screen, int udp_port) {
    LOG("screen_discover: start");

    ServerInfo result;
    std::atomic<bool> found{false};
    std::atomic<int>  ticks{0};

    std::thread discover_thrd([&] {
        while (!found.load()) {
            auto info = discover_server(udp_port, 1);
            if (info && !found.load()) {
                result = *info;
                found  = true;
                screen.PostEvent(Event::Custom);
            }
        }
    });

    std::thread ticker_thrd([&] {
        while (!found.load()) {
            std::this_thread::sleep_for(200ms);
            if (found.load()) break;
            ++ticks;
            screen.PostEvent(Event::Custom);
        }
    });

    const std::string spin = "|/-\\";
    auto renderer = Renderer([&] {
        return vbox({
            header("SERVER DISCOVERY"),
            text(""),
            hbox({
                text("  " + std::string(1, spin[ticks.load() % 4]) + "  ")
                    | color(Color::Yellow),
                text("Searching for MAG server on the LAN..."),
            }) | hcenter,
            text(""),
            text("(Make sure the server is running and you are on the same Wi-Fi)")
                | color(Color::GrayDark) | hcenter,
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event) {
        if (found.load()) { screen.ExitLoopClosure()(); }
        return false;
    });

    screen.Loop(component);
    discover_thrd.join();
    ticker_thrd.join();
    LOG("screen_discover: server found");
    return result;
}

// ---------------------------------------------------------------------------
// screen_register
// ---------------------------------------------------------------------------

Student screen_register(ScreenInteractive& screen, HttpClient& cli) {
    LOG("screen_register: start");
    std::optional<Student> result;

    // --- Registration form ---
    {
        LOG("screen_register: registration form");
        std::string id_str, forename, surname, error_msg;
        bool submitting = false;

        auto id_input = Input(&id_str,   "e.g.  10012345");
        auto fn_input = Input(&forename, "e.g.  Jane");
        auto sn_input = Input(&surname,  "e.g.  Smith");

        auto do_submit = [&] {
            LOG("screen_register: submit");
            error_msg.clear();
            if (id_str.empty() || forename.empty() || surname.empty()) {
                error_msg = "All fields are required."; return;
            }
            uint64_t sid = 0;
            try { sid = std::stoull(id_str); }
            catch (...) { error_msg = "Student ID must be a number."; return; }
            submitting = true;
            std::thread([&, sid] {
                auto r = cli.register_student(encrypt_id(sid), forename, surname);
                submitting = false;
                if (!r) {
                    LOG("screen_register: network error");
                    error_msg = "Network error - is the server running?";
                } else {
                    LOG("screen_register: ok, is_new", r->is_new);
                    result = r;
                }
                screen.PostEvent(Event::Custom);
            }).detach();
        };

        auto submit_btn = Button("  Register  ", do_submit);
        auto inputs = Container::Vertical({id_input, fn_input, sn_input, submit_btn});

        auto form = CatchEvent(Renderer(inputs, [&] {
            Elements rows = {
                header("REGISTRATION"),
                text(""),
                text(" Student ID") | bold,
                id_input->Render() | border,
                text(" First name") | bold,
                fn_input->Render() | border,
                text(" Last name") | bold,
                sn_input->Render() | border,
                text(""),
            };
            if (!error_msg.empty())
                rows.push_back(text(" " + error_msg) | color(Color::Red));
            if (submitting)
                rows.push_back(text(" Connecting...") | color(Color::Yellow));
            rows.push_back(text(""));
            rows.push_back(submit_btn->Render() | hcenter);
            rows.push_back(text(""));
            rows.push_back(
                text(" Tab between fields  |  Enter to submit")
                    | color(Color::GrayDark) | hcenter);
            return vbox(rows) | border;
        }), [&](Event) {
            if (result) { screen.ExitLoopClosure()(); }
            return false;
        });

        screen.Loop(form);

        // Reconnect with name mismatch: offer to update
        if (result && !result->is_new) {
            bool differs = (result->forename != forename || result->surname != surname);
            if (differs) {
                auto yes = Button("  Yes, update  ", [&] {
                    cli.update_name(result->uuid, forename, surname);
                    result->forename = forename;
                    result->surname  = surname;
                    screen.ExitLoopClosure()();
                });
                auto no = Button("  Keep stored name  ", [&] {
                    screen.ExitLoopClosure()();
                });
                auto btns = Container::Horizontal({yes, no});
                screen.Loop(Renderer(btns, [&] {
                    return vbox({
                        header("WELCOME BACK"),
                        text(""),
                        text(" Stored name:  " + result->forename + " " + result->surname),
                        text(" Entered name: " + forename + " " + surname),
                        text(""),
                        text(" Update to the newly entered name?") | bold,
                        text(""),
                        hbox({yes->Render(), text("   "), no->Render()}) | hcenter,
                    }) | border;
                }));
            }
        }
    }

    // --- Passphrase display ---
    {
        LOG("screen_register: passphrase display");
        auto cont = Button("  I have it - Continue  ", [&] {
            screen.ExitLoopClosure()();
        });
        screen.Loop(Renderer(cont, [&] {
            return vbox({
                header("YOUR SECRET PASSPHRASE"),
                text(""),
                text(" Welcome, " + result->forename + " " + result->surname + "!")
                    | color(Color::Green) | bold,
                text(""),
                text(" Your passphrase is:") | bold,
                text(""),
                text("   >>> " + result->passphrase + " <<<")
                    | bold | color(Color::Yellow) | hcenter,
                text(""),
                text(" Write it down or memorise it.")
                    | color(Color::GrayDark),
                text(" Other students will ask for it when they find you.")
                    | color(Color::GrayDark),
                text(""),
                cont->Render() | hcenter,
            }) | border;
        }));
    }

    LOG("screen_register: done");
    return *result;
}

// ---------------------------------------------------------------------------
// screen_wait
// ---------------------------------------------------------------------------

std::vector<Target> screen_wait(ScreenInteractive& screen, HttpClient& cli,
                                const Student& student) {
    std::vector<Target> result;
    std::atomic<bool>   found{false};
    std::string         time_label = "Waiting for session to start...";
    std::mutex          label_mtx;

    std::thread poll_thrd([&] {
        while (!found.load()) {
            auto targets = cli.get_targets(student.uuid);
            if (!targets.empty() && !found.load()) {
                result = targets;
                found  = true;
                screen.PostEvent(Event::Custom);
                return;
            }
            auto tinfo = cli.get_time();
            if (tinfo && tinfo->remaining >= 0) {
                int s = static_cast<int>(tinfo->remaining);
                std::lock_guard<std::mutex> lk(label_mtx);
                time_label = std::to_string(s / 60) + "m "
                           + std::to_string(s % 60) + "s remaining";
            }
            screen.PostEvent(Event::Custom);
            for (int i = 0; i < 10 && !found.load(); ++i)
                std::this_thread::sleep_for(500ms);
        }
    });

    std::atomic<int>  ticks{0};
    std::atomic<bool> ticker_stop{false};
    std::thread ticker_thrd([&] {
        while (!ticker_stop.load()) {
            std::this_thread::sleep_for(250ms);
            if (ticker_stop.load()) break;
            ++ticks;
            screen.PostEvent(Event::Custom);
        }
    });

    const std::string spin = "|/-\\";
    AnnState ann;

    auto inner = Renderer([&] {
        std::string tl;
        { std::lock_guard<std::mutex> lk(label_mtx); tl = time_label; }
        return vbox({
            header("WAITING FOR TARGETS"),
            text(""),
            hbox({
                text("  " + std::string(1, spin[ticks.load() % 4]) + "  ")
                    | color(Color::Yellow),
                text("Waiting for staff to assign targets..."),
            }) | hcenter,
            text(""),
            text(" " + tl) | color(Color::GrayDark) | hcenter,
            text(""),
            separator(),
            text(" Your passphrase (share when asked):") | bold,
            text("   " + student.passphrase) | bold | color(Color::Yellow),
        }) | border;
    });

    Component with_ann;
    auto ann_thrd = with_announcements(inner, with_ann, cli, ann, screen);

    screen.Loop(CatchEvent(with_ann, [&](Event) {
        if (found.load()) { screen.ExitLoopClosure()(); }
        return false;
    }));

    found = true;
    ticker_stop = true;
    { std::lock_guard<std::mutex> lk(ann.mtx); ann.stop = true; }
    poll_thrd.join();
    ticker_thrd.join();
    ann_thrd.join();
    return result;
}

// ---------------------------------------------------------------------------
// screen_hunt helpers
// ---------------------------------------------------------------------------

static std::string passphrase_entry_screen(ScreenInteractive& screen) {
    std::string passphrase;
    bool cancelled = false;

    auto input      = Input(&passphrase, "e.g.  eagle-has-landed");
    auto submit_btn = Button("  Authenticate  ", [&] {
        if (!passphrase.empty()) screen.ExitLoopClosure()();
    });
    auto cancel_btn = Button("  Cancel  ", [&] {
        cancelled = true; screen.ExitLoopClosure()();
    });
    auto btns = Container::Horizontal({submit_btn, cancel_btn});
    auto c    = Container::Vertical({input, btns});

    screen.Loop(Renderer(c, [&] {
        return vbox({
            header("ENTER PASSPHRASE"),
            text(""),
            text(" Ask them: \"What is your passphrase?\"")
                | color(Color::GrayDark),
            text(""),
            text(" Their passphrase:") | bold,
            input->Render() | border,
            text(""),
            hbox({submit_btn->Render(), text("   "), cancel_btn->Render()})
                | hcenter,
        }) | border;
    }));
    return cancelled ? "" : passphrase;
}

// Shows questions as ice-breakers (no answer entry).
// Returns true if the user confirmed the meeting, false if cancelled.
static bool icebreaker_screen(ScreenInteractive& screen,
                              const std::string& target_name,
                              const std::vector<std::string>& questions)
{
    bool confirmed = false;
    auto ok  = Button("  We met! Record it  ", [&] {
        confirmed = true; screen.ExitLoopClosure()();
    });
    auto cancel = Button("  Cancel  ", [&] { screen.ExitLoopClosure()(); });
    auto btns = Container::Horizontal({ok, cancel});

    screen.Loop(Renderer(btns, [&] {
        Elements rows = {
            header("YOU FOUND " + target_name + "!"),
            text(""),
            text(" Chat about these together:") | bold,
            text(""),
        };
        for (size_t i = 0; i < questions.size(); ++i)
            rows.push_back(
                text("  " + std::to_string(i + 1) + ".  " + questions[i])
                    | color(Color::Cyan));
        rows.push_back(text(""));
        rows.push_back(
            hbox({ok->Render(), text("   "), cancel->Render()}) | hcenter);
        return vbox(rows) | border;
    }));
    return confirmed;
}

// ---------------------------------------------------------------------------
// screen_hunt
// ---------------------------------------------------------------------------

void screen_hunt(ScreenInteractive& screen, HttpClient& cli,
                 const Student& student, std::vector<Target> targets)
{
    // Track met UUIDs from the server's perspective (includes symmetric).
    std::set<std::string> known_met;
    int completed = 0;
    int total = static_cast<int>(targets.size());

    // Seed known_met from current stats so a reconnect doesn't re-notify.
    if (auto s = cli.get_stats(student.uuid)) {
        completed = s->completed;
        known_met = std::set<std::string>(s->met_uuids.begin(), s->met_uuids.end());
        // Remove already-met targets from the visible list.
        targets.erase(std::remove_if(targets.begin(), targets.end(),
            [&](const Target& t) { return known_met.count(t.uuid); }),
            targets.end());
    }

    while (completed < total && !targets.empty()) {
        int selected = 0;
        std::vector<std::string> labels;
        for (const auto& t : targets)
            labels.push_back(t.forename + " " + t.surname
                             + "  (hint: " + t.passphrase_hint + "-...)");

        bool do_meet     = false;
        bool show_mypass = false;

        // Symmetric-meeting notification: names of partners who found us
        // since we last checked. Set by the stats-poll thread.
        struct SymNotif {
            std::mutex mtx;
            std::vector<std::string> names;
            bool show{false};
            bool stop{false};
        } sym;

        AnnState ann;

        auto menu     = Menu(&labels, &selected);
        auto meet_btn = Button("  Meet this person  ", [&] {
            do_meet = true; screen.ExitLoopClosure()();
        });
        auto mypass_btn = Button("  Show my passphrase  ", [&] {
            show_mypass = true; screen.ExitLoopClosure()();
        });
        auto btns = Container::Horizontal({meet_btn, mypass_btn});
        auto c    = Container::Vertical({menu, btns});

        std::string time_label;
        std::mutex  tl_mtx;

        // Stats / time poller: also detects symmetric meetings.
        std::atomic<bool> tp_stop{false};
        std::thread stats_poller([&] {
            while (!tp_stop.load()) {
                // Time
                auto tinfo = cli.get_time();
                if (tinfo && tinfo->remaining >= 0) {
                    int s = static_cast<int>(tinfo->remaining);
                    std::lock_guard<std::mutex> lk(tl_mtx);
                    time_label = std::to_string(s / 60) + "m "
                               + std::to_string(s % 60) + "s remaining";
                }
                // Symmetric meeting detection
                auto stats = cli.get_stats(student.uuid);
                if (stats) {
                    std::vector<std::string> new_names;
                    for (const auto& uid : stats->met_uuids) {
                        if (known_met.count(uid)) continue;
                        // Check it is one of our remaining targets
                        auto it = std::find_if(targets.begin(), targets.end(),
                            [&](const Target& t) { return t.uuid == uid; });
                        if (it == targets.end()) continue;
                        known_met.insert(uid);
                        new_names.push_back(it->forename + " " + it->surname);
                    }
                    if (!new_names.empty()) {
                        std::lock_guard<std::mutex> lk(sym.mtx);
                        for (auto& n : new_names) sym.names.push_back(std::move(n));
                        sym.show = true;
                        completed = stats->completed;
                        screen.PostEvent(Event::Custom);
                    }
                }
                for (int i = 0; i < 10 && !tp_stop.load(); ++i)
                    std::this_thread::sleep_for(500ms);
            }
        });

        // Symmetric-meeting modal
        auto sym_dismiss = Button("  Got it  ", [&] {
            std::lock_guard<std::mutex> lk(sym.mtx);
            sym.names.clear();
            sym.show = false;
        });
        auto sym_modal_body = Renderer(sym_dismiss, [&sym, sym_dismiss] {
            std::vector<std::string> names;
            { std::lock_guard<std::mutex> lk(sym.mtx); names = sym.names; }
            Elements rows = {
                text("  They found you!  ") | bold | color(Color::Green) | hcenter,
                separator(),
                text(""),
            };
            for (const auto& n : names)
                rows.push_back(text("  " + n + " recorded meeting with you.")
                    | color(Color::Green));
            rows.push_back(text(""));
            rows.push_back(sym_dismiss->Render() | hcenter);
            return vbox(rows) | border | color(Color::Green);
        });

        auto inner = Renderer(c, [&] {
            std::string tl;
            { std::lock_guard<std::mutex> lk(tl_mtx); tl = time_label; }
            return vbox({
                header("FIND YOUR TARGETS"),
                text(""),
                hbox({
                    text(" Progress: ") | bold,
                    text(std::to_string(completed) + " / "
                         + std::to_string(total) + " found")
                        | color(Color::Green),
                    filler(),
                    text(tl) | color(Color::Red),
                    text("  "),
                }),
                separator(),
                text(""),
                text(" Your targets (arrow keys to navigate):") | bold,
                text(""),
                menu->Render() | border,
                text(""),
                text(" Your passphrase: ") | color(Color::GrayDark),
                text("   " + student.passphrase) | bold | color(Color::Yellow),
                text(""),
                hbox({meet_btn->Render(), text("   "), mypass_btn->Render()})
                    | hcenter,
                text(""),
                text(" Arrow keys to navigate  |  Enter to select button")
                    | color(Color::GrayDark) | hcenter,
            }) | border;
        });

        // Layer: sym-met modal on top of ann modal on top of inner.
        Component with_ann;
        auto ann_thrd = with_announcements(inner, with_ann, cli, ann, screen);
        auto with_sym = Modal(with_ann, sym_modal_body, &sym.show);

        screen.Loop(CatchEvent(with_sym, [&](Event) {
            // Symmetric meeting: remove newly-met targets from list now.
            {
                std::lock_guard<std::mutex> lk(sym.mtx);
                if (!sym.names.empty()) {
                    targets.erase(std::remove_if(targets.begin(), targets.end(),
                        [&](const Target& t) { return known_met.count(t.uuid); }),
                        targets.end());
                    labels.clear();
                    for (const auto& t : targets)
                        labels.push_back(t.forename + " " + t.surname
                                         + "  (hint: " + t.passphrase_hint + "-...)");
                    if (selected >= static_cast<int>(targets.size()))
                        selected = std::max(0, static_cast<int>(targets.size()) - 1);
                }
            }
            if (completed >= total) { screen.ExitLoopClosure()(); }
            return false;
        }));

        tp_stop = true;
        stats_poller.join();
        { std::lock_guard<std::mutex> lk(ann.mtx); ann.stop = true; }
        ann_thrd.join();

        if (completed >= total) break;
        if (show_mypass) {
            auto ok = Button("  Back  ", [&] { screen.ExitLoopClosure()(); });
            screen.Loop(Renderer(ok, [&] {
                return vbox({
                    header("YOUR PASSPHRASE"),
                    text(""),
                    text("   " + student.passphrase)
                        | bold | color(Color::Yellow) | hcenter,
                    text(""),
                    ok->Render() | hcenter,
                }) | border;
            }));
            continue;
        }
        if (!do_meet || selected >= static_cast<int>(targets.size())) continue;

        // --- Passphrase entry ---
        std::string passphrase = passphrase_entry_screen(screen);
        if (passphrase.empty()) continue;

        // --- Authenticate with server ---
        std::string target_uuid, target_forename, error;
        auto questions = cli.meet(student.uuid, passphrase,
                                  target_uuid, target_forename, error);
        if (questions.empty()) {
            // Wrong passphrase / already met / not a target
            auto ok = Button("  Try again  ", [&] { screen.ExitLoopClosure()(); });
            screen.Loop(Renderer(ok, [&, error] {
                return vbox({
                    header("NOT FOUND"),
                    text(""),
                    text(" " + error) | color(Color::Red) | hcenter,
                    text(""),
                    ok->Render() | hcenter,
                }) | border;
            }));
            continue;
        }

        // --- Ice-breaker questions ---
        if (!icebreaker_screen(screen, target_forename, questions)) continue;

        // --- Record meeting ---
        // Submit with no written answers; questions are verbal ice-breakers.
        int new_completed = 0, new_total = 0;
        bool ok = cli.submit_answers(student.uuid, target_uuid, {},
                                     new_completed, new_total);
        if (!ok) {
            // 409: partner already recorded this meeting (symmetric race).
            // The server already has the record; refresh via stats.
            if (auto s = cli.get_stats(student.uuid)) {
                new_completed = s->completed;
                for (const auto& uid : s->met_uuids) known_met.insert(uid);
            }
        }

        completed = new_completed;
        known_met.insert(target_uuid);
        targets.erase(std::remove_if(targets.begin(), targets.end(),
            [&](const Target& t) { return t.uuid == target_uuid; }),
            targets.end());

        auto cont = Button("  Keep going!  ", [&] { screen.ExitLoopClosure()(); });
        screen.Loop(Renderer(cont, [&] {
            return vbox({
                header("MEETING RECORDED"),
                text(""),
                text(" You met " + target_forename + "!")
                    | color(Color::Green) | bold | hcenter,
                text(""),
                text(" Progress: " + std::to_string(completed)
                     + " / " + std::to_string(new_total)) | hcenter,
                text(""),
                cont->Render() | hcenter,
            }) | border;
        }));
    }
}

// ---------------------------------------------------------------------------
// screen_stats
// ---------------------------------------------------------------------------

void screen_stats(ScreenInteractive& screen, HttpClient& cli,
                  const Student& student)
{
    auto stats = cli.get_stats(student.uuid);
    auto exit  = Button("  Exit  ", [&] { screen.ExitLoopClosure()(); });

    screen.Loop(Renderer(exit, [&] {
        Elements rows = {
            header("ALL DONE!"),
            text(""),
            text(" You found all your targets - great work!")
                | color(Color::Green) | bold | hcenter,
            text(""),
        };
        if (stats) {
            rows.push_back(
                text(" Meetings: " + std::to_string(stats->completed)
                     + " / " + std::to_string(stats->total)) | hcenter);
            if (stats->place > 0) {
                rows.push_back(text(""));
                rows.push_back(
                    text(" Finish place: " + stats->ordinal + "!")
                        | bold | color(Color::Yellow) | hcenter);
            }
        }
        rows.push_back(text(""));
        rows.push_back(separator());
        rows.push_back(
            text(" Your passphrase (in case others still need it):")
                | color(Color::GrayDark) | hcenter);
        rows.push_back(
            text("   " + student.passphrase)
                | bold | color(Color::Yellow) | hcenter);
        rows.push_back(text(""));
        rows.push_back(exit->Render() | hcenter);
        return vbox(rows) | border;
    }));
}

} // namespace mag::tui
