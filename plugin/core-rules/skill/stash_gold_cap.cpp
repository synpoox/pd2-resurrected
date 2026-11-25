// Stash gold as in PD2 (#2118): the personal stash and the whole shared stash each hold up to 25,000,000. The inventory
// cap stays 5,000,000 (gold_cap.cpp, #2027).
//
// PD2 sends deposit and withdraw (1.13c D2Game +0xd14a2) to its own copy 0x103999f0, whose deposit clamps stat 15
// (goldbank) at 25,000,000. D2R's bank cap is 93847 0x34b310 (`mov eax, 2500000; ret`), asked per stash unit: by deposit
// (0x4ba580), by the save load (PlrSave 0x420280/0x5311f0, PlrSave2 0x52fdb0) and by the client's deposit limit. Its
// immediate becomes 25,000,000, and so does the one inline copy, the client's shared stash save write in D2Client
// SCmd.cpp 0x133610 (`cmp eax, 0x2625a0; jg`: a page over it skips the whole file write). The write check goes first, so
// the game never holds gold it then cannot save.
//
// D2R keeps each shared page as its own player unit (stat 0xba), listed on the owner's inventory at +0x68 (node: unit id
// +0, next +0x10; 0x385860 appends, 0x389910 removes), so 0x34b310 alone caps each page. The pool: the server's gold
// message handler (0x4ba580, PlrMsg table slot) is wrapped. A deposit (size 0x11, signed amount at +0xd < 0) into one of
// the sender's own shared pages (+1) is cut to 25,000,000 less the gold on all their shared pages, and dropped when no
// room is left. The load guard stays per page, so no save that already holds more fails to load.
#include "common.h"
#include "registry.h"

#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t Stock[] = {0xa0, 0x25, 0x26, 0x00};  // 2,500,000
constexpr uint8_t Pd2[] = {0x40, 0x78, 0x7d, 0x01};    // 25,000,000
constexpr int64_t Pool = 25'000'000;

using HandlerFn = int64_t (*)(void* game, void* player, const uint8_t* message, int32_t size) noexcept;
using FindUnitFn = d2r::Unit* (*)(d2r::Game*, int32_t type, uint32_t id) noexcept;
using InventoryFn = const uint8_t* (*)(void*, const char*, int) noexcept;
HandlerFn g_gold = nullptr;

// Gold on all of `player`'s shared pages, or -1 when `page` is not one of them.
auto SharedGold(void* game, void* player, uint32_t page) noexcept -> int64_t {
	const uint8_t* inventory = d2r::Engine<InventoryFn>(d2r::sig::UnitInventory)(player, __FILE__, __LINE__);
	if (inventory == nullptr || d2r::At<uint32_t>(inventory, 0) != 0x1020304) {
		return -1;
	}
	int64_t sum = 0;
	bool mine = false;
	int nodes = 0;
	for (const uint8_t* node = d2r::At<const uint8_t*>(inventory, 0x68); node != nullptr && nodes < 256;
	     node = d2r::At<const uint8_t*>(node, 0x10), ++nodes) {
		const uint32_t id = d2r::At<uint32_t>(node, 0);
		mine = mine || id == page;
		d2r::Unit* unit = d2r::Engine<FindUnitFn>(d2r::sig::FindUnit)(static_cast<d2r::Game*>(game), 0, id);
		sum += unit != nullptr ? d2r::api.unitStat(unit, 15, 0) : 0;
	}
	return mine ? sum : -1;
}

auto GoldMessage(void* game, void* player, const uint8_t* message, int32_t size) noexcept -> int64_t {
	if (size != 0x11 || message == nullptr || player == nullptr) {
		return g_gold(game, player, message, size);
	}
	uint32_t page = 0;
	int32_t amount = 0;
	std::memcpy(&page, message + 1, 4);
	std::memcpy(&amount, message + 0xd, 4);
	if (amount >= 0 || amount == INT32_MIN || page == d2r::At<uint32_t>(player, 0x08)) {
		return g_gold(game, player, message, size);  // a withdraw, or the personal stash
	}
	const int64_t held = SharedGold(game, player, page);
	const int64_t room = held < 0 ? -amount : Pool - held;
	if (-static_cast<int64_t>(amount) <= room) {
		return g_gold(game, player, message, size);
	}
	if (room <= 0) {
		return 0;  // the shared stash is full: nothing moves, the client keeps the server's values
	}
	uint8_t cut[0x11];
	std::memcpy(cut, message, sizeof(cut));
	const int32_t less = -static_cast<int32_t>(room);
	std::memcpy(cut + 0xd, &less, 4);
	return g_gold(game, player, cut, size);
}

