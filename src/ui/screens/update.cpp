// Settings > Update firmware: installs a signed .cvu from the SD card (#85).
#include <M5Cardputer.h>

#include <memory>

#include "core/messages.h"
#include "core/update.h"
#include "platform/ota.h"
#include "platform/sd_backup.h"
#include "platform/update_key.h"
#include "ui/app.h"
#include "ui/gfx.h"
#include "ui/layout.h"
#include "version.h"

namespace cv {
namespace ui {

namespace {

using Hold = std::shared_ptr<sd::MountHold>;

constexpr size_t kChunk = 4096;

void show(const char* title, const char* text, uint16_t col = color::bad) {
    app().router.push(make_message(title, text, col));
}

// The running version; a build whose CV_VERSION doesn't parse never updates.
bool running_version(Version& v) { return parse_version(CV_VERSION, v); }

void show_header_error(UpdateError e, const UpdateHeader& h) {
    char buf[200];
    switch (e) {
        case UpdateError::BadSignature: return show(msg::kUpdateFailedTitle, msg::kUpdateBadSignature);
        case UpdateError::WrongDevice: return show(msg::kUpdateFailedTitle, msg::kUpdateWrongDevice);
        case UpdateError::TooBig: return show(msg::kUpdateFailedTitle, msg::kUpdateTooBig);
        case UpdateError::TooOld:
            snprintf(buf, sizeof buf, msg::kUpdateTooOld, h.version.str().c_str(), CV_VERSION);
            return show(msg::kUpdateOlderTitle, buf, color::warn);
        case UpdateError::Truncated: return show(msg::kUpdateFailedTitle, msg::kUpdateTruncated);
        case UpdateError::HashMismatch: return show(msg::kUpdateFailedTitle, msg::kUpdateDamaged);
        case UpdateError::BadFile:
        case UpdateError::Ok: break;
    }
    show(msg::kUpdateFailedTitle, msg::kUpdateBadFile);
}

// Reads and checks a file's header, and that the file is as long as it says.
UpdateError read_header(const std::string& name, UpdateHeader& h, bool& card_ok) {
    uint8_t buf[kUpdateHeaderSize];
    size_t got = 0, size = 0;
    card_ok = sd::read_update_head(name, buf, sizeof buf, got, size);
    Version running;
    if (!card_ok) return UpdateError::BadFile;
    if (!running_version(running)) return UpdateError::BadFile;
    UpdateError e =
        check_update_header(buf, got, kUpdatePublicKey, running, ota::free_slot_size(), h);
    if (e == UpdateError::Ok && size != kUpdateHeaderSize + h.image_len) {
        e = size < kUpdateHeaderSize + h.image_len ? UpdateError::Truncated : UpdateError::BadFile;
    }
    return e;
}

void draw_progress(float frac) {
    App& a = app();
    if (!a.canvas) return;
    Canvas& c = *a.canvas;
    clear(c);
    header(c, msg::kUpdateTitle, color::warn);
    dialog(c, msg::kUpdateInstalling, "Keep the power on.", color::warn);
    progress(c, 40, kH / 2 + 14, kW - 80, 6, frac, color::warn);
    c.pushSprite(0, 0);
}

void install(const std::string& name, UpdateHeader h) {
    App& a = app();
    ota::Installer inst;
    if (!inst.begin(h.image_len)) return show(msg::kUpdateFailedTitle, msg::kUpdateWriteFailed);
    UpdateImageCheck check(h);
    bool write_failed = false, overflow = false;
    size_t done = 0, drawn = 0;
    draw_progress(0);
    bool read_ok = sd::stream_update(name, kUpdateHeaderSize, kChunk,
                                     [&](const uint8_t* p, size_t n) {
                                         if (!check.add(p, n)) {
                                             overflow = true;
                                             return false;
                                         }
                                         if (!inst.write(p, n)) {
                                             write_failed = true;
                                             return false;
                                         }
                                         done += n;
                                         if (done - drawn >= 32 * 1024) {
                                             drawn = done;
                                             draw_progress(float(done) / float(h.image_len));
                                         }
                                         return true;
                                     });
    if (Screen* t = a.router.top()) t->invalidate();
    // Every failure leaves the boot slot as it was: the Installer drops the write.
    if (write_failed) return show(msg::kUpdateFailedTitle, msg::kUpdateWriteFailed);
    if (overflow) return show(msg::kUpdateFailedTitle, msg::kUpdateBadFile);
    if (!read_ok) {
        if (sd::wedged())
            return show(msg::kSdStalledTitle, msg::kSdStalled);
        return show(msg::kUpdateFailedTitle, msg::kUpdateReadFailed);
    }
    UpdateError e = check.finish();
    if (e != UpdateError::Ok) return show_header_error(e, h);
    draw_progress(1);
    if (!inst.finish(h.version)) return show(msg::kUpdateFailedTitle, msg::kUpdateWriteFailed);
    // The new version starts on the next boot; nothing decrypted survives the restart.
    sd::unmount();
    a.busy("Restarting...");
    a.session.close();
    delay(300);
    ESP.restart();
}

void choose(const std::string& name) {
    UpdateHeader h;
    bool card_ok = false;
    UpdateError e = read_header(name, h, card_ok);
    if (!card_ok) return show(msg::kUpdateFailedTitle, msg::kUpdateReadFailed);
    if (e != UpdateError::Ok) return show_header_error(e, h);
    char buf[200];
    std::string v = h.version.str();
    snprintf(buf, sizeof buf, msg::kUpdateConfirm, v.c_str(), CV_VERSION);
    app().router.push(make_confirm(msg::kUpdateTitle, buf, [name, h] { install(name, h); }));
}

}  // namespace

void start_update_flow() {
    App& a = app();
    // A restart would lose a vault that is only in memory.
    if (a.flash_missing) return show(msg::kUpdateTitle, msg::kUpdateInMemory);
    switch (ota::layout()) {
        case LayoutCheck::NotOwnTable:
            return show(msg::kUpdateLauncherTitle, msg::kUpdateLauncher, color::warn);
        case LayoutCheck::NoSecondSlot:
            return show(msg::kUpdateNoSlotTitle, msg::kUpdateNoSlot, color::warn);
        case LayoutCheck::Ok: break;
    }
    a.busy("Reading SD card...");
    if (!sd::mount()) {
        if (sd::wedged()) return show(msg::kSdStalledTitle, msg::kSdStalled);
        return show("No SD card", msg::kUpdateNoCard, color::warn);
    }
    auto names = sd::list_updates();
    if (names.empty()) {
        sd::unmount();
        return show(msg::kUpdateNoFileTitle, msg::kUpdateNoFile, color::warn);
    }
    // Stays mounted while the menu is open; leaving it (or locking) releases the card.
    Hold hold = std::make_shared<sd::MountHold>();
    Version running;
    bool have_running = running_version(running);
    std::vector<MenuItem> items;
    for (const std::string& n : names) {
        UpdateHeader h;
        bool card_ok = false;
        UpdateError e = read_header(n, h, card_ok);
        MenuItem it;
        std::string name = n;
        it.action = [name, hold] { choose(name); };
        if (e == UpdateError::Ok || e == UpdateError::TooOld) {
            it.label = SString(("v" + h.version.str()).c_str());
            if (e == UpdateError::TooOld) {
                it.detail = "older";
                it.color = color::dim;
            } else if (have_running && compare_versions(h.version, running) == 0) {
                it.detail = "installed";
            }
        } else {
            it.label = SString(n.c_str());
            it.detail = "not valid";
            it.color = color::dim;
        }
        items.push_back(std::move(it));
    }
    a.router.push(make_menu(msg::kUpdateTitle, std::move(items)));
}

ScreenPtr with_update_outcome(std::function<ScreenPtr()> next) {
    Version v;
    UpdateOutcome o = ota::take_outcome(v);
    if (o == UpdateOutcome::None) return next();
    char buf[200];
    std::string vs = v.str();
    auto then = [next] { app().router.reset(next()); };
    if (o == UpdateOutcome::Installed) {
        snprintf(buf, sizeof buf, msg::kUpdateDone, vs.c_str());
        return make_message(msg::kUpdateDoneTitle, buf, color::good, then);
    }
    snprintf(buf, sizeof buf, msg::kUpdateRolledBack, vs.c_str(), CV_VERSION);
    return make_message(msg::kUpdateRolledBackTitle, buf, color::warn, then);
}

}  // namespace ui
}  // namespace cv
