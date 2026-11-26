// Charm rows: a charm works only in the inventory's lower four rows (#358, #1358).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

// --- Charm rows (#358) ---------------------------------------------------------------------------------------------
// PD2's inventory is 10x8 and a charm works only in its lower four rows (the grid's y 4 to 7, by the charm's top row).
// D2R's own charm-active body (93847: 0x36ae00) is not called: D2RLoader rewrote its callers to a thunk in its .maho
// section whose `jmp [slot]` reaches D2RCore's charm policy (stock inventory, custom page or both). No SDK service
// covers a cell range in the stock inventory, so the slot is swapped for a wrapper (as for crushing blow), found
// from D2Game's add-to-inventory call site, never by a fixed address.
sigscan::Entry CharmCallSite {"ItemCharmActiveCall", "charms", sigscan::Kind::Code, 0, 0,
                              "48 8b cb e8 ?? ?? ?? ?? 83 f8 01 74 ?? 48 8b cb e8 ?? ?? ?? ?? 85 c0 75 ?? 48 8b d7 48 8b cb e8 ?? ?? ?? ?? 85 c0 74 ?? 0f b6 8e 06 01 00 00 41 b9 01 00 00 00"};
constexpr uint32_t CharmCallOffset = 31;  // the E8 of the charm check in that signature
constexpr uint32_t CharmFirstActiveRow = d2r::CharmFirstRow;  // zero-based: rows 5 to 8

using CharmActiveFn = int32_t (*)(Unit* item, Unit* player) noexcept;
CharmActiveFn g_charmActiveOriginal = nullptr;
std::atomic<uint32_t> g_charmChecks {0};
std::atomic<uint32_t> g_charmOffChecks {0};

// A unique charm of the same name in the active rows too (#1288): D2R's (item) -> unique row + 1, the cube executor's read.
constexpr uint64_t ItemUniqueRow = 0x36ce70;
constexpr int32_t QualityUnique = 7;

// Stats follow the policy only when the engine asks it: link (0x475140 -> StatListLink 0x2f81a0) happens on a yes from the add
// path 0x475090, identify 0x46e8c0 / 0x46ea70 and the refresh 0x470ca0, which asks about every stored grid item, linked or not,
// but only links (never unlinks). An item leaving the grid is unlinked by 0x46f710 inside the move (0x4747f0), then the move
// runs the refresh (0x471e90 / 0x472590). So: on any ask about a unique charm with a twin, answer no and unlink every linked
// copy (the item too) with StatListUnlink 0x2f8290; when a copy leaves, the refresh asks about the other one, finds no twin and
// links it. No return address is needed: the rule is the same whoever asks.
constexpr uint64_t ItemStatsAttached = 0x2f8120;  // (item, int* out) -> the item's stat list's owner, 0 when not linked
constexpr uint64_t StatListUnlink = 0x2f8290;     // (owner, item), toolkit stats.h
constexpr uint32_t CharmTwinLogCap = 200;
std::atomic<uint32_t> g_charmTwinLogs {0};

// Unlinks item if its stats are on; true when it did.
auto UnlinkIfAttached(Unit* item, Unit* player) noexcept -> bool {
	if (d2r::Engine<uint64_t (*)(Unit*, int32_t*) noexcept>(ItemStatsAttached)(item, nullptr) == 0) {
		return false;
	}
	d2r::Engine<void (*)(Unit*, Unit*) noexcept>(StatListUnlink)(player, item);
	return true;
}

// Same-name unique charms stored (mode 0, so never the cursor) with their top row in rows 5-8, item excluded; each linked one is
// unlinked (counted in unlinked; pass nullptr to only count, as the draw path must). Returns how many there are, 0 when item is not in the player's item list: the client's remove
// handler 0x1c60a0 asks about the leaving copy after 0x389820 took it out (mode and cell unchanged) and unlinks it and refreshes
// 0x1c9610 only on a yes, so a leaving copy gets D2RCore's answer. A twin D2RCore refuses (unidentified, requirements) still counts.
auto CountTwins(Unit* item, Unit* player, uint32_t* unlinked) noexcept -> uint32_t {
	using namespace d2r::rva;
	if (player == nullptr || d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item) != QualityUnique) {
		return 0;
	}
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return 0;
	}
	const auto uniqueRow = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemUniqueRow);
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	const int32_t mine = uniqueRow(item);
	Unit* const first = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory);
	Unit* listed = first;
	while (listed != nullptr && listed != item) {
		listed = next(listed);
	}
	if (listed == nullptr) {
		return 0;
	}
	uint32_t found = 0;
	for (Unit* other = first; other != nullptr; other = next(other)) {
		if (other != item && At<int32_t>(other, UnitType) == 4 &&
		    d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitModeOf)(other) == 0 &&
		    d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(other) == 0 &&
		    (d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitPackedXY)(other) >> 16) >= CharmFirstActiveRow &&
		    d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(other) == QualityUnique && uniqueRow(other) == mine) {
			++found;
			if (unlinked != nullptr) {
				*unlinked += UnlinkIfAttached(other, player) ? 1 : 0;
			}
		}
	}
	return found;
}