// The PlrMsg handler table slot that holds the gold handler: false (nothing changed) when any check fails.
auto SwapGoldSlot(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t handler = 0, dispatch = 0, list = 0;
	// 0x4ba580's entry through `cmp ebx, 0x11; je` (the message size check)
	if (sigscan::ScanUnique(context->exeBase,
	                        "40 55 53 56 57 41 57 48 8d 6c 24 c9 48 81 ec 00 01 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 45 0f "
	                        "49 63 d9 49 8b f8 48 89 4d af 48 89 55 9f 83 fb 11 74 12",
	                        0, &handler) != 1 ||
	    // the dispatch 0x4f2fa0: lea rdx,[image]; lea r14,[rdx+table]; cmp qword [r14+rsi*8],0
	    sigscan::ScanUnique(context->exeBase, "48 8d 15 ?? ?? ?? ?? 4c 8d b2 ?? ?? ?? ?? 49 83 3c f6 00 4d 8d 34 f6", 0, &dispatch) != 1 ||
	    // the page list's layout, from 0x389910: head [rcx+0x68], `cmp [rbx],edx` (id +0), next [rbx+0x10]
	    sigscan::ScanUnique(context->exeBase,
	                        "48 8b 41 68 48 8b d8 4c 8b c0 48 85 c0 74 10 39 13 74 14 4c 8b c3 48 8b 5b 10 48 85 db 75 f0", 0,
	                        &list) != 1 ||
	    d2r::sig::FindUnit.state != sigscan::State::Ok || d2r::sig::UnitInventory.state != sigscan::State::Ok ||
	    d2r::api.unitStat == nullptr) {
		return false;
	}
	int32_t table = 0;
	std::memcpy(&table, reinterpret_cast<const void*>(context->exeBase + dispatch + 10), 4);
	const uint64_t stock = context->exeBase + handler;
	uint64_t slot = 0;
	for (uint32_t id = 0; id <= 0x66; ++id) {  // the dispatch refuses ids past 0x66
		if (d2r::At<uint64_t>(reinterpret_cast<const void*>(context->exeBase + table), id * 8) == stock) {
			if (slot != 0) {
				return false;
			}
			slot = static_cast<uint64_t>(table) + id * 8;
		}
	}
	if (slot == 0) {
		return false;
	}
	g_gold = reinterpret_cast<HandlerFn>(stock);  // before the slot can send a message here
	const uint64_t wrapper = reinterpret_cast<uint64_t>(&GoldMessage);
	return context->PatchBytes(slot, reinterpret_cast<const uint8_t*>(&stock), 8, reinterpret_cast<const uint8_t*>(&wrapper), 8);
}

}  // namespace

auto InstallStashGoldCap(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t cap = 0;
	uint64_t page = 0;
	// int3 pad; mov eax, 2500000; ret; int3 -> the imm32 at +2
	const bool found = sigscan::ScanUnique(context->exeBase, "cc b8 a0 25 26 00 c3 cc", 2, &cap) == 1 &&
	                   // shr rax,0x20; cmp eax,0x7fffffff; ja; cmp eax,2500000; jg -> the imm32 at +0x10
	                   sigscan::ScanUnique(context->exeBase, "48 c1 e8 20 3d ff ff ff 7f 0f 87 ?? ?? ?? ?? 3d a0 25 26 00 0f 8f",
	                                       0x10, &page) == 1;
	const bool on = found && context->PatchBytes(page, Stock, sizeof(Stock), Pd2, sizeof(Pd2)) &&
	                context->PatchBytes(cap, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	// the pool only on top of the raised cap: alone it would cut deposits D2R then refuses at 2,500,000 anyway
	const bool pool = on && SwapGoldSlot(context);
	plog::Info("stash-gold-cap: pd2 25000000 %s, shared pool %s", on ? "on" : "FAILED", pool ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; stash-gold-cap: pd2 25000000 %s, shared pool %s", on ? "on" : "FAILED",
		              pool ? "on" : "FAILED");
	}
	return on && pool;
}

PD2R_RULE(InstallStashGoldCap, 310)
