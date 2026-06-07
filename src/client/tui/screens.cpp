#include "tui.h"
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

// ---------------------------------------------------------------------------
// Announcement overlay state
// ---------------------------------------------------------------------------

struct AnnState {
    std::mutex  mtx;
    std::string message;
    double      last_since{0.0};
    bool        show_modal{false};  // read/written under mtx, also the bool* Modal needs
};

// Wraps `inner` with a modal overlay that appears when announcements arrive.
// A detached background thread polls; caller must ensure cli outlives the screen.
static Component with_announcements(
    Component inner,
    mag::HttpClient& cli,
    AnnState& ann,
    ScreenInteractive& screen)
{
    std::thread([&]() {
        while (true) {
            std::this_thread::sleep_for(5s);
            auto anns = cli.get_announcements(ann.last_since);
            for (const auto& a : anns) {
                std::lock_guard<std::mutex> lk(ann.mtx);
                ann.message    = a.message;
                ann.last_since = a.sent_at;
                ann.show_modal = true;
                screen.PostEvent(Event::Custom);
            }
        }
    }).detach();

    auto dismiss_btn = Button("  Dismiss  ", [&] {
        std::lock_guard<std::mutex> lk(ann.mtx);
        ann.show_modal = false;
    });

    auto modal_renderer = Renderer(dismiss_btn, [&] {
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

    return Modal(inner, modal_renderer, &ann.show_modal);
}

// ---------------------------------------------------------------------------
// screen_discover
// ---------------------------------------------------------------------------

namespace mag::tui {

ServerInfo screen_discover(int udp_port) {
    auto screen = ScreenInteractive::Fullscreen();

    ServerInfo result;
    std::atomic<bool> found{false};
    std::atomic<int>  ticks{0};

    std::thread([&]() {
        while (!found.load()) {
            auto info = discover_server(udp_port, 3);
            if (info) {
                result = *info;
                found  = true;
                screen.PostEvent(Event::Custom);
            }
        }
    }).detach();

    std::thread([&]() {
        while (!found.load()) {
            std::this_thread::sleep_for(200ms);
            ++ticks;
            screen.PostEvent(Event::Custom);
        }
    }).detach();

    const std::string spinner_chars = "|/-\\";
    auto renderer = Renderer([&] {
        std::string spin(1, spinner_chars[ticks.load() % 4]);
        return vbox({
            header("SERVER DISCOVERY"),
            text(""),
            hbox({
                text("  " + spin + "  ") | color(Color::Yellow),
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
    return result;
}

// ---------------------------------------------------------------------------
// screen_register
// ---------------------------------------------------------------------------

Student screen_register(HttpClient& cli) {
    std::optional<Student> result;

    // --- Registration form ---
    {
        auto screen = ScreenInteractive::Fullscreen();
        std::string id_str, forename, surname, error_msg;
        bool submitting = false;

        auto id_input  = Input(&id_str,   "e.g.  10012345");
        auto fn_input  = Input(&forename, "e.g.  Jane");
        auto sn_input  = Input(&surname,  "e.g.  Smith");

        auto do_submit = [&]() {
            error_msg.clear();
            if (id_str.empty() || forename.empty() || surname.empty()) {
                error_msg = "All fields are required."; return;
            }
            uint64_t sid = 0;
            try { sid = std::stoull(id_str); }
            catch (...) { error_msg = "Student ID must be a number."; return; }
            submitting = true;
            auto r = cli.register_student(encrypt_id(sid), forename, surname);
            submitting = false;
            if (!r) { error_msg = "Network error - is the server running?"; return; }
            result = r;
            screen.ExitLoopClosure()();
        };

        auto submit_btn = Button("  Register  ", do_submit);
        auto inputs = Container::Vertical({id_input, fn_input, sn_input, submit_btn});

        screen.Loop(Renderer(inputs, [&] {
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
                text(" Tab between fields  |  Enter to submit") | color(Color::GrayDark) | hcenter);
            return vbox(rows) | border;
        }));

        // If reconnect with name mismatch, offer to update
        if (result && !result->is_new) {
            bool differs = (result->forename != forename || result->surname != surname);
            if (differs) {
                auto ns = ScreenInteractive::Fullscreen();
                auto yes = Button("  Yes, update  ", [&] {
                    cli.update_name(result->uuid, forename, surname);
                    result->forename = forename;
                    result->surname  = surname;
                    ns.ExitLoopClosure()();
                });
                auto no = Button("  Keep stored name  ", [&] { ns.ExitLoopClosure()(); });
                auto btns = Container::Horizontal({yes, no});
                ns.Loop(Renderer(btns, [&] {
                    return vbox({
                        header("WELCOME BACK"),
                        text(""),
                        text(" Stored name:   " + result->forename + " " + result->surname),
                        text(" Entered name:  " + forename + " " + surname),
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
        auto ps = ScreenInteractive::Fullscreen();
        auto cont = Button("  I have it - Continue  ", [&] { ps.ExitLoopClosure()(); });
        ps.Loop(Renderer(cont, [&] {
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
                text(" Write this down or memorise it.") | color(Color::GrayDark),
                text(" Other students will ask for it when they find you.")
                    | color(Color::GrayDark),
                text(""),
                cont->Render() | hcenter,
            }) | border;
        }));
    }

    return *result;
}

// ---------------------------------------------------------------------------
// screen_wait
// ---------------------------------------------------------------------------

std::vector<Target> screen_wait(HttpClient& cli, const Student& student) {
    auto screen = ScreenInteractive::Fullscreen();

    std::vector<Target> result;
    std::atomic<bool>   found{false};
    std::string         time_label = "Waiting for session to start...";
    std::mutex          label_mtx;

    std::thread([&]() {
        while (!found.load()) {
            auto targets = cli.get_targets(student.uuid);
            if (!targets.empty()) {
                result = targets;
                found  = true;
                screen.PostEvent(Event::Custom);
                return;
            }
            auto tinfo = cli.get_time();
            if (tinfo && tinfo->remaining >= 0) {
                int secs = static_cast<int>(tinfo->remaining);
                std::string lbl = std::to_string(secs / 60) + "m "
                                + std::to_string(secs % 60) + "s remaining";
                std::lock_guard<std::mutex> lk(label_mtx);
                time_label = lbl;
            }
            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(5s);
        }
    }).detach();

    std::atomic<int> ticks{0};
    std::thread([&]() {
        while (!found.load()) {
            std::this_thread::sleep_for(250ms);
            ++ticks;
            screen.PostEvent(Event::Custom);
        }
    }).detach();

    const std::string spinner_chars = "|/-\\";
    AnnState ann;

    auto inner = Renderer([&] {
        std::string tl;
        { std::lock_guard<std::mutex> lk(label_mtx); tl = time_label; }
        return vbox({
            header("WAITING FOR TARGETS"),
            text(""),
            hbox({
                text("  " + std::string(1, spinner_chars[ticks.load() % 4]) + "  ")
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

    auto with_ann = with_announcements(inner, cli, ann, screen);

    auto component = CatchEvent(with_ann, [&](Event) {
        if (found.load()) { screen.ExitLoopClosure()(); }
        return false;
    });

    screen.Loop(component);
    return result;
}

// ---------------------------------------------------------------------------
// screen_hunt helpers
// ---------------------------------------------------------------------------

static std::string passphrase_entry_screen() {
    auto screen = ScreenInteractive::Fullscreen();
    std::string passphrase;
    bool cancelled = false;

    auto input      = Input(&passphrase, "e.g.  eagle-has-landed");
    auto submit_btn = Button("  Authenticate  ", [&] {
        if (!passphrase.empty()) screen.ExitLoopClosure()();
    });
    auto cancel_btn = Button("  Cancel  ", [&] {
        cancelled = true;
        screen.ExitLoopClosure()();
    });
    auto btns = Container::Horizontal({submit_btn, cancel_btn});
    auto c    = Container::Vertical({input, btns});

    screen.Loop(Renderer(c, [&] {
        return vbox({
            header("ENTER PASSPHRASE"),
            text(""),
            text(" Ask them: \"What is your passphrase?\"") | color(Color::GrayDark),
            text(""),
            text(" Their passphrase:") | bold,
            input->Render() | border,
            text(""),
            hbox({submit_btn->Render(), text("   "), cancel_btn->Render()}) | hcenter,
        }) | border;
    }));
    return cancelled ? "" : passphrase;
}

static std::vector<Question> qa_screen(
    const std::string& target_name,
    const std::vector<std::string>& questions)
{
    auto screen = ScreenInteractive::Fullscreen();
    std::vector<std::string> answers(questions.size());
    bool cancelled = false;

    Components inputs;
    for (auto& ans : answers)
        inputs.push_back(Input(&ans, "Your answer..."));

    auto submit_btn = Button("  Submit answers  ", [&] { screen.ExitLoopClosure()(); });
    auto cancel_btn = Button("  Cancel  ", [&] {
        cancelled = true;
        screen.ExitLoopClosure()();
    });
    auto btns = Container::Horizontal({submit_btn, cancel_btn});
    auto all  = Container::Vertical(inputs);
    all->Add(btns);

    screen.Loop(Renderer(all, [&] {
        Elements rows = {
            header("YOU FOUND " + target_name + "!"),
            text(""),
            text(" Answer these questions together:") | bold,
            text(""),
        };
        for (size_t i = 0; i < questions.size(); ++i) {
            rows.push_back(text(" " + std::to_string(i + 1) + ". " + questions[i]) | bold);
            rows.push_back(inputs[i]->Render() | border);
            rows.push_back(text(""));
        }
        rows.push_back(
            hbox({submit_btn->Render(), text("   "), cancel_btn->Render()}) | hcenter);
        return vbox(rows) | border;
    }));

    if (cancelled) return {};
    std::vector<Question> result;
    for (size_t i = 0; i < questions.size(); ++i)
        result.push_back({questions[i], answers[i]});
    return result;
}

// ---------------------------------------------------------------------------
// screen_hunt
// ---------------------------------------------------------------------------

void screen_hunt(HttpClient& cli, const Student& student,
                 std::vector<Target> targets)
{
    auto stats0 = cli.get_stats(student.uuid);
    int completed = stats0 ? stats0->completed : 0;
    int total     = static_cast<int>(targets.size());

    while (completed < total) {
        if (targets.empty()) break;

        int selected = 0;
        std::vector<std::string> labels;
        for (const auto& t : targets)
            labels.push_back(t.forename + " " + t.surname
                             + "  (hint: " + t.passphrase_hint + "-...)");

        bool do_meet    = false;
        bool show_mypass = false;

        auto screen = ScreenInteractive::Fullscreen();
        AnnState ann;

        auto menu     = Menu(&labels, &selected);
        auto meet_btn = Button("  Meet this person  ", [&] {
            do_meet = true;
            screen.ExitLoopClosure()();
        });
        auto mypass_btn = Button("  Show my passphrase  ", [&] {
            show_mypass = true;
            screen.ExitLoopClosure()();
        });
        auto btns = Container::Horizontal({meet_btn, mypass_btn});
        auto c    = Container::Vertical({menu, btns});

        std::string time_label;
        std::mutex  tl_mtx;
        std::thread time_poller([&]() {
            while (true) {
                auto tinfo = cli.get_time();
                if (tinfo && tinfo->remaining >= 0) {
                    int s = static_cast<int>(tinfo->remaining);
                    std::string lbl = std::to_string(s / 60) + "m "
                                    + std::to_string(s % 60) + "s remaining";
                    std::lock_guard<std::mutex> lk(tl_mtx);
                    time_label = lbl;
                    screen.PostEvent(Event::Custom);
                }
                std::this_thread::sleep_for(10s);
            }
        });
        time_poller.detach();

        auto inner = Renderer(c, [&] {
            std::string tl;
            { std::lock_guard<std::mutex> lk(tl_mtx); tl = time_label; }
            return vbox({
                header("FIND YOUR TARGETS"),
                text(""),
                hbox({
                    text(" Progress: ") | bold,
                    text(std::to_string(completed) + " / "
                         + std::to_string(total) + " found") | color(Color::Green),
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
                hbox({meet_btn->Render(), text("   "), mypass_btn->Render()}) | hcenter,
                text(""),
                text(" Arrow keys to navigate  |  Enter to select button")
                    | color(Color::GrayDark) | hcenter,
            }) | border;
        });

        screen.Loop(with_announcements(inner, cli, ann, screen));

        if (show_mypass) {
            auto ps = ScreenInteractive::Fullscreen();
            auto ok = Button("  Back  ", [&] { ps.ExitLoopClosure()(); });
            ps.Loop(Renderer(ok, [&] {
                return vbox({
                    header("YOUR PASSPHRASE"),
                    text(""),
                    text("   " + student.passphrase) | bold | color(Color::Yellow) | hcenter,
                    text(""),
                    ok->Render() | hcenter,
                }) | border;
            }));
            continue;
        }

        if (!do_meet || selected >= static_cast<int>(targets.size())) continue;

        std::string passphrase = passphrase_entry_screen();
        if (passphrase.empty()) continue;

        std::string target_uuid, target_forename, error;
        auto questions = cli.meet(student.uuid, passphrase, target_uuid, target_forename, error);

        if (questions.empty()) {
            auto es = ScreenInteractive::Fullscreen();
            auto ok = Button("  Try again  ", [&] { es.ExitLoopClosure()(); });
            es.Loop(Renderer(ok, [&] {
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

        auto answered = qa_screen(target_forename, questions);
        if (answered.empty()) continue;

        int new_completed = 0, new_total = 0;
        if (cli.submit_answers(student.uuid, target_uuid, answered,
                               new_completed, new_total)) {
            completed = new_completed;
            targets.erase(std::remove_if(targets.begin(), targets.end(),
                [&](const Target& t) { return t.uuid == target_uuid; }),
                targets.end());

            auto ts = ScreenInteractive::Fullscreen();
            auto cont = Button("  Keep going!  ", [&] { ts.ExitLoopClosure()(); });
            ts.Loop(Renderer(cont, [&] {
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
}

// ---------------------------------------------------------------------------
// screen_stats
// ---------------------------------------------------------------------------

void screen_stats(HttpClient& cli, const Student& student) {
    auto screen = ScreenInteractive::Fullscreen();
    auto stats  = cli.get_stats(student.uuid);
    auto exit   = Button("  Exit  ", [&] { screen.ExitLoopClosure()(); });

    screen.Loop(Renderer(exit, [&] {
        Elements rows = {
            header("ALL DONE!"),
            text(""),
            text(" You found all your targets - great work!")
                | color(Color::Green) | bold | hcenter,
            text(""),
        };
        if (stats) {
            rows.push_back(text(" Meetings: "
                + std::to_string(stats->completed) + " / "
                + std::to_string(stats->total)) | hcenter);
            if (stats->place > 0) {
                rows.push_back(text(""));
                rows.push_back(
                    text(" Finish place: " + stats->ordinal + "!")
                    | bold | color(Color::Yellow) | hcenter);
            }
        }
        rows.push_back(text(""));
        rows.push_back(exit->Render() | hcenter);
        return vbox(rows) | border;
    }));
}

} // namespace mag::tui
