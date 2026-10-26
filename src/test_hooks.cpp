#ifdef CV_TEST_HOOKS

#include "test_hooks.h"

#include <M5Cardputer.h>

#include <string>

#include "core/flows.h"

namespace cv {
namespace test {

namespace {

std::string g_line;
bool g_g0 = false;
// Id of the request being answered ("-" when the line had none). Every reply
// carries it, so the host can tell a late reply from the one it waits for.
std::string g_id = "-";
// `sync` is answered once the key queue is empty and the main loop has gone
// round twice since, i.e. every earlier key was handled and navigated.
std::string g_sync_id;
int g_sync_wait = 0;

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            default:
                if (uint8_t(c) < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

void reply(const std::string& s) {
    Serial.print("OK ");
    Serial.print(g_id.c_str());
    Serial.print(' ');
    Serial.println(s.c_str());
}
void error(const char* s) {
    Serial.print("ERR ");
    Serial.print(g_id.c_str());
    Serial.print(' ');
    Serial.println(s);
}

bool key_named(const std::string& n, ui::Key& k) {
    using ui::KeyCode;
    k = ui::Key();
    if (n == "enter") k.code = KeyCode::Enter;
    else if (n == "tab") k.code = KeyCode::Tab;
    else if (n == "esc") k.code = KeyCode::Esc;
    else if (n == "bs") k.code = KeyCode::Backspace;
    else if (n == "up") k.code = KeyCode::Up;
    else if (n == "down") k.code = KeyCode::Down;
    else if (n == "left") k.code = KeyCode::Left;
    else if (n == "right") k.code = KeyCode::Right;
    else if (n == "opt") k.code = KeyCode::Opt;
    else if (n == "space") k.ch = ' ';
    else if (n == "backtick") k.ch = '`';
    else if (n.size() == 1) k.ch = n[0];
    else return false;
    return true;
}

void state(ui::App& a) {
    ui::Screen* s = a.router.top();
    std::string info;
    if (s) s->debug(info);
    std::string j = "{";
    j += "\"screen\":\"" + std::string(s ? s->name() : "") + "\"";
    j += ",\"title\":\"" + esc(s ? s->title() : "") + "\"";
    j += ",\"depth\":" + std::to_string(a.router.depth());
    j += ",\"open\":" + std::string(a.session.is_open() ? "true" : "false");
    j += ",\"entries\":" + std::to_string(a.session.data().entries.size());
    j += ",\"fails\":" + std::to_string(a.guard.failures());
    j += ",\"toast\":\"" + esc(a.toast_active() ? a.toast_msg.c_str() : "") + "\"";
    j += ",\"dimmed\":" + std::string(a.dimmed ? "true" : "false");
    j += ",\"bright\":" + std::to_string(a.settings.brightness);
    j += ",\"time\":" + std::to_string(device::unix_time());
    j += ",\"info\":\"" + esc(info) + "\"}";
    reply(j);
}

void dump(ui::App& a) {
    std::string j = "[";
    bool first = true;
    for (auto& e : a.session.data().entries) {
        if (!first) j += ",";
        first = false;
        j += "{\"id\":" + std::to_string(e.id) + ",\"type\":" + std::to_string(int(e.type)) +
             ",\"title\":\"" + esc(e.title.c_str()) + "\",\"fields\":{";
        bool ff = true;
        for (auto& f : e.fields) {
            if (!ff) j += ",";
            ff = false;
            j += "\"" + std::to_string(f.key) + "\":\"" + esc(f.value.c_str()) + "\"";
        }
        j += "}}";
    }
    reply(j + "]");
}

// "<id> <command> [argument]"; the id is a decimal number chosen by the host.
void handle(ui::App& a, const std::string& request) {
    std::string line;
    size_t idlen = request.find(' ');
    if (idlen == std::string::npos) idlen = request.size();
    bool numeric = idlen > 0 && idlen <= 18;
    for (size_t i = 0; i < idlen && numeric; i++)
        if (request[i] < '0' || request[i] > '9') numeric = false;
    if (!numeric) {
        g_id = "-";
        return error("bad request");
    }
    g_id = request.substr(0, idlen);
    if (idlen < request.size()) line = request.substr(idlen + 1);

    std::string cmd = line, arg;
    auto sp = line.find(' ');
    if (sp != std::string::npos) {
        cmd = line.substr(0, sp);
        arg = line.substr(sp + 1);
    }
    if (cmd == "ping") {
        reply("pong");
    } else if (cmd == "sync") {
        g_sync_id = g_id;
        g_sync_wait = 2;
    } else if (cmd == "state") {
        state(a);
    } else if (cmd == "dump") {
        dump(a);
    } else if (cmd == "key") {
        ui::Key k;
        if (!key_named(arg, k)) return error("unknown key");
        ui::inject_key(k);
        reply("");
    } else if (cmd == "type") {
        for (char c : arg) {
            ui::Key k;
            k.ch = c;
            ui::inject_key(k);
        }
        reply("");
    } else if (cmd == "hold") {
        // hold <char> <0|1>, use "space" for ' '
        auto sp2 = arg.find(' ');
        std::string c = arg.substr(0, sp2);
        bool on = sp2 != std::string::npos && arg.substr(sp2 + 1) == "1";
        ui::set_held_override(c == "space" ? ' ' : c[0], on);
        reply("");
    } else if (cmd == "press") {
        // press <char> <0|1>: hold/release a simulated physical key
        auto sp2 = arg.find(' ');
        bool on = sp2 != std::string::npos && arg.substr(sp2 + 1) == "1";
        ui::set_sim_pressed(on && !arg.empty() ? arg[0] : 0);
        reply("");
    } else if (cmd == "g0") {
        g_g0 = true;
        reply("");
    } else if (cmd == "idle") {
        a.last_input_ms -= uint32_t(atol(arg.c_str()));
        reply("");
    } else if (cmd == "settime") {
        device::set_unix_time(uint32_t(atol(arg.c_str())));
        reply("");
    } else if (cmd == "reboot") {
        reply("");
        Serial.flush();
        delay(50);
        ESP.restart();
    } else if (cmd == "factory") {
        a.router.clear_now();
        a.session.close();
        WipeResult w = wipe_vault(a.store, a.guard);
        if (!w.erased || !w.reset) {
            error(w.erased ? "counter reset failed" : "erase failed");
            return;
        }
        reply("");
        Serial.flush();
        device::save_settings(Settings());
        delay(50);
        ESP.restart();
    } else {
        error("unknown command");
    }
}

}  // namespace

void begin() {
    Serial.begin(115200);
    Serial.println("CVTEST READY");
}

void poll(ui::App& a) {
    if (g_sync_wait > 0 && ui::pending_keys() == 0 && --g_sync_wait == 0) {
        g_id = g_sync_id;
        reply("");
    }
    while (Serial.available()) {
        char c = char(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            std::string line = g_line;
            g_line.clear();
            if (!line.empty()) handle(a, line);
        } else if (g_line.size() < 512) {
            g_line += c;
        }
    }
}

bool take_g0() {
    bool v = g_g0;
    g_g0 = false;
    return v;
}

}  // namespace test
}  // namespace cv

#endif  // CV_TEST_HOOKS
