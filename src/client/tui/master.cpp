#include "tui.h"
#include "network.h"
#include "data.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace ftxui;
using json = nlohmann::json;

static const char* STATE_FILE = "master_state.json";

struct Phantom {
    std::string uuid, forename, surname, passphrase;
    uint64_t    student_id{0};
    bool        found{false};
};

static std::vector<Phantom> g_phantoms;

static void save_phantoms() {
    json arr = json::array();
    for (const auto& p : g_phantoms)
        arr.push_back({{"uuid", p.uuid}, {"forename", p.forename},
                       {"surname", p.surname}, {"student_id", p.student_id},
                       {"passphrase", p.passphrase}, {"found", p.found}});
    std::ofstream(STATE_FILE) << arr.dump(2);
}

static void load_phantoms() {
    std::ifstream f(STATE_FILE);
    if (!f) return;
    try {
        for (const auto& p : json::parse(f))
            g_phantoms.push_back({p.value("uuid",""), p.value("forename",""),
                                   p.value("surname",""),
                                   p.value("passphrase",""),
                                   p.value("student_id", uint64_t{0}),
                                   p.value("found", false)});
    } catch (...) {}
}

namespace mag::tui {

void screen_master(HttpClient& cli) {
    load_phantoms();

    // Use int for tab selector (avoids enum cast issues)
    int view = 0;  // 0=menu, 1=register, 2=list

    // Register form state
    std::string reg_forename, reg_surname, reg_id_str, reg_error;

    // List state
    int list_selected = 0;
    std::vector<std::string> phantom_labels;

    auto refresh_labels = [&] {
        phantom_labels.clear();
        for (const auto& p : g_phantoms)
            phantom_labels.push_back(
                (p.found ? "[FOUND] " : "        ")
                + p.forename + " " + p.surname
                + "   id=" + std::to_string(p.student_id)
                + "   pass=" + p.passphrase);
    };

    auto screen = ScreenInteractive::Fullscreen();

    // --- Register sub-components ---
    auto fn_input  = Input(&reg_forename, "First name");
    auto sn_input  = Input(&reg_surname,  "Last name");
    auto id_input  = Input(&reg_id_str,   "Student ID");
    auto reg_ok    = Button("  Register  ", [&] {
        reg_error.clear();
        if (reg_forename.empty() || reg_surname.empty() || reg_id_str.empty()) {
            reg_error = "All fields required."; return;
        }
        uint64_t sid = 0;
        try { sid = std::stoull(reg_id_str); }
        catch (...) { reg_error = "Invalid ID."; return; }
        auto r = cli.register_student(mag::encrypt_id(sid), reg_forename, reg_surname);
        if (!r) { reg_error = "Network error."; return; }
        g_phantoms.push_back({r->uuid, r->forename, r->surname, r->passphrase, sid, false});
        save_phantoms();
        reg_forename = reg_surname = reg_id_str = "";
        view = 0;
    });
    auto reg_back = Button("  Back  ", [&] { view = 0; });
    auto reg_form = Container::Vertical({
        fn_input, sn_input, id_input,
        Container::Horizontal({reg_ok, reg_back})
    });

    // --- List sub-components ---
    refresh_labels();
    auto phantom_menu = Menu(&phantom_labels, &list_selected);
    auto mark_btn     = Button("  Mark found  ", [&] {
        if (list_selected < static_cast<int>(g_phantoms.size())) {
            g_phantoms[list_selected].found = true;
            save_phantoms();
            refresh_labels();
        }
    });
    auto list_back = Button("  Back  ", [&] { view = 0; });
    auto list_view = Container::Vertical({
        phantom_menu,
        Container::Horizontal({mark_btn, list_back})
    });

    // --- Main menu ---
    std::vector<std::string> menu_items = {
        "Register phantom student",
        "List / manage phantoms",
        "Exit",
    };
    int menu_sel = 0;
    auto main_menu = Menu(&menu_items, &menu_sel);
    auto menu_go   = Button("  Select  ", [&] {
        if      (menu_sel == 0) { view = 1; }
        else if (menu_sel == 1) { refresh_labels(); list_selected = 0; view = 2; }
        else                    { screen.ExitLoopClosure()(); }
    });
    auto main_view = Container::Vertical({main_menu, menu_go});

    // --- Tab container ---
    auto root = Container::Tab({main_view, reg_form, list_view}, &view);

    screen.Loop(Renderer(root, [&] {
        Element body;
        if (view == 0) {
            body = vbox({
                text("  MASTER MODE  ") | bold | color(Color::Magenta) | hcenter,
                text(" Staff-only: register phantom students")
                    | color(Color::GrayDark) | hcenter,
                separator(),
                text(""),
                main_menu->Render() | border,
                text(""),
                menu_go->Render() | hcenter,
            });
        } else if (view == 1) {
            Elements rows = {
                text("  Register Phantom  ") | bold | color(Color::Magenta) | hcenter,
                separator(),
                text(" First name") | bold, fn_input->Render() | border,
                text(" Last name")  | bold, sn_input->Render() | border,
                text(" Student ID") | bold, id_input->Render() | border,
                text(""),
            };
            if (!reg_error.empty())
                rows.push_back(text(" " + reg_error) | color(Color::Red));
            rows.push_back(
                hbox({reg_ok->Render(), text("   "), reg_back->Render()}) | hcenter);
            body = vbox(rows);
        } else {
            body = vbox({
                text("  Phantom Students  ") | bold | color(Color::Magenta) | hcenter,
                separator(),
                phantom_menu->Render() | border,
                text(""),
                hbox({mark_btn->Render(), text("   "), list_back->Render()}) | hcenter,
            });
        }
        return body | border;
    }));
}

} // namespace mag::tui
