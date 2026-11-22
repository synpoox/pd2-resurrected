// PD2's stacks in the RotW stash tabs (Gems, Materials, Runes; #1537). A tab slot is one item with a count byte (item
// data +0x9c, at most 255, saved as 8 bits) that D2R's deposit raises by one and its withdraw lowers by one, handing out a
// new single. PD2 makes gems, runes and the other AdvancedStashStackable rows stackable, so a deposited stack of 12
// counted as one and 11 were lost. Here a deposit raises the count by the stack's quantity up to the limit (a top-up,
// #1816): the "tab full" check passes while the slot is below the limit, the add stops at the limit, and the rest is made
// again as a new stack through the SDK (the deposit destroys the deposited item right after the add, 0x4f9ea7, a direct
// call with no thunk). A slot already at the limit refuses the stack, which stays where it was, as D2R does for a full slot.
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
#include "stash_toml.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/threads.h>

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

using d2r::Unit;
using namespace pd2::stashstack;

// The deposit function's prologue: it spills argument 2 (the depositing player) with `mov [rsp+0x68],rdx`, which DepositorIsLocal reads
sigscan::Entry DepositPrologue {"StashTabDepositPrologue", "stash tabs", sigscan::Kind::Code, 0, 0,
                                "55 53 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24 f8 48 81 ec 08 01 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 45 f8 8b 45 70 "
                                "48 89 54 24 68"};
bool g_prologueOk = false;  // false: the depositor is unknown, so every deposit keeps the old all-or-nothing rule

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
int32_t g_limit = DefaultLimit;  // D2RCore's set_materials_limit, read at install

// The rest of a stack that did not fit (#1816), made again on the game thread after the deposit. shortcut: the local player
// only, as the cube's leftovers (cube_stacks.cpp; the SDK names the player on the UI thread). A remote player's deposit
// stays all-or-nothing (Takes), so a rest is never made for one; upgrade for a hosted game. Known loss window: leaving
// the game before the queued job runs (one game update) loses the rest.
struct Rest {
	uint32_t code, quantity, itemLevel, depositor;  // depositor: the unit id of the player who deposited
};
constexpr int MaxRests = 16;
Rest g_rests[MaxRests];
int g_restCount = 0;
std::mutex g_restLock;
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// Game thread: each rest to the inventory, the ground when it is full
void MakeRests(const D2RL::PluginContext*, void* user) noexcept {
	namespace items = D2RL::Items;
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	Rest rests[MaxRests];
	int count = 0;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		std::memcpy(rests, g_rests, sizeof(Rest) * g_restCount);
		count = g_restCount;
		g_restCount = 0;
	}
	for (int i = 0; i < count; ++i) {
		items::Result made = items::Result::Success;
		for (const auto container : {items::ItemContainer::Inventory, items::ItemContainer::Ground}) {
			const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
			                                  .code = rests[i].code,
			                                  .quality = items::Quality::Normal,
			                                  .qualityRecordId = items::RandomQualityRecord,
			                                  .itemLevel = rests[i].itemLevel,
			                                  .seedMode = items::SeedMode::Random,
			                                  .quantity = rests[i].quantity,
			                                  .durability = items::DefaultValue,
			                                  .stateFlags = items::ItemStateIdentified,
			                                  .destination = {.structSize = items::ItemDestinationSize,
			                                                  .container = container,
			                                                  .placement = items::Placement::Automatic}};
			D2RL::ItemHandle item = D2RL::InvalidItemHandle;
			made = g_items->createItem(g_context, player, &spec, &item);
			if (made == items::Result::Success) {
				break;
			}
		}
		plog::Info("stash tabs: the rest of a stack, %.4s x%u, made again: result %u", reinterpret_cast<const char*>(&rests[i].code),
		           rests[i].quantity, static_cast<uint32_t>(made));
	}
}

// server and client unit ids match, in a local or a hosted game
auto LocalPlayerId() noexcept -> uint32_t {
	using namespace d2r::rva;
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	return local != nullptr ? d2r::At<uint32_t>(local, UnitIdField) : 0xffffffffu;
}

// UI thread: the SDK names the local player only here; a rest whose depositor is another player is dropped
void PickPlayer(const D2RL::PluginContext*, void*) noexcept {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	{
		const uint32_t local = LocalPlayerId();
		std::lock_guard<std::mutex> lock(g_restLock);
		int kept = 0;
		for (int i = 0; i < g_restCount; ++i) {
			if (g_rests[i].depositor == local) {
				g_rests[kept++] = g_rests[i];
			} else {
				plog::Warn("stash tabs: a rest of %u items was for another player; dropped", g_rests[i].quantity);
			}
		}
		g_restCount = kept;
	}
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success ||
	    g_threads->runOnGameThread(g_context, MakeRests, reinterpret_cast<void*>(player)) != D2RL::Threads::Result::Success) {
		plog::Warn("stash tabs: could not queue the rest of a stack; it is lost");
		std::lock_guard<std::mutex> lock(g_restLock);
		g_restCount = 0;
	}
}

