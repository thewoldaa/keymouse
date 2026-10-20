// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/binding_table.h"

namespace km {

std::shared_ptr<const BindingTable> BindingTable::build(const std::vector<Binding>& bindings) {
    // build() is a member, so it may call the private constructor. Filling the
    // table before converting to shared_ptr<const> keeps the type system
    // honest: callers cannot mutate what they receive.
    std::shared_ptr<BindingTable> table(new BindingTable());

    for (const auto& binding : bindings) {
        if (!binding.valid() || binding.action == Action::None) {
            continue;
        }
        if (binding.chord.key >= kKeySlots) {
            continue;
        }

        const uint32_t mods = binding.chord.mods & (kModSlots - 1);
        const uint32_t index = (mods << 8) | binding.chord.key;

        BindingEntry& entry = table->slots_[index];
        if (!entry.bound()) {
            ++table->count_;
        }
        entry.action = binding.action;
        entry.mode = binding.mode;
        entry.swallow = binding.swallow;
    }
    return table;
}

BindingTableStore::BindingTableStore() {
    // The store always holds a valid table so current() never returns null.
    auto empty = BindingTable::build({});
    current_.store(empty.get(), std::memory_order_release);
    tables_.push_back(std::move(empty));
}

std::shared_ptr<const BindingTable> BindingTableStore::publish(const std::vector<Binding>& bindings) {
    if (has_published_ && bindings == last_bindings_ && !tables_.empty()) {
        return tables_.back();
    }
    last_bindings_ = bindings;
    has_published_ = true;

    auto table = BindingTable::build(bindings);
    tables_.push_back(table);
    // The raw pointer is what the hook thread reads. Every table stays alive in
    // tables_ for the lifetime of the store, so the pointer outlives any
    // concurrent lookup.
    current_.store(table.get(), std::memory_order_release);
    return table;
}

}  // namespace km
