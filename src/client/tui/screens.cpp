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
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace ftxui;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Element header(const std::string& title) {
    return vbox({
        text("  MEET AND GREET  ") | bold | color(Color::Cyan) | hcenter,
        text(" " + title + " ")   | bold | color(Color::Yellow) | hcenter,
        separator(),
    });
}

// Stoppable sleep: wakes early when stop() is called.
struct StopToken {
    std::mutex              mtx;
    std::condition_variable cv;
    bool                    stopped{false};

    void stop() {
        std::lock_guard<std::mutex> lk(mtx);
        stopped = true;
        cv.notify_all();
    }
    // Returns true if stopped early, false if timeout elapsed normally.
    bool sleep(std::chrono::milliseconds d) {
        std::unique_lock<std::mutex> lk(mtx);
        return cv.wait_for(lk, d, [this] { return stopped; });
    }
    bool is_stopped() {
        std::lock_guard<std::mutex> lk(mtx);
        return stopped;
    }
};

// ---------------------------------------------------------------------------
// Navigation router - single screen.Loop() for the whole session.
//
// RouterBase wraps a Component that can be swapped at runtime.
// replace() is thread-safe; the swap is applied at the next Render/OnEvent.
// In FTXUI v6 override OnRender(), not Render().
// ---------------------------------------------------------------------------

using NavFn = std::function<void(Component)>;

class RouterBase : public ComponentBase {
public:
    void replace(Component c) {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_ = std::move(c);
    }
    Element OnRender() override {
        apply();
        return inner_ ? inner_->Render() : text("") | border;
    }
    bool OnEvent(Event e) override {
        apply();
        return inner_ ? inner_->OnEvent(e) : false;
    }
    bool Focusable() const override {
        return inner_ ? inner_->Focusable() : false;
    }
private:
    void apply() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pending_) { inner_ = std::move(pending_); }
    }
    std::mutex mtx_;
    Component  inner_;
    Component  pending_;
};

// ---------------------------------------------------------------------------
// Announcement overlay
//
// AnnState must live at least as long as both the component tree that
// references it AND the background thread.  Pass a shared_ptr everywhere.
// ---------------------------------------------------------------------------

struct AnnState {
    std::mutex  mtx;
    std::string message;
    double      last_since{0.0};
    bool        show_modal{false};
    bool        stop{false};
};

// Wraps inner with an announcement modal; returns the modal-wrapped component.
// The caller is responsible for starting (and detaching) the poll thread via
// start_ann_thread().
static Component make_ann_modal(Component inner,
                                std::shared_ptr<AnnState> ann)
{
    auto dismiss_btn = Button("  Dismiss  ", [ann] {
        std::lock_guard<std::mutex> lk(ann->mtx);
        ann->show_modal = false;
    });
    auto modal_body = Renderer(dismiss_btn, [ann, dismiss_btn] {
        std::string msg;
        { std::lock_guard<std::mutex> lk(ann->mtx); msg = ann->message; }
        return vbox({
            text("  *** ANNOUNCEMENT ***  ") | bold | color(Color::Red) | hcenter,
            separator(),
            text(""),
            paragraph(msg) | hcenter,
            text(""),
            dismiss_btn->Render() | hcenter,
        }) | border | color(Color::Red);
    });
    return Modal(inner, modal_body, &ann->show_modal);
}

