#include "core/mnemonic_entry.h"

namespace cv {

MnemonicEntry::MnemonicEntry(int words, const SString& initial) : words_(size_t(words)) {
    auto init = bip39_split(initial);
    if (int(init.size()) == words)
        for (int i = 0; i < words; i++) words_[i] = init[i];
    for (auto& w : init) wipe(w);
    while (idx_ < words - 1 && !words_[idx_].empty()) idx_++;
    update();
}

MnemonicEntry::~MnemonicEntry() {
    for (auto& w : words_) wipe(w);
    wipe(prefix_);
}

void MnemonicEntry::update() {
    ncand_ = prefix_.empty() ? 0 : bip39_complete(prefix_.c_str(), cand_, kMaxCandidates);
    sel_ = 0;
}

MnemonicEntry::Event MnemonicEntry::type_char(char c) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (c < 'a' || c > 'z' || prefix_.size() >= 8) return Event::None;
    prefix_ += c;
    update();
    if (ncand_ == 1 && prefix_.size() >= 4) return accept_word(cand_[0]);
    return Event::None;
}

void MnemonicEntry::backspace() {
    if (!prefix_.empty()) {
        prefix_.pop_back();
    } else if (idx_ > 0 || !words_[idx_].empty()) {
        if (words_[idx_].empty()) idx_--;
        prefix_ = words_[idx_];
        wipe(words_[idx_]);
        if (!prefix_.empty()) prefix_.pop_back();
    }
    update();
}

MnemonicEntry::Event MnemonicEntry::accept() {
    if (ncand_ > 0) return accept_word(cand_[sel_]);
    if (prefix_.empty() && !words_[idx_].empty()) return advance();
    return Event::None;
}

void MnemonicEntry::select_next() {
    if (ncand_) sel_ = (sel_ + 1) % candidates_shown();
}

void MnemonicEntry::select_prev() {
    if (ncand_) sel_ = (sel_ + candidates_shown() - 1) % candidates_shown();
}

void MnemonicEntry::prev_word() {
    if (idx_ == 0) return;
    int i = bip39_index(prefix_.c_str());
    if (i >= 0) words_[idx_] = kBip39Words[i];
    wipe(prefix_);
    idx_--;
    update();
}

void MnemonicEntry::next_word() {
    if (idx_ < count() - 1 && !words_[idx_].empty()) {
        idx_++;
        update();
    }
}

bool MnemonicEntry::complete() const {
    for (auto& w : words_)
        if (w.empty()) return false;
    return true;
}

SString MnemonicEntry::joined() const {
    SString out;
    for (size_t i = 0; i < words_.size(); i++) {
        if (i) out += ' ';
        out += words_[i];
    }
    return out;
}

MnemonicEntry::Event MnemonicEntry::accept_word(int word_index) {
    words_[idx_] = kBip39Words[word_index];
    wipe(prefix_);
    return advance();
}

MnemonicEntry::Event MnemonicEntry::advance() {
    update();
    if (idx_ < count() - 1) {
        idx_++;
        return Event::Accepted;
    }
    for (int i = 0; i < count(); i++) {
        if (words_[i].empty()) {
            idx_ = i;
            return Event::Accepted;
        }
    }
    return Event::Completed;
}

}  // namespace cv
