#ifdef CV_TEST_HOOKS

#include "test_hooks.h"

#include <M5Cardputer.h>

#include <string>

namespace cv {
namespace test {

namespace {

std::string g_line;
bool g_g0 = false;

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
    Serial.println(s.c_str());
}
void error(const char* s) {
    Serial.print("ERR ");
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

void handle(ui::App& a, const std::string& line) {
    std::string cmd = line, arg;
    auto sp = line.find(' ');
    if (sp != std::string::npos) {
        cmd = line.substr(0, sp);
        arg = line.substr(sp + 1);
    }
    if (cmd == "ping") {
        reply("pong");
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
        reply("");
        Serial.flush();
        a.router.clear_now();
        a.session.close();
        a.store.erase();
        a.guard.reset();
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