// Detaches a background thread that polls for announcements.
// ann is kept alive by the thread via the shared_ptr capture.
static void start_ann_thread(mag::HttpClient& cli,
                              std::shared_ptr<AnnState> ann,
                              ScreenInteractive& screen)
{
    std::thread([&cli, ann, &screen] {
        while (true) {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(500ms);
                { std::lock_guard<std::mutex> lk(ann->mtx); if (ann->stop) return; }
            }
            auto anns = cli.get_announcements(ann->last_since);
            for (const auto& a : anns) {
                std::lock_guard<std::mutex> lk(ann->mtx);
                if (ann->stop) return;
                ann->message    = a.message;
                ann->last_since = a.sent_at;
                ann->show_modal = true;
                screen.PostEvent(Event::Custom);
            }
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// Hunt shared context - persists across all hunt sub-screens.
// ---------------------------------------------------------------------------

struct HuntCtx {
    mag::HttpClient&          cli;
    const mag::Student&       student;
    std::set<std::string>     known_met;
    int                       completed{0};
    int                       total{0};
    std::vector<mag::Target>  targets;

    // Pending ice-breaker: set by the stats poller when someone enters our
    // passphrase (server-side pending_meet).
    struct Pib {
        std::mutex               mtx;
        bool                     active{false};
        std::string              finder_uuid;
        std::string              finder_forename;
        std::vector<std::string> questions;
    } pib;

    HuntCtx(mag::HttpClient& c, const mag::Student& s) : cli(c), student(s) {}
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void nav_hunt_menu(ScreenInteractive&, NavFn, std::shared_ptr<HuntCtx>,
                           std::function<void()> on_done);

// ---------------------------------------------------------------------------
// Hunt sub-screens
// ---------------------------------------------------------------------------

static void nav_meeting_recorded(
    ScreenInteractive& screen, NavFn nav,
    std::shared_ptr<HuntCtx> ctx,
    const std::string& target_forename,
    std::function<void()> on_done)
{
    int comp = ctx->completed;
    int tot  = ctx->total;
    auto cont = Button("  Keep going!  ", [&screen, nav, ctx, on_done] {
        if (ctx->completed >= ctx->total || ctx->targets.empty())
            on_done();
        else
            nav_hunt_menu(screen, nav, ctx, on_done);
    });
    nav(Renderer(cont, [cont, target_forename, comp, tot] {
        return vbox({
            header("MEETING RECORDED"),
            text(""),
            text(" You met " + target_forename + "!")
                | color(Color::Green) | bold | hcenter,
            text(""),
            text(" Progress: " + std::to_string(comp)
                 + " / " + std::to_string(tot)) | hcenter,
            text(""),
            cont->Render() | hcenter,
        }) | border;
    }));
}

static void nav_icebreaker(
    ScreenInteractive& screen, NavFn nav,
    std::shared_ptr<HuntCtx> ctx,
    const std::string& target_forename,
    const std::string& target_uuid,
    const std::vector<std::string>& questions,
    std::function<void()> on_done)
{
    auto ok = Button("  We met! Record it  ",
        [&screen, nav, ctx, on_done, target_forename, target_uuid] {
            int nc = 0, nt = 0;
            bool ok = ctx->cli.submit_answers(
                ctx->student.uuid, target_uuid, {}, nc, nt);
            if (!ok) {
                if (auto s = ctx->cli.get_stats(ctx->student.uuid)) {
                    nc = s->completed;
                    for (const auto& uid : s->met_uuids)
                        ctx->known_met.insert(uid);
                }
            }
            ctx->completed = nc;
            ctx->known_met.insert(target_uuid);
            ctx->targets.erase(
                std::remove_if(ctx->targets.begin(), ctx->targets.end(),
                    [&](const mag::Target& t) { return t.uuid == target_uuid; }),
                ctx->targets.end());
            nav_meeting_recorded(screen, nav, ctx, target_forename, on_done);
        });
    auto cancel = Button("  Cancel  ", [nav, ctx, on_done, &screen] {
        nav_hunt_menu(screen, nav, ctx, on_done);
    });
    auto btns = Container::Horizontal({ok, cancel});

    nav(Renderer(btns, [btns, target_forename, questions, ok, cancel] {
        Elements rows = {
            header("YOU FOUND " + target_forename + "!"),
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
}

static void nav_invalid_passphrase(
    ScreenInteractive& screen, NavFn nav,
    std::shared_ptr<HuntCtx> ctx,
    const std::string& reason,
    std::function<void()> on_done)
{
    auto retry = Button("  Try again  ", [nav, ctx, on_done, &screen] {
        nav_hunt_menu(screen, nav, ctx, on_done);
    });
    nav(Renderer(retry, [retry, reason] {
        return vbox({
            header("PASSPHRASE NOT RECOGNISED"),
            text(""),
            hbox({
                text("  "),
                text("X") | bold | color(Color::Red),
                text("  " + reason) | color(Color::Red),
            }) | hcenter,
            text(""),
            text(" Tips:") | bold | color(Color::Yellow),
            text("  - Ask them to say it slowly, one word at a time.")
                | color(Color::GrayDark),
            text("  - Passphrases are three words joined by dashes.")
                | color(Color::GrayDark),
            text("  - Check for typos (e.g. 0 vs O, 1 vs l).")
                | color(Color::GrayDark),
            text(""),
            retry->Render() | hcenter,
            text(""),
        }) | border;
    }));
}

static void nav_passphrase_entry(
    ScreenInteractive& screen, NavFn nav,
    std::shared_ptr<HuntCtx> ctx,
    std::function<void()> on_done)
{
    auto passphrase = std::make_shared<std::string>();

    auto input = Input(passphrase.get(), "e.g.  eagle-has-landed");
    auto submit_btn = Button("  Authenticate  ",
        [&screen, nav, ctx, on_done, passphrase] {
            if (passphrase->empty()) return;
            std::string target_uuid, target_forename, error;
            auto questions = ctx->cli.meet(
                ctx->student.uuid, *passphrase,
                target_uuid, target_forename, error);
            if (questions.empty()) {
                nav_invalid_passphrase(screen, nav, ctx, error, on_done);
                return;
            }
            nav_icebreaker(screen, nav, ctx,
                           target_forename, target_uuid, questions, on_done);
        });
    auto cancel_btn = Button("  Cancel  ", [nav, ctx, on_done, &screen] {
        nav_hunt_menu(screen, nav, ctx, on_done);
    });
    auto btns = Container::Horizontal({submit_btn, cancel_btn});
    auto c    = Container::Vertical({input, btns});

    nav(Renderer(c, [c, input, submit_btn, cancel_btn] {
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
}

// ---------------------------------------------------------------------------
// Hunt menu
// ---------------------------------------------------------------------------

static void nav_hunt_menu(
    ScreenInteractive& screen, NavFn nav,
    std::shared_ptr<HuntCtx> ctx,
    std::function<void()> on_done)
{
    if (ctx->completed >= ctx->total || ctx->targets.empty()) {
        on_done();
        return;
    }

    // All mutable per-iteration state on the heap so background threads
    // can safely hold references after nav_hunt_menu returns.
    struct IterState {
        std::vector<std::string>  labels;
        int                       sel{0};   // heap-allocated; captured by components
        StopToken                 stop;
        std::atomic<bool>         user_action{false};
        std::shared_ptr<AnnState> ann{std::make_shared<AnnState>()};
    };
    auto ist = std::make_shared<IterState>();

    for (const auto& t : ctx->targets)
        ist->labels.push_back(t.forename + " " + t.surname
                              + "  (hint: " + t.passphrase_hint + "-...)");

    // Symmetric meeting notification (visible in the menu UI).
    struct SymNotif {
        std::mutex              mtx;
        std::vector<std::string> names;
        bool                    show{false};
    };
    auto sym = std::make_shared<SymNotif>();

    // Stats + pending_meet poller (detached; lives via shared_ptr captures).
    std::thread([ist, sym, ctx, &screen] {
        while (!ist->stop.sleep(5000ms)) {
            auto stats = ctx->cli.get_stats(ctx->student.uuid);
            if (stats) {
                std::vector<std::string> new_names;
                for (const auto& uid : stats->met_uuids) {
                    if (ctx->known_met.count(uid)) continue;
                    auto it = std::find_if(ctx->targets.begin(), ctx->targets.end(),
                        [&](const mag::Target& t) { return t.uuid == uid; });
                    if (it == ctx->targets.end()) continue;
                    ctx->known_met.insert(uid);
                    new_names.push_back(it->forename + " " + it->surname);
                }
                if (!new_names.empty()) {
                    std::lock_guard<std::mutex> lk(sym->mtx);
                    for (auto& n : new_names) sym->names.push_back(std::move(n));
                    sym->show = true;
                    ctx->completed = stats->completed;
                    screen.PostEvent(Event::Custom);
                }
            }
            auto pm = ctx->cli.get_pending_meet(ctx->student.uuid);
            if (pm) {
                auto it = std::find_if(ctx->targets.begin(), ctx->targets.end(),
                    [&](const mag::Target& t) { return t.uuid == pm->finder_uuid; });
                if (it != ctx->targets.end()
                    && !ctx->known_met.count(pm->finder_uuid)) {
                    std::lock_guard<std::mutex> lk(ctx->pib.mtx);
                    if (!ctx->pib.active) {
                        ctx->pib.active         = true;
                        ctx->pib.finder_uuid     = pm->finder_uuid;
                        ctx->pib.finder_forename = pm->finder_forename;
                        ctx->pib.questions       = pm->questions;
                        screen.PostEvent(Event::Custom);
                    }
                }
            }
        }
    }).detach();

    auto menu = Menu(&ist->labels, &ist->sel);
    auto meet_btn = Button("  Meet this person  ",
        [&screen, nav, ctx, on_done, ist] {
            if (ist->sel >= static_cast<int>(ctx->targets.size())) return;
            if (ist->user_action.exchange(true)) return;
            ist->stop.stop();
            { std::lock_guard<std::mutex> lk(ist->ann->mtx); ist->ann->stop = true; }
            nav_passphrase_entry(screen, nav, ctx, on_done);
        });
    auto c = Container::Vertical({menu, meet_btn});

    auto sym_dismiss = Button("  Got it  ", [sym] {
        std::lock_guard<std::mutex> lk(sym->mtx);
        sym->names.clear();
        sym->show = false;
    });
    auto sym_modal = Renderer(sym_dismiss, [sym, sym_dismiss] {
        std::vector<std::string> names;
        { std::lock_guard<std::mutex> lk(sym->mtx); names = sym->names; }
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

    auto inner = Renderer(c, [ctx, ist, menu, meet_btn] {
        return vbox({
            header("FIND YOUR TARGETS"),
            text(""),
            hbox({
                text(" Progress: ") | bold,
                text(std::to_string(ctx->completed) + " / "
                     + std::to_string(ctx->total) + " found")
                    | color(Color::Green),
                filler(),
            }),
            separator(),
            text(""),
            text(" Your targets (arrow keys to navigate):") | bold,
            text(""),
            menu->Render() | border,
            text(""),
            text(" Your passphrase: ") | color(Color::GrayDark),
            text("   " + ctx->student.passphrase) | bold | color(Color::Yellow),
            text(""),
            meet_btn->Render() | hcenter,
            text(""),
            text(" Arrow keys to navigate  |  Enter to select button")
                | color(Color::GrayDark) | hcenter,
        }) | border;
    });

    auto with_ann = make_ann_modal(inner, ist->ann);
    start_ann_thread(ctx->cli, ist->ann, screen);
    auto with_sym = Modal(with_ann, sym_modal, &sym->show);

    auto root = CatchEvent(with_sym,
        [&screen, nav, ctx, on_done, ist, sym](Event) -> bool {
            // Sync symmetric-met targets out of the list.
            {
                std::lock_guard<std::mutex> lk(sym->mtx);
                if (!sym->names.empty()) {
                    ctx->targets.erase(
                        std::remove_if(ctx->targets.begin(), ctx->targets.end(),
                            [&](const mag::Target& t) {
                                return ctx->known_met.count(t.uuid);
                            }),
                        ctx->targets.end());
                    ist->labels.clear();
                    for (const auto& t : ctx->targets)
                        ist->labels.push_back(
                            t.forename + " " + t.surname
                            + "  (hint: " + t.passphrase_hint + "-...)");
                    if (ist->sel >= static_cast<int>(ist->labels.size()))
                        ist->sel = std::max(0, static_cast<int>(ist->labels.size()) - 1);
                }
            }
            // Pending ice-breaker: partner entered our passphrase.
            {
                std::lock_guard<std::mutex> lk(ctx->pib.mtx);
                if (ctx->pib.active && !ist->user_action.exchange(true)) {
                    ctx->pib.active = false; // reset so next hunt_menu doesn't re-fire
                    ist->stop.stop();
                    { std::lock_guard<std::mutex> lk2(ist->ann->mtx);
                      ist->ann->stop = true; }
                    std::string fn  = ctx->pib.finder_forename;
                    std::string uid = ctx->pib.finder_uuid;
                    auto qs = ctx->pib.questions;
                    nav_icebreaker(screen, nav, ctx, fn, uid, qs, on_done);
                    return true;
                }
            }
            // All done.
            if (!ist->user_action.load()
                && (ctx->completed >= ctx->total || ctx->targets.empty())) {
                if (!ist->user_action.exchange(true)) {
                    ist->stop.stop();
                    { std::lock_guard<std::mutex> lk2(ist->ann->mtx);
                      ist->ann->stop = true; }
                    on_done();
                    return true;
                }
            }
            return false;
        });

    nav(root);
    // nav_hunt_menu returns here.  All threads are detached and hold shared_ptrs
    // to their state, so no dangling references even though the stack frame is gone.
}

// ---------------------------------------------------------------------------
// Wait screen
// ---------------------------------------------------------------------------

static void nav_wait(
    ScreenInteractive& screen, NavFn nav,
    mag::HttpClient& cli, const mag::Student& student,
    std::vector<mag::Target>& out_targets,
    std::function<void()> on_done)
{
    struct WaitState {
        std::atomic<bool>         found{false};
        std::atomic<bool>         stop{false};
        std::atomic<bool>         navigated{false};
        std::vector<mag::Target>  result;
        std::string               time_label{"Waiting for session to start..."};
        std::mutex                tl_mtx;
        std::atomic<int>          ticks{0};
        StopToken                 ticker_stop;
        std::shared_ptr<AnnState> ann{std::make_shared<AnnState>()};
    };
    auto ws = std::make_shared<WaitState>();

    std::thread([ws, &cli, &student, &screen] {
        while (!ws->stop.load()) {
            auto targets = cli.get_targets(student.uuid);
            if (!targets.empty() && !ws->found.load()) {
                ws->result = targets;
                ws->found  = true;
                screen.PostEvent(Event::Custom);
                return;
            }
            auto tinfo = cli.get_time();
            if (tinfo && tinfo->remaining >= 0) {
                int s = static_cast<int>(tinfo->remaining);
                std::lock_guard<std::mutex> lk(ws->tl_mtx);
                ws->time_label = std::to_string(s / 60) + "m "
                               + std::to_string(s % 60) + "s remaining";
            }
            screen.PostEvent(Event::Custom);
            for (int i = 0; i < 10 && !ws->stop.load(); ++i)
                std::this_thread::sleep_for(500ms);
        }
    }).detach();

    std::thread([ws, &screen] {
        while (!ws->ticker_stop.sleep(250ms)) {
            ++ws->ticks;
            screen.PostEvent(Event::Custom);
        }
    }).detach();

    const std::string spin = "|/-\\";
    auto inner = Renderer([ws, &student, spin] {
        std::string tl;
        { std::lock_guard<std::mutex> lk(ws->tl_mtx); tl = ws->time_label; }
        return vbox({
            header("WAITING FOR TARGETS"),
            text(""),
            hbox({
                text("  " + std::string(1, spin[ws->ticks.load() % 4]) + "  ")
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

    auto with_ann = make_ann_modal(inner, ws->ann);
    start_ann_thread(cli, ws->ann, screen);

    auto root = CatchEvent(with_ann,
        [ws, &out_targets, nav, on_done, &screen](Event) -> bool {
            if (ws->found.load() && !ws->navigated.exchange(true)) {
                ws->stop = true;
                ws->ticker_stop.stop();
                { std::lock_guard<std::mutex> lk(ws->ann->mtx);
                  ws->ann->stop = true; }
                out_targets = ws->result;
                on_done();
                return true;
            }
            return false;
        });

    nav(root);
}

// ---------------------------------------------------------------------------
// Register screens
// ---------------------------------------------------------------------------

static void nav_passphrase_display(
    ScreenInteractive& screen, NavFn nav,
    mag::Student& student,
    std::function<void()> on_done)
{
    auto cont = Button("  I have it - Continue  ", [on_done] { on_done(); });
    // Capture student by reference; student lives in run_tui (optional<Student>).
    nav(Renderer(cont, [cont, &student] {
        return vbox({
            header("YOUR SECRET PASSPHRASE"),
            text(""),
            text(" Welcome, " + student.forename + " " + student.surname + "!")
                | color(Color::Green) | bold,
            text(""),
            text(" Your passphrase is:") | bold,
            text(""),
            text("   >>> " + student.passphrase + " <<<")
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

static void nav_name_mismatch(
    ScreenInteractive& screen, NavFn nav,
    mag::HttpClient& cli,
    mag::Student& result,
    const std::string& entered_fn,
    const std::string& entered_sn,
    std::function<void()> on_done)
{
    std::string stored_fn = result.forename;
    std::string stored_sn = result.surname;
    auto yes = Button("  Yes, update  ",
        [nav, &cli, &result, entered_fn, entered_sn, on_done, &screen] {
            cli.update_name(result.uuid, entered_fn, entered_sn);
            result.forename = entered_fn;
            result.surname  = entered_sn;
            nav_passphrase_display(screen, nav, result, on_done);
        });
    auto no = Button("  Keep stored name  ",
        [nav, &result, on_done, &screen] {
            nav_passphrase_display(screen, nav, result, on_done);
        });
    auto btns = Container::Horizontal({yes, no});
    nav(Renderer(btns, [btns, yes, no, stored_fn, stored_sn, entered_fn, entered_sn] {
        return vbox({
            header("WELCOME BACK"),
            text(""),
            text(" Stored name:  " + stored_fn + " " + stored_sn),
            text(" Entered name: " + entered_fn + " " + entered_sn),
            text(""),
            text(" Update to the newly entered name?") | bold,
            text(""),
            hbox({yes->Render(), text("   "), no->Render()}) | hcenter,
        }) | border;
    }));
}

static void nav_register(
    ScreenInteractive& screen, NavFn nav,
    mag::HttpClient& cli,
    std::optional<mag::Student>& out_student,
    std::function<void()> on_done)
{
    struct RegState {
        std::string id_str, forename, surname, error_msg;
        bool submitting{false};
        std::optional<mag::Student> result;
        std::atomic<bool> navigated{false}; // on heap so CatchEvent lambda is safe
    };
    auto rs = std::make_shared<RegState>();

    auto id_input = Input(&rs->id_str,   "e.g.  10012345");
    auto fn_input = Input(&rs->forename, "e.g.  Jane");
    auto sn_input = Input(&rs->surname,  "e.g.  Smith");

    auto do_submit = [rs, &cli, &screen, nav, &out_student, on_done] {
        rs->error_msg.clear();
        if (rs->id_str.empty() || rs->forename.empty() || rs->surname.empty()) {
            rs->error_msg = "All fields are required."; return;
        }
        uint64_t sid = 0;
        try { sid = std::stoull(rs->id_str); }
        catch (...) { rs->error_msg = "Student ID must be a number."; return; }
        rs->submitting = true;
        std::thread([rs, sid, &cli, &screen] {
            auto r = cli.register_student(mag::encrypt_id(sid),
                                          rs->forename, rs->surname);
            rs->submitting = false;
            if (!r)
                rs->error_msg = "Network error - is the server running?";
            else
                rs->result = r;
            screen.PostEvent(Event::Custom);
        }).detach();
    };

    auto submit_btn = Button("  Register  ", do_submit);
    auto inputs = Container::Vertical({id_input, fn_input, sn_input, submit_btn});

    auto form = CatchEvent(
        Renderer(inputs, [rs, id_input, fn_input, sn_input, submit_btn] {
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
            if (!rs->error_msg.empty())
                rows.push_back(text(" " + rs->error_msg) | color(Color::Red));
            if (rs->submitting)
                rows.push_back(text(" Connecting...") | color(Color::Yellow));
            rows.push_back(text(""));
            rows.push_back(submit_btn->Render() | hcenter);
            rows.push_back(text(""));
            rows.push_back(
                text(" Tab between fields  |  Enter to submit")
                    | color(Color::GrayDark) | hcenter);
            return vbox(rows) | border;
        }),
        // CatchEvent captures rs (shared_ptr) - no dangling refs.
        [rs, nav, &cli, &out_student, on_done, &screen](Event) -> bool {
            if (rs->result && !rs->navigated.exchange(true)) {
                out_student = rs->result;
                bool differs = (rs->result->forename != rs->forename
                             || rs->result->surname  != rs->surname);
                if (!rs->result->is_new && differs) {
                    nav_name_mismatch(screen, nav, cli, *out_student,
                                      rs->forename, rs->surname, on_done);
                } else {
                    nav_passphrase_display(screen, nav, *out_student, on_done);
                }
                return true;
            }
            return false;
        });

    nav(form);
}

// ---------------------------------------------------------------------------
// Stats screen
// ---------------------------------------------------------------------------

static void nav_stats(
    ScreenInteractive& screen, NavFn nav,
    mag::HttpClient& cli, const mag::Student& student,
    std::function<void()> on_exit)
{
    auto stats = cli.get_stats(student.uuid);
    auto exit_btn = Button("  Exit  ", on_exit);
    nav(Renderer(exit_btn, [exit_btn, &student, stats] {
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
        rows.push_back(exit_btn->Render() | hcenter);
        return vbox(rows) | border;
    }));
}

// ---------------------------------------------------------------------------
// Discover screen
// ---------------------------------------------------------------------------

static void nav_discover(
    ScreenInteractive& screen, NavFn nav, int udp_port,
    std::function<void(mag::ServerInfo)> on_done)
{
    struct DiscState {
        std::atomic<bool> found{false};
        std::atomic<bool> done_called{false}; // on heap so CatchEvent lambda is safe
        StopToken         stop;
        std::atomic<int>  ticks{0};
        mag::ServerInfo   result;
    };
    auto ds = std::make_shared<DiscState>();

    std::thread([ds, &screen, udp_port] {
        while (!ds->stop.is_stopped()) {
            auto info = mag::discover_server(udp_port, 1);
            if (info && !ds->found.load()) {
                ds->result = *info;
                ds->found  = true;
                screen.PostEvent(Event::Custom);
                return;
            }
        }
    }).detach();

    std::thread([ds, &screen] {
        while (!ds->stop.sleep(200ms)) {
            ++ds->ticks;
            screen.PostEvent(Event::Custom);
        }
    }).detach();

    const std::string spin = "|/-\\";
    auto renderer = Renderer([ds, spin] {
        return vbox({
            header("SERVER DISCOVERY"),
            text(""),
            hbox({
                text("  " + std::string(1, spin[ds->ticks.load() % 4]) + "  ")
                    | color(Color::Yellow),
                text("Searching for MAG server on the LAN..."),
            }) | hcenter,
            text(""),
            text("(Make sure the server is running and you are on the same Wi-Fi)")
                | color(Color::GrayDark) | hcenter,
        }) | border;
    });

    nav(CatchEvent(renderer,
        [ds, on_done](Event) -> bool {
            if (ds->found.load() && !ds->done_called.exchange(true)) {
                ds->stop.stop();
                on_done(ds->result);
                return true;
            }
            return false;
        }));
}

// ---------------------------------------------------------------------------
// Hunt entry
// ---------------------------------------------------------------------------

static void nav_hunt(
    ScreenInteractive& screen, NavFn nav,
    mag::HttpClient& cli, const mag::Student& student,
    std::vector<mag::Target>& targets,
    std::function<void()> on_done)
{
    auto ctx = std::make_shared<HuntCtx>(cli, student);
    ctx->total   = static_cast<int>(targets.size());
    ctx->targets = targets;
    if (auto s = cli.get_stats(student.uuid)) {
        ctx->completed = s->completed;
        ctx->known_met = {s->met_uuids.begin(), s->met_uuids.end()};
        ctx->targets.erase(
            std::remove_if(ctx->targets.begin(), ctx->targets.end(),
                [&](const mag::Target& t) { return ctx->known_met.count(t.uuid); }),
            ctx->targets.end());
    }
    if (ctx->completed >= ctx->total || ctx->targets.empty()) {
        on_done();
        return;
    }
    nav_hunt_menu(screen, nav, ctx, on_done);
}

// ---------------------------------------------------------------------------
// run_tui - single entry point; one screen.Loop() for the whole session.
// ---------------------------------------------------------------------------

namespace mag::tui {

void run_tui(ScreenInteractive& screen, int udp_port,
             const std::string& server_addr) {
    LOG("run_tui: start");

    auto rtr = std::make_shared<RouterBase>();
    NavFn nav = [rtr, &screen](Component c) {
        rtr->replace(std::move(c));
        screen.PostEvent(Event::Custom);
    };

    // Session-level state.  Lives for the entire run_tui call (i.e., the whole
    // session).  All nav_* functions capture these by reference safely.
    std::optional<mag::HttpClient> cli;
    std::optional<mag::Student>    student;
    std::vector<mag::Target>       targets;

    auto do_exit = [&screen] { screen.ExitLoopClosure()(); };

    std::function<void()> do_stats, do_hunt, do_wait;

    do_stats = [&] {
        LOG("phase", "stats");
        nav_stats(screen, nav, *cli, *student, do_exit);
    };
    do_hunt = [&] {
        LOG("phase", "hunt");
        nav_hunt(screen, nav, *cli, *student, targets, do_stats);
    };
    do_wait = [&] {
        LOG("phase", "wait");
        nav_wait(screen, nav, *cli, *student, targets, [&] {
            LOG("targets_count", static_cast<int>(targets.size()));
            if (targets.empty()) { do_exit(); return; }
            do_hunt();
        });
    };

    auto do_register = [&](mag::ServerInfo server) {
        LOG("server_ip",   server.ip);
        LOG("server_port", server.port);
        cli = mag::HttpClient{server.ip, server.port};
        LOG("phase", "register");
        nav_register(screen, nav, *cli, student, do_wait);
    };

    if (!server_addr.empty()) {
        // --server supplied: skip UDP discovery, parse ip:port directly.
        auto colon = server_addr.rfind(':');
        mag::ServerInfo si;
        si.ip   = server_addr.substr(0, colon);
        si.port = std::stoi(server_addr.substr(colon + 1));
        LOG("phase", "register_direct");
        do_register(si);
    } else {
        LOG("phase", "discover");
        nav_discover(screen, nav, udp_port, do_register);
    }

    // Single blocking event loop.  Screen transitions replace the router's
    // inner component; this loop never re-enters raw terminal mode.
    screen.Loop(rtr);

    LOG("run_tui: done");
}

} // namespace mag::tui
