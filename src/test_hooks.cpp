#ifdef CV_TEST_HOOKS

#include "test_hooks.h"

#include <M5Cardputer.h>

#include <string>
#include <vector>

#include "core/flows.h"
#include "ui/gfx.h"
#include "ui/sim_keyboard.h"

namespace cv {
namespace test {

namespace {

std::string g_line;
bool g_g0 = false;
// Id of the request being answered ("-" when the line had none). Every reply
// carries it, so the host can tell a late reply from the one it waits for.
std::string g_id = "-";

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

// Starts the reply line; the payload and the newline follow.
void reply_begin() {
    Serial.print("OK ");
    Serial.print(g_id.c_str());
    Serial.print(' ');
}
void reply(const std::string& s) {
    reply_begin();
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

// What is really on flash/NVS, not what the session holds in RAM.
void storage(ui::App& a) {
    FlashStore::Probe pr = a.store.probe();
    uint32_t fails = 0;
    KvRead kr = a.guard_kv.get_u32("fails", fails);
    std::string counter = kr == KvRead::Ok ? "ok:" + std::to_string(fails)
                          : kr == KvRead::Missing ? "missing" : "error";
    auto b = [](bool v) { return v ? "true" : "false"; };
    std::string j = "{";
    j += "\"mounted\":" + std::string(b(pr.mounted));
    j += ",\"vault\":" + std::string(b(pr.vault));
    j += ",\"tmp\":" + std::string(b(pr.tmp));
    j += ",\"bak\":" + std::string(b(pr.bak));
    j += ",\"scanned\":" + std::string(b(pr.scanned));
    j += ",\"magic_hits\":" + std::to_string(pr.magic_hits);
    j += ",\"counter\":\"" + counter + "\"}";
    reply(j);
}

// The screen as the loop would draw it now, as ONE reply line:
// "OK <id> <w> <h> <rows>", RGB565 as 4 hex characters per pixel, row after row.
// tools/screenshots.py turns it into a PNG. Revealed secrets show up in it.
void shot(ui::App& a) {
    ui::Screen* s = a.router.top();
    if (!s || !a.canvas) return error("no screen");
    s->draw(*a.canvas);
    a.draw_toast(*a.canvas);
    // Streamed: the whole line is ~130 KB, too big to build in RAM.
    reply_begin();
    Serial.printf("%d %d ", ui::kW, ui::kH);
    char hex[ui::kW * 4 + 1];
    for (int y = 0; y < ui::kH; y++) {
        ui::row_hex(*a.canvas, y, hex);
        Serial.print(hex);
    }
    Serial.println();
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
    } else if (cmd == "state") {
        state(a);
    } else if (cmd == "dump") {
        dump(a);
    } else if (cmd == "storage") {
        storage(a);
    } else if (cmd == "shot") {
        shot(a);
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
    } else if (cmd == "keys") {
        // keys [x,y ...]: the full set of simulated held keys, as keyboard
        // matrix positions (column x 0-13, row y 0-3); no argument releases all.
        std::vector<Point2D_t> pos;
        size_t i = 0;
        bool ok = true;
        while (ok && i < arg.size()) {
            if (arg[i] == ' ') {
                i++;
                continue;
            }
            int x = -1, y = -1, n = 0;
            ok = sscanf(arg.c_str() + i, "%d,%d%n", &x, &y, &n) == 2 && x >= 0 && x < 14 &&
                 y >= 0 && y < 4;
            if (!ok) break;
            Point2D_t p;
            p.x = x;
            p.y = y;
            pos.push_back(p);
            i += size_t(n);
        }
        if (!ok) {
            error("bad key position");
        } else {
            ui::set_sim_keys(pos);
            reply("");
        }
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
