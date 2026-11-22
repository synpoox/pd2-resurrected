// PD2 item rules: Ormus' Robes' random sorceress skill (properties func 25; skill on cast is the Cast Triggers
// companion's, IR2), curses (max curses, curse resistance), skill while equipped and phasing, and IR3's combat
// stats (critical and deadly strike, op bases, life steal cap, crushing blow), item_rules.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2r {
struct Unit;
}

// PD2's per-hand item stat sum (ProjectDiablo.dll 0x10381720): the stat's item-type layers that the wielded weapon
// counts (the masteries' passive stats sit on their passiveitype's layer); layer 0 is not counted.
auto HandStatSum(d2r::Unit* unit, uint32_t stat) noexcept -> int32_t;

namespace sigscan {
struct Entry;
}
// The RVA of the D2RLoader .maho thunk slot (`jmp [slot]`) behind the E8 call at `entry` + `callOffset`; 0 when it is not one.
auto ThunkSlotRva(const sigscan::Entry& entry, uint32_t callOffset) noexcept -> uint64_t;
// Swaps that slot for `wrapper`; the slot's old target goes to `original` first, so the wrapper never runs without it.
auto SwapThunkSlot(const D2RL::PluginContext* context, const sigscan::Entry& entry, uint32_t callOffset, uint64_t wrapper,
                   uint64_t* original) noexcept -> bool;

// Points the E8 call at `callAt` in the one match of `pattern` at a relay page that jumps to `entry`, by a MutationService
// byte patch with the stock call as expected bytes; the call's old target goes to `original` first.
auto RedirectCall(const D2RL::PluginContext* context, const char* pattern, uint32_t callAt, uint64_t entry,
                  uint64_t* original) noexcept -> bool;

// Installs the item rules; `report` gets one entry per rule. Returns how many were installed.
auto InstallItemRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto ItemRuleCount() noexcept -> int;
// Live counters for the status command.
auto ItemRuleCounters(char* out, size_t size) noexcept -> void;