void QueueRest(Unit* item, int32_t left, uint32_t depositor) noexcept {
	using namespace d2r::rva;
	bool first = false;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		if (g_restCount >= MaxRests) {
			plog::Warn("stash tabs: too many rests queued; %d items lost", left);
			return;
		}
		first = g_restCount == 0;
		g_rests[g_restCount++] = {d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode)(item), static_cast<uint32_t>(left),
		                          d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemLevel)(item), depositor};
	}
	if (first && g_threads->runOnUiThread(g_context, PickPlayer, nullptr) != D2RL::Threads::Result::Success) {
		plog::Warn("stash tabs: could not queue the rest of a stack; it is lost");
		std::lock_guard<std::mutex> lock(g_restLock);
		g_restCount = 0;
	}
}

auto Quantity(Unit* item) noexcept -> int32_t {
	return item == nullptr ? 0 : d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(item, StatQuantity, 0);
}

auto SlotCount(Unit* slot) noexcept -> uint8_t {
	void* data = slot != nullptr ? d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitItemData)(slot) : nullptr;
	return data != nullptr ? d2r::At<uint8_t>(data, CountByte) : 0;
}

// Whether the deposit is the local player's. The depositing player is the deposit function's own second argument, which its
// prologue spills to [rsp+0x68] (rsp = rbp-0x100, so rbp-0x98; DepositPrologue checks the bytes at install); the item's own
// owner id reads -1 by then. Only the local player's rest can be made again.
auto DepositorIsLocal(const uint8_t* rbp, uint32_t* id) noexcept -> bool {
	*id = 0xffffffffu;
	if (!g_prologueOk) {
		return false;
	}
	Unit* player = rbp != nullptr ? *reinterpret_cast<Unit* const*>(rbp - 0x98) : nullptr;
	*id = player != nullptr ? d2r::At<uint32_t>(player, d2r::rva::UnitIdField) : 0xffffffffu;
	return player != nullptr && *id == LocalPlayerId();
}

auto Count(Unit* slotItem, uint64_t, Unit* rbx, uint8_t* rbp) noexcept -> uint8_t {
	if (reinterpret_cast<uint64_t>(_ReturnAddress()) != g_countCheckReturn) {
		return g_count(slotItem);
	}
	// the deposit check: the slot's own count byte (D2RCore's getter read 0 for a slot of 1 in game)
	const uint8_t count = SlotCount(slotItem);
	// D2R compares the returned byte with a fixed 99 (`cmp al,0x63; jb`), whatever set_materials_limit says, so answer 0
	// while the slot has room under the limit read from the toml and 255 when it has none (#1767); Add takes what fits (#1816)
	uint32_t depositor = 0;
	if (!Takes(count, Quantity(rbx), g_limit, DepositorIsLocal(rbp, &depositor))) {
		plog::Info("stash tabs: refused a stack of %d onto a slot of %u (max %d); depositor %u, local %u", Units(Quantity(rbx)), count, g_limit, depositor, LocalPlayerId());
		g_refused.fetch_add(1);
		return 255;
	}
	return 0;
}

auto Add(void* proxy, uint64_t itemClass, Unit* rbx, uint8_t* rbp) noexcept -> Unit* {
	Unit* slot = g_add(proxy, itemClass);
	if (reinterpret_cast<uint64_t>(_ReturnAddress()) != g_serverAddReturn || slot == nullptr) {
		return slot;
	}
	uint32_t depositor = 0;
	const bool local = DepositorIsLocal(rbp, &depositor);
	const int32_t wanted = Units(Quantity(rbx));
	const uint8_t first = SlotCount(slot);  // the add above is in already
	const int32_t units = wanted - TopUpOf(first > 0 ? first - 1 : 0, wanted, g_limit).left;
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
	plog::Info("stash tabs: deposit of class %d added %d of %d, %d left, slot count %u, depositor %u (%s)", static_cast<int32_t>(itemClass),
	           added, wanted, wanted - added, SlotCount(slot), depositor, local ? "local" : "not local");
	if (units < wanted && local) {
		QueueRest(rbx, wanted - units, depositor);
	}
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
	sigscan::Entry* const own[] = {&CountCheckSite, &ServerAddSite, &SetSendingOnCall, &SendItemCall, &SetSendingOffCall,
	                               &DepositPrologue};
	sigscan::Resolve(own, 6, context->exeBase, nullptr, 0);
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
	// the prologue must sit shortly before both sites, in the one function
	const auto before = [](const sigscan::Entry& site) {
		return DepositPrologue.rva != 0 && site.rva > DepositPrologue.rva && site.rva - DepositPrologue.rva < 0x1000;
	};
	g_prologueOk = before(CountCheckSite) && before(ServerAddSite);
	plog::Info("stash tabs: the deposit prologue check %s", g_prologueOk ? "passed" : "FAILED; deposits stay all-or-nothing");
	g_context = context;
	const auto query = [context](const auto*& service, auto has, uint32_t required) {
		return context->QueryService(&service) == D2RL::ServiceQueryResult::Success && has(service, required);
	};
	if (!query(g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize) ||
	    !query(g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) ||
	    !query(g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize)) {
		std::snprintf(report, reportSize, "stash tabs FAILED (an SDK service is missing)");
		return false;
	}
	int32_t limit = -1;
	const auto source = pd2::stash_toml::Resolve(context->pluginConfigPath, "set_materials_limit", &limit);
	g_limit = LimitOf(limit);
	plog::Info("stash stacks: set_materials_limit = %d from %s", g_limit,
	           source == pd2::stash_toml::Source::Mod ? "the mod's d2rloader.toml" : source == pd2::stash_toml::Source::Game ? "the game-wide d2rloader.toml" : "the default (no live key)");
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
