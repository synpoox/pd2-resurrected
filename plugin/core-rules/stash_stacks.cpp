// PD2's stacks in the RotW stash tabs (Gems, Materials, Runes; #1537). A tab slot is one item with a count byte (item
// data +0x9c, at most 99, saved as 8 bits) that D2R's deposit raises by one and its withdraw lowers by one, handing out a
// new single. PD2 makes gems, runes and the other AdvancedStashStackable rows stackable, so a deposited stack of 12
// counted as one and 11 were lost. Here a deposit raises the count by the stack's quantity, and the "tab full" check
// refuses a stack that would not fit whole (the stack stays where it was, as D2R does for a full slot).
//
// No PluginSDK service sees the stash tabs (the SDK's stash moves cover the normal grids only), so this swaps two
// D2RLoader .maho thunk slots, found from their D2R call sites by signature:
//   count  (slot item) -> u8, read by the server deposit (93847: 0x4f9898) before `cmp al,0x63; jb`.
//   add    (stash proxy, item class) -> slot item, raises the count by one; called by the server deposit (0x4f9cd3,
//          the deposited item in rbx), the client's tab update (0x1c2c68, the received item in rsi) and the withdraw's
//          undo paths (those stay one).
// The deposited item lives only in the caller's rbx / rsi, which C++ cannot read, so each slot goes to a 20-byte stub
// that copies rbx and rsi into r8 / r9 (unused by both calls) and jumps to the C++ wrapper; the wrapper tells the call
// sites apart by its return address.
#include "stash_stacks.h"

#include "item_rules.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "stash_stack.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;
using namespace pd2::stashstack;

sigscan::Entry CountCheckSite {"StashTabCountCheck", "stash tabs", sigscan::Kind::Code, 0, 0,
                               "45 84 e4 0f 84 ?? ?? ?? ?? 48 8b c8 e8 ?? ?? ?? ?? 3c 63 0f 82"};
constexpr uint32_t CountCheckCall = 12;
sigscan::Entry ServerAddSite {"StashTabServerAdd", "stash tabs", sigscan::Kind::Code, 0, 0,
                              "41 8b d7 48 8b cf e8 ?? ?? ?? ?? 4c 8b f8 48 85 c0 0f 85"};
constexpr uint32_t ServerAddCall = 6;
sigscan::Entry ClientAddSite {"StashTabClientAdd", "stash tabs", sigscan::Kind::Code, 0, 0,
                              "84 c0 75 0a 48 8b d6 e8 ?? ?? ?? ?? eb 07 8b d3 e8 ?? ?? ?? ?? 48 8b b4 24 98 00 00 00"};
constexpr uint32_t ClientAddCall = 16;
constexpr uint64_t StashSlotItem = 0x46d8f0;  // (stash proxy, item class) -> the tab's item of that class, or null
constexpr int32_t StatQuantity = 70;

using CountFn = uint8_t (*)(Unit* slotItem) noexcept;
using AddFn = Unit* (*)(void* proxy, uint64_t itemClass) noexcept;
CountFn g_count = nullptr;
AddFn g_add = nullptr;
uint64_t g_countCheckReturn = 0, g_serverAddReturn = 0, g_clientAddReturn = 0;
std::atomic<uint32_t> g_deposits {0}, g_refused {0};

auto Quantity(Unit* item) noexcept -> int32_t {
	return item == nullptr ? 0 : d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(item, StatQuantity, 0);
}

auto Count(Unit* slotItem, uint64_t, Unit* rbx, Unit*) noexcept -> uint8_t {
	const uint8_t count = g_count(slotItem);
	if (reinterpret_cast<uint64_t>(_ReturnAddress()) != g_countCheckReturn) {
		return count;
	}
	const uint8_t seen = CheckedCount(count, Quantity(rbx));
	if (seen >= TabMax) {
		plog::Info("stash tabs: refused a stack of %d onto a slot of %u (max %d)", Units(Quantity(rbx)), count, TabMax);
		g_refused.fetch_add(1);
	}
	return seen;
}

