// Screen stack. Navigation changes requested during event handling are
// queued and applied afterwards, so a screen may safely pop itself.
#pragma once

#include <memory>
#include <vector>

#include "ui/gfx.h"
#include "ui/input.h"

namespace cv {
namespace ui {

class Screen {
public:
    virtual ~Screen() = default;
    virtual void on_key(const Key& k) = 0;
    virtual void draw(Canvas& c) = 0;
    virtual void tick(uint32_t now_ms) { (void)now_ms; }
    // Called when this screen becomes the top again.
    virtual void on_resume() { invalidate(); }
    void invalidate() { dirty = true; }
    bool dirty = true;
};

using ScreenPtr = std::unique_ptr<Screen>;

class Router {
public:
    void push(ScreenPtr s) { ops_.push_back({Op::Push, std::move(s)}); }
    void pop() { ops_.push_back({Op::Pop, nullptr}); }
    void replace(ScreenPtr s) {
        pop();
        push(std::move(s));
    }
    void reset(ScreenPtr s) {
        ops_.push_back({Op::Clear, nullptr});
        push(std::move(s));
    }
    // Destroys all screens immediately (used when locking).
    void clear_now() {
        ops_.clear();
        stack_.clear();
    }
    Screen* top() { return stack_.empty() ? nullptr : stack_.back().get(); }
    size_t depth() const { return stack_.size(); }

    void apply() {
        if (ops_.empty()) return;
        auto ops = std::move(ops_);
        ops_.clear();
        for (auto& op : ops) {
            switch (op.kind) {
                case Op::Push: stack_.push_back(std::move(op.screen)); break;
                case Op::Pop:
                    if (!stack_.empty()) stack_.pop_back();
                    break;
                case Op::Clear: stack_.clear(); break;
            }
        }
        if (Screen* t = top()) t->on_resume();
    }

private:
    struct Op {
        enum Kind { Push, Pop, Clear } kind;
        ScreenPtr screen;
    };
    std::vector<ScreenPtr> stack_;
    std::vector<Op> ops_;
};

}  // namespace ui
}  // namespace cv
