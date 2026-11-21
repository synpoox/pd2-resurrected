// PD2's stacks in the RotW stash tabs (Gems, Materials, Runes; #1537). A tab slot is one item with a count byte (item
// data +0x9c, at most 99, saved as 8 bits) that D2R's deposit raises by one and its withdraw lowers by one, handing out a
// new single. PD2 makes gems, runes and the other AdvancedStashStackable rows stackable, so a deposited stack of 12
// counted as one and 11 were lost. Here a deposit raises the count by the stack's quantity, and the "tab full" check
// refuses a stack that would not fit whole (the stack stays where it was, as D2R does for a full slot).
//
// The deposit (93847: 0x4f92f0, drag and Ctrl-click alike, logged in game) runs on the server:
//   count  0x4f9898: (slot item) -> u8 through a D2RLoader .maho thunk, then `cmp al,0x63; jb` (the full check).
//   add    0x4f9cd3: (stash proxy, item class) -> slot item through another .maho thunk, +1; then the caller sends the
//          slot item to the client (0x535f60, flag 0x2000), whose tab update (0x1c2b10) adds one on its own copy
//          (a new slot is inserted with the count the item carries).
// So the server adds the stack one item at a time and sends one update per extra item: the client's copy, in a local or
// remote game, counts along. No PluginSDK service sees these tabs (its stash moves cover the normal grids only).
// The deposited item and the caller's frame live only in rbx / rbp, which C++ cannot read, so each thunk slot goes to
// a 20-byte stub that copies rbx and rbp into r8 / r9 (unused by both calls) and jumps to the C++ wrapper; the wrapper
// tells the deposit's call from the others (the withdraw's undo paths) by its return address.
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
// the add call, and after it the caller's own update send: `mov rcx,[rbp-0x78] / mov r8,r15 / ... mov rdx,rdi / ...
// mov [rsp+0x20],2 / call` with the position dwords at rbp-0x50, -0x4c, -0x44, -0x48 (93847: 0x4f9cd3, 0x4f9eed)
sigscan::Entry ServerAddSite {"StashTabServerAdd", "stash tabs", sigscan::Kind::Code, 0, 0,
                              "41 8b d7 48 8b cf e8 ?? ?? ?? ?? 4c 8b f8 48 85 c0 0f 85"};
constexpr uint32_t ServerAddCall = 6;
// The caller's own update send, which also checks the frame: `mov edx,1 / mov rcx,r15 / call SetItemSending / mov eax,[rbp-0x48]
// / mov r9d,0x2000 / ... / the position dwords at rbp-0x50, -0x4c, -0x44, -0x48 / mov [rsp+0x20],2 / call SendItem / xor edx,edx
// / mov rcx,r15 / call SetItemSending` (93847: 0x4f9eac). The three calls give the engine functions, so nothing is a fixed RVA:
//   SendItem (game, owner, item, flags, mode, 4 position dwords): item update to clients
//   SetItemSending (item, on): the caller brackets its 0x2000 send with it
constexpr const char* SendPattern =
    "ba 01 00 00 00 49 8b cf e8 ?? ?? ?? ?? 8b 45 b8 41 b9 00 20 00 00 48 8b 4d 88 4d 8b c7 89 44 24 40 48 8b d7 8b 45 bc 89 44 "
    "24 38 8b 45 b4 89 44 24 30 8b 45 b0 89 44 24 28 c7 44 24 20 02 00 00 00 e8 ?? ?? ?? ?? 33 d2 49 8b cf e8 ?? ?? ?? ??";
sigscan::Entry SetSendingOnCall {"StashTabSetSendingOn", "stash tabs", sigscan::Kind::Rip, 9, 13, SendPattern};
sigscan::Entry SendItemCall {"StashTabSendItem", "stash tabs", sigscan::Kind::Rip, 66, 70, SendPattern};
sigscan::Entry SetSendingOffCall {"StashTabSetSendingOff", "stash tabs", sigscan::Kind::Rip, 76, 80, SendPattern};
constexpr int32_t StatQuantity = 70;
constexpr int32_t CountByte = 0x9c;  // item data

using CountFn = uint8_t (*)(Unit* slotItem) noexcept;
using AddFn = Unit* (*)(void* proxy, uint64_t itemClass) noexcept;
using SendFn = void (*)(void* game, void* owner, Unit* item, uint32_t flags, uint32_t mode, uint32_t a, uint32_t b, uint32_t c,
                        uint32_t d) noexcept;
CountFn g_count = nullptr;
AddFn g_add = nullptr;
uint64_t g_countCheckReturn = 0, g_serverAddReturn = 0;
std::atomic<uint32_t> g_deposits {0}, g_refused {0};

auto Quantity(Unit* item) noexcept -> int32_t {
	return item == nullptr ? 0 : d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(item, StatQuantity, 0);
}

auto SlotCount(Unit* slot) noexcept -> uint8_t {
	void* data = slot != nullptr ? d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitItemData)(slot) : nullptr;
	return data != nullptr ? d2r::At<uint8_t>(data, CountByte) : 0;
}

