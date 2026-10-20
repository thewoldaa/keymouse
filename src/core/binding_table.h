// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/action.h"
#include "core/binding.h"
#include "core/chord.h"

namespace km {

struct BindingEntry {
    Action action = Action::None;
    BindMode mode = BindMode::Hold;
    SwallowMode swallow = SwallowMode::Swallow;

    bool bound() const { return action != Action::None; }
};

// Immutable chord lookup table.
//
// Built once per config reload and published to the hook thread, which must be
// able to answer "is this chord bound, and should I swallow it?" without
// allocating or locking. The table is a flat array indexed by
// (mods << 8) | key, so lookup is a single bounds-checked array access.
//
// Tables are never mutated after construction.
class BindingTable {
public:
    static constexpr uint32_t kKeySlots = 256;
    static constexpr uint32_t kModSlots = 32;
    static constexpr uint32_t kSlotCount = kModSlots * kKeySlots;

    // Builds a table. Invalid bindings are skipped.
    static std::shared_ptr<const BindingTable> build(const std::vector<Binding>& bindings);

    // Returns the entry for a chord, or nullptr when unbound.
    const BindingEntry* find(ModMask mods, KeyId key) const {
        if (key >= kKeySlots) {
            return nullptr;
        }
        const uint32_t index = (static_cast<uint32_t>(mods & (kModSlots - 1)) << 8) | key;
        const BindingEntry& entry = slots_[index];
        return entry.bound() ? &entry : nullptr;
    }

    bool empty() const { return count_ == 0; }
    size_t count() const { return count_; }

private:
    // Private so the only way to obtain a table is build(), which returns a
    // shared_ptr<const BindingTable>. Callers therefore cannot mutate one.
    BindingTable() = default;

private:
    BindingEntry slots_[kSlotCount]{};
    size_t count_ = 0;
};

// Holds every table ever built.
//
// Reloads are rare (the user edited a setting), so retaining old tables costs a
// few kilobytes and removes any lifetime hazard: a table handed to the hook
// thread stays valid for the lifetime of the process, no matter how many times
// the configuration is reloaded while that thread is running.
class BindingTableStore {
public:
    BindingTableStore();

    // Builds a table and makes it current. Call from a single publishing
    // thread (the UI thread) while other threads read current().
    //
    // Publishing the same binding list twice returns the existing table
    // instead of allocating another one. apply_config() runs on every frame
    // while a slider is dragged, so without this check a 32 KB table would be
    // retained per frame for no reason.
    std::shared_ptr<const BindingTable> publish(const std::vector<Binding>& bindings);

    // The current table. Safe to call from any thread. Never returns null.
    const BindingTable* current() const {
        return current_.load(std::memory_order_acquire);
    }

private:
    // Only touched by the publishing thread. Reallocation moves the
    // shared_ptrs, never the tables they point at, so published pointers stay
    // valid.
    std::vector<std::shared_ptr<const BindingTable>> tables_;
    std::atomic<const BindingTable*> current_{nullptr};
    // The bindings behind `current_`, compared to skip duplicate publications.
    // Only valid for the publishing thread, like tables_.
    std::vector<Binding> last_bindings_;
    bool has_published_ = false;
};

}  // namespace km