auto Add(void* proxy, uint64_t itemClass, Unit* rbx, Unit* rsi) noexcept -> Unit* {
	const uint64_t back = reinterpret_cast<uint64_t>(_ReturnAddress());
	Unit* item = back == g_serverAddReturn ? rbx : back == g_clientAddReturn ? rsi : nullptr;
	const auto cls = static_cast<int32_t>(itemClass);
	// the client's tab update can carry the slot's own item: that is one, as D2R counts it
	if (item != nullptr && item == d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(StashSlotItem)(proxy, cls)) {
		item = nullptr;
	}
	const int32_t units = item == nullptr ? 1 : Units(Quantity(item));
	Unit* slot = g_add(proxy, itemClass);
	for (int32_t i = 1; i < units && slot != nullptr; ++i) {
		g_add(proxy, itemClass);
	}
	if (item != nullptr) {
		plog::Info("stash tabs: %s deposit of class %d added %d, slot count %u", back == g_serverAddReturn ? "server" : "client",
		           cls, units, slot != nullptr ? d2r::At<uint8_t>(d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitItemData)(slot), 0x9c) : 0u);
		g_deposits.fetch_add(1);
	}
	return slot;
}

// mov r8,rbx / mov r9,rsi / jmp [rip+0] / target
auto MakeStub(const void* target) noexcept -> uint64_t {
	static uint8_t* page = nullptr;
	static size_t used = 0;
	constexpr uint8_t Head[] = {0x49, 0x89, 0xd8, 0x49, 0x89, 0xf1, 0xff, 0x25, 0, 0, 0, 0};
	if (page == nullptr) {
		page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (page == nullptr) {
			return 0;
		}
	}
	uint8_t* stub = page + used;
	DWORD old = 0;
	VirtualProtect(page, 4096, PAGE_READWRITE, &old);
	std::memcpy(stub, Head, sizeof(Head));
	std::memcpy(stub + sizeof(Head), &target, sizeof(target));
	used += 32;
	VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old);
	FlushInstructionCache(GetCurrentProcess(), stub, 32);
	return reinterpret_cast<uint64_t>(stub);
}

}  // namespace

auto InstallStashStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	sigscan::Entry* const own[] = {&CountCheckSite, &ServerAddSite, &ClientAddSite};
	sigscan::Resolve(own, 3, context->exeBase, nullptr, 0);
	// both add sites call the same thunk: one slot, swapped once
	const uint64_t addSlot = ThunkSlotRva(ServerAddSite, ServerAddCall);
	if (addSlot == 0 || ThunkSlotRva(ClientAddSite, ClientAddCall) != addSlot || ThunkSlotRva(CountCheckSite, CountCheckCall) == 0) {
		std::snprintf(report, reportSize, "stash tabs FAILED (call sites or thunks differ)");
		return false;
	}
	g_countCheckReturn = d2r::imageBase + CountCheckSite.rva + CountCheckCall + 5;
	g_serverAddReturn = d2r::imageBase + ServerAddSite.rva + ServerAddCall + 5;
	g_clientAddReturn = d2r::imageBase + ClientAddSite.rva + ClientAddCall + 5;
	const uint64_t countStub = MakeStub(reinterpret_cast<const void*>(&Count));
	const uint64_t addStub = MakeStub(reinterpret_cast<const void*>(&Add));
	const bool ok = countStub != 0 && addStub != 0 &&
	                SwapThunkSlot(context, ServerAddSite, ServerAddCall, addStub, reinterpret_cast<uint64_t*>(&g_add)) &&
	                SwapThunkSlot(context, CountCheckSite, CountCheckCall, countStub, reinterpret_cast<uint64_t*>(&g_count));
	std::snprintf(report, reportSize, ok ? "stash tabs take stacks whole" : "stash tabs FAILED (slot swap)");
	return ok;
}

auto StashStackCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "stash tabs: %u stack deposits, %u refused as full", g_deposits.load(), g_refused.load());
}
