// State machine behind the seed phrase entry screen: per-word prefix typing
// with BIP39 autocomplete, auto-accept on a unique 4+ letter prefix,
// backspacing into the previous word and word navigation.
#pragma once

#include "crypto/secure.h"
#include "features/bip39.h"

namespace cv {

class MnemonicEntry {
public:
    static constexpr int kMaxCandidates = 4;
    enum class Event { None, Accepted, Completed };

    MnemonicEntry(int words, const SString& initial);
    ~MnemonicEntry();
    MnemonicEntry(const MnemonicEntry&) = delete;
    MnemonicEntry& operator=(const MnemonicEntry&) = delete;

    Event type_char(char c);  // letters only; uppercase is folded
    void backspace();
    Event accept();  // accept the selected candidate (or advance past a filled word)
    void select_next();
    void select_prev();
    void prev_word();
    void next_word();

    int count() const { return int(words_.size()); }
    int index() const { return idx_; }
    const SString& prefix() const { return prefix_; }
    const SString& word(int i) const { return words_[i]; }
    int candidate_total() const { return ncand_; }
    int candidates_shown() const { return ncand_ < kMaxCandidates ? ncand_ : kMaxCandidates; }
    int candidate(int i) const { return cand_[i]; }
    int selected() const { return sel_; }
    bool complete() const;
    SString joined() const;

private:
    void update();
    Event accept_word(int word_index);
    Event advance();

    SVec<SString> words_;
    int idx_ = 0;
    SString prefix_;
    int cand_[kMaxCandidates] = {};
    int ncand_ = 0;
    int sel_ = 0;
};

}  // namespace cv
