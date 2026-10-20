// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/chord.h"

namespace km {

// Immutable per-macro hotkey list, published to the hook thread.
//
// The list is tiny (one entry per macro in the library), so a linear scan beats
// a hash table: the hook thread must not allocate or lock, and a few dozen
// comparisons are free.
class MacroHotkeyTable {
public:
    struct Entry {
        Chord chord;
        std::string id;

        bool operator==(const Entry&) const = default;
    };

    MacroHotkeyTable() = default;
    explicit MacroHotkeyTable(std::vector<Entry> entries)
        : entries_(std::move(entries)) {}

    bool find(ModMask mods, KeyId key, size_t& index) const {
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Chord& chord = entries_[i].chord;
            if (chord.key == key && chord.mods == mods) {
                index = i;
                return true;
            }
        }
        return false;
    }

    const Entry& entry(size_t index) const { return entries_[index]; }
    size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

// Keeps every published table alive for the process lifetime.
//
// The hook thread reads the current table without locking and may hold the
// pointer for a while, so tables are never freed; a config reload is rare enough
// that retaining the handful of vectors is cheaper than reference counting on
// the hook thread.
class MacroHotkeyStore {
public:
    void publish(std::vector<MacroHotkeyTable::Entry> entries) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (has_published_ && entries == last_entries_) {
                return;
            }
            last_entries_ = std::move(entries);
            has_published_ = true;

            auto table = std::make_shared<const MacroHotkeyTable>(
                std::vector<MacroHotkeyTable::Entry>(last_entries_));
            retained_.push_back(table);
            published_ = table;
        }
        current_.store(published_.get(), std::memory_order_release);
    }

    const MacroHotkeyTable* current() const {
        return current_.load(std::memory_order_acquire);
    }

private:
    std::atomic<const MacroHotkeyTable*> current_{nullptr};
    std::mutex mutex_;
    std::vector<std::shared_ptr<const MacroHotkeyTable>> retained_;
    // The last published list, compared to avoid retaining a duplicate table
    // on every apply_config() call. Guarded by mutex_.
    std::vector<MacroHotkeyTable::Entry> last_entries_;
    std::shared_ptr<const MacroHotkeyTable> published_;
    bool has_published_ = false;
};

}  // namespace km
