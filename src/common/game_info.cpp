#include "app/app.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/input.h"

namespace {

// XBE_PAD_SCRIPT=40:X,52:A,60:START presses that button on port 0 for 0.2 s at each of those seconds after
// launch, so a test run can reach a menu without a controller (local test aid)
void ScriptedPad(int port, input::Pad& pad) {
    struct Press {
        double at;
        uint16_t buttons;
        int analog;
    };
    static const std::vector<Press> script = [] {
        std::vector<Press> v;
        const char* e = std::getenv("XBE_PAD_SCRIPT");
        for (const char* p = e; p && *p;) {
            char* end;
            double t = std::strtod(p, &end);
            if (end == p || *end != ':') break;
            const char* name = end + 1;
            size_t n = std::strcspn(name, ",");
            auto is = [&](const char* s) { return std::strlen(s) == n && !std::strncmp(name, s, n); };
            Press press{t, 0, -1};
            if (is("A")) press.analog = 0;
            if (is("B")) press.analog = 1;
            if (is("X")) press.analog = 2;
            if (is("Y")) press.analog = 3;
            if (is("UP")) press.buttons = 0x1;
            if (is("DOWN")) press.buttons = 0x2;
            if (is("LEFT")) press.buttons = 0x4;
            if (is("RIGHT")) press.buttons = 0x8;
            if (is("START")) press.buttons = 0x10;
            if (is("BACK")) press.buttons = 0x20;
            v.push_back(press);
            p = name[n] ? name + n + 1 : name + n;
        }
        return v;
    }();
    static const auto start = std::chrono::steady_clock::now();
    if (port != 0 || script.empty()) return;
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    pad.connected = true;
    for (const Press& press : script) {
        if (now < press.at || now >= press.at + 0.2) continue;
        pad.buttons |= press.buttons;
        if (press.analog >= 0) pad.analog[press.analog] = 0xFF;
    }
}

}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"MVP 06 NCAA Baseball",               // window and splash
    .short_title = L"MVP 06",               // message boxes
    .name = L"MVP06",                      // window classes, crash dumps, handover event
    .log = "mvp06",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MVP06"}},  // each recompiled xbe and its exe, for title launches
    .options = {},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = ScriptedPad,                    // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