auto Count(Unit* slotItem, uint64_t, Unit* rbx, uint8_t*) noexcept -> uint8_t {
	if (reinterpret_cast<uint64_t>(_ReturnAddress()) != g_countCheckReturn) {
		return g_count(slotItem);
	}
	// the deposit check: the slot's own count byte (D2RCore's getter read 0 for a slot of 1 in game)
	const uint8_t count = SlotCount(slotItem);
	const uint8_t seen = CheckedCount(count, Quantity(rbx));
	if (seen >= TabMax) {
		plog::Info("stash tabs: refused a stack of %d onto a slot of %u (max %d)", Units(Quantity(rbx)), count, TabMax);
		g_refused.fetch_add(1);
	}
	return seen;
}

auto Add(void* proxy, uint64_t itemClass, Unit* rbx, uint8_t* rbp) noexcept -> Unit* {
	Unit* slot = g_add(proxy, itemClass);
	if (reinterpret_cast<uint64_t>(_ReturnAddress()) != g_serverAddReturn || slot == nullptr) {
		return slot;
	}
	const int32_t units = Units(Quantity(rbx));
	void* const game = *reinterpret_cast<void**>(rbp - 0x78);
	const auto at = [rbp](int32_t off) { return *reinterpret_cast<uint32_t*>(rbp + off); };
	int32_t added = 1;
	for (; added < units; ++added) {
		// the update for the item just added, as the caller sends it after the last
		d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetSendingOnCall.rva)(slot, 1);
		d2r::Engine<SendFn>(SendItemCall.rva)(game, proxy, slot, 0x2000, 2, at(-0x50), at(-0x4c), at(-0x44), at(-0x48));
		d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetSendingOffCall.rva)(slot, 0);
		if (g_add(proxy, itemClass) == nullptr) {
			plog::Warn("stash tabs: add %d of %d for class %d failed, slot count %u", added + 1, units,
			           static_cast<int32_t>(itemClass), SlotCount(slot));
			break;
		}
	}
	plog::Info("stash tabs: deposit of class %d added %d of %d, slot count %u", static_cast<int32_t>(itemClass), added, units,
	           SlotCount(slot));
	g_deposits.fetch_add(1);
	return slot;
}

// mov r8,rbx / mov r9,rbp / jmp [rip+0] / target
auto MakeStub(const void* target) noexcept -> uint64_t {
	static uint8_t* page = nullptr;
	static size_t used = 0;
	constexpr uint8_t Head[] = {0x49, 0x89, 0xd8, 0x49, 0x89, 0xe9, 0xff, 0x25, 0, 0, 0, 0};
	if (page == nullptr) {
		page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (page == nullptr) {
			return 0;
		}
	}
	uint8_t* stub = page + used;
	DWORD old = 0;
	if (!VirtualProtect(page, 4096, PAGE_READWRITE, &old)) {
		return 0;
	}
	std::memcpy(stub, Head, sizeof(Head));
	std::memcpy(stub + sizeof(Head), &target, sizeof(target));
	used += 32;
	if (!VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old)) {
		return 0;
	}
	FlushInstructionCache(GetCurrentProcess(), stub, 32);
	return reinterpret_cast<uint64_t>(stub);
}

}  // namespace

auto InstallStashStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	sigscan::Entry* const own[] = {&CountCheckSite, &ServerAddSite, &SetSendingOnCall, &SendItemCall, &SetSendingOffCall};
	sigscan::Resolve(own, 5, context->exeBase, nullptr, 0);
	for (const sigscan::Entry* e : own) {
		plog::Info("stash tabs: %s %s (rva 0x%llx)", e->name, sigscan::StateText(e->state), static_cast<unsigned long long>(e->rva));
	}
	if (SetSendingOnCall.rva == 0 || SendItemCall.rva == 0 || SetSendingOffCall.rva == 0 ||
	    SetSendingOnCall.rva != SetSendingOffCall.rva) {
		std::snprintf(report, reportSize, "stash tabs FAILED (send functions not found)");
		return false;
	}
	if (ThunkSlotRva(ServerAddSite, ServerAddCall) == 0 || ThunkSlotRva(CountCheckSite, CountCheckCall) == 0) {
		std::snprintf(report, reportSize, "stash tabs FAILED (call sites or thunks differ)");
		return false;
	}
	g_countCheckReturn = d2r::imageBase + CountCheckSite.rva + CountCheckCall + 5;
	g_serverAddReturn = d2r::imageBase + ServerAddSite.rva + ServerAddCall + 5;
	const uint64_t countStub = MakeStub(reinterpret_cast<const void*>(&Count));
	const uint64_t addStub = MakeStub(reinterpret_cast<const void*>(&Add));
	// add first: a failed count swap then only loses the full check, never the stack
	const bool ok = countStub != 0 && addStub != 0 &&
	                SwapThunkSlot(context, ServerAddSite, ServerAddCall, addStub, reinterpret_cast<uint64_t*>(&g_add)) &&
	                SwapThunkSlot(context, CountCheckSite, CountCheckCall, countStub, reinterpret_cast<uint64_t*>(&g_count));
	std::snprintf(report, reportSize, ok ? "stash tabs take stacks whole" : "stash tabs FAILED (slot swap)");
	return ok;
}

auto StashStackCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "stash tabs: %u stack deposits, %u refused as full", g_deposits.load(), g_refused.load());
}