// True when D2R counts the charm as active but it sits in a top grid row: PD2 switches it off.
auto CharmSwitchedOff(Unit* item, Unit* player, uint32_t& xy, uint8_t& page) noexcept -> bool {
	if (item == nullptr || g_charmActiveOriginal(item, player) == 0) {
		return false;
	}
	page = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::rva::ItemPage)(item);
	xy = d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::UnitPackedXY)(item);
	return page == 0 && (xy >> 16) < CharmFirstActiveRow;  // a custom page is never judged by its grid row
}

// True for a same-name unique charm stored in rows 5-8 with a twin there (#1288): both go inactive. Only counts unless
// `unlinked` is given, so the draw path (#1377) shares it without changing state.
auto CharmTwinOff(Unit* item, Unit* player, uint32_t xy, uint8_t page, uint32_t* unlinked) noexcept -> bool {
	return page == 0 && (xy >> 16) >= CharmFirstActiveRow &&
	       d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::UnitModeOf)(item) == 0 && CountTwins(item, player, unlinked) != 0;
}

auto CharmActiveInRow(Unit* item, Unit* player) noexcept -> int32_t {
	const int32_t active = g_charmActiveOriginal(item, player);
	if (active == 0 || item == nullptr) {
		return active;
	}
	uint32_t xy = 0;
	uint8_t page = 0;
	// a same-name unique in the active rows switches both off (#1288); a cursor-held copy (mode 4) never counts
	uint32_t unlinked = 0;
	bool off = CharmSwitchedOff(item, player, xy, page);
	const uint32_t row = xy >> 16;
	const bool twin = !off && CharmTwinOff(item, player, xy, page, &unlinked);
	if (twin) {
		unlinked += UnlinkIfAttached(item, player) ? 1 : 0;
		off = true;
	}
	++(off ? g_charmOffChecks : g_charmChecks);
	if (twin) {
		++g_charmTwinLogs;
	}
	return off ? 0 : active;
}

// The inventory widget's draw (93847: 0x2c3570) asks D2RCore's "requirements met for display" check for each stored
// item (a .maho thunk, its slot swapped like the charm policy's) and draws the item's cell in D2R's red unusable tint
// when it says no. A top-row charm gives no stats, so PD2 tints it red like any inactive item (#1358): the check says
// no for it. The draw is that call's only caller, so no rule sees the answer. The arguments are the call site's
// (item, player, four zero words, the last a dword on the stack), passed on as given.
sigscan::Entry ReqDisplaySite {"ItemReqDisplayCall", "charms", sigscan::Kind::Code, 0, 0,
                               "33 c0 45 33 c9 89 44 24 30 45 33 c0 48 89 44 24 28 49 8b d4 48 8b cf 48 89 44 24 20 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 84 db 0f 85"};
constexpr uint32_t ReqDisplayCallOffset = 28;  // the E8 of the check in that signature

using ReqDisplayFn = int32_t (*)(Unit* item, Unit* player, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) noexcept;
ReqDisplayFn g_reqDisplayOriginal = nullptr;

auto ReqDisplayInRow(Unit* item, Unit* player, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) noexcept -> int32_t {
	const int32_t ok = g_reqDisplayOriginal(item, player, a3, a4, a5, a6, a7);
	uint32_t xy = 0;
	uint8_t page = 0;
	// a same-name twin in rows 5-8 is switched off too (#1377); counted only, the draw never unlinks
	return ok != 0 && (CharmSwitchedOff(item, player, xy, page) || (g_charmActiveOriginal(item, player) != 0 && CharmTwinOff(item, player, xy, page, nullptr))) ? 0 : ok;
}


auto InstallCharmRows(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Entry* const own[] = {&CharmCallSite, &ReqDisplaySite};
	sigscan::Resolve(own, 2, context->exeBase, nullptr, 0);
	if (!SwapThunkSlot(context, CharmCallSite, CharmCallOffset,
	                   reinterpret_cast<uint64_t>(static_cast<CharmActiveFn>(CharmActiveInRow)),
	                   reinterpret_cast<uint64_t*>(&g_charmActiveOriginal))) {
		return false;
	}
	// The tint is cosmetic and the rows work without it, so a miss is only logged.
	if (!SwapThunkSlot(context, ReqDisplaySite, ReqDisplayCallOffset,
	                   reinterpret_cast<uint64_t>(static_cast<ReqDisplayFn>(ReqDisplayInRow)),
	                   reinterpret_cast<uint64_t*>(&g_reqDisplayOriginal))) {
		plog::Warn("charm rows: the red tint hook was not installed (signature or bytes differ)");
	}
	return true;
}
