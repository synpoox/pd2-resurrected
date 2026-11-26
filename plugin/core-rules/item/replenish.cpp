// Replenish charges (IR5).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

namespace itemrules {

// --- Replenish charges (IR5) ---------------------------------------------------------------------------------------
// PD2 (ProjectDiablo.dll 0x10345130, 0x103450b0, 0x103451a0) runs item_replenish_charges on the replenish event (3):
// equipping an item with the stat schedules it 2500 / stat + 1 frames on, and each run adds one charge to the item's
// first charged skill below its max (sent to the owner's client) and schedules the next. No SDK service covers unit
// events or charged skills, so both D2R functions are hooked: the scheduler, which every equip calls, and the handler.
// PD2's handler (0x10345320) runs the durability step, else the quantity step, and only when neither added anything
// the charge step, which schedules the next run whatever is pending (D2R frees the firing event only after its handler
// returns, so a pending check there always sees it). The port matches: D2R's step first, then the charges only when
// durability and quantity are unchanged. No merged item has both stats.
// cut: PD2 halves the delay in PD2's PvP maps, cut.
constexpr uint32_t StatReplenishCharges = 475;  // item_replenish_charges (PD2's 422)
constexpr int32_t ReplenishFrames = 2500;

ScheduleReplenishFn g_scheduleReplenishOriginal = nullptr;
ReplenishEventFn g_replenishEventOriginal = nullptr;
std::atomic<uint32_t> g_chargesReplenished {0};

// The next run, 2500 / stat + 1 frames on; `pending` first looks for a replenish event already queued (the scheduler's
// check; PD2's 0x103450b0)
void ScheduleCharges(Game* game, Unit* item, bool pending) noexcept {
	using namespace d2r::rva;
	const int32_t rate = Stat(item, StatReplenishCharges);
	if (rate <= 0 || (pending && d2r::Engine<int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept>(UnitEventFrame)(
	                                 game, item, UnitEventReplenish, 0) != 0)) {
		return;
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, item, UnitEventReplenish, At<int32_t>(game, GameFrame) + 1 + ReplenishFrames / rate, 0, 0, 0);
}

auto ReplenishSchedule(Game* game, Unit* item) noexcept -> void {
	g_scheduleReplenishOriginal(game, item);
	if (game != nullptr && item != nullptr) {
		ScheduleCharges(game, item, true);
	}
}

// One charge back on the item's first charged skill, as the charge use (0x4368a0) moves it: the list that holds it, the
// value set there, and the stat sent to the owner's client.
// shortcut: PD2 takes the first item_charged_skill of the item's own list (flag 0x40); this takes the first of its full
// stats, the same entry unless a socketed item adds a charged skill. Upgrade if one does.
void AddCharge(Game* game, Unit* item) noexcept {
	using namespace d2r::rva;
	using Entry = pd2::mapstack::Entry;  // 16 bytes under D2RCore (map_stack.h)
	Entry entries[64] {};
	const int32_t count = d2r::Engine<int32_t (*)(Unit*, uint32_t, Entry*, int32_t) noexcept>(UnitStatEntries)(
		item, StatChargedSkill, entries, 64);
	const uint32_t dataFlag = d2r::DataFlag(game);
	if (count <= 0 || DataTables(dataFlag) == nullptr) {
		return;
	}
	const uint32_t layer = static_cast<uint32_t>(entries[0].layer);
	const int32_t skill = static_cast<int32_t>(layer >> LayerShift(dataFlag));
	const int32_t level = static_cast<int32_t>(layer & LayerMask(dataFlag));
	int32_t charges = 0;
	void* list = nullptr;
	if (d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t, int32_t*, void**) noexcept>(ChargedSkillList)(
	        item, skill, level, &charges, &list) == 0 || list == nullptr) {
		return;
	}
	const int32_t value = d2r::Engine<int32_t (*)(uint32_t, void*, uint32_t, uint32_t) noexcept>(ListStatValue)(
		dataFlag, list, StatChargedSkill, layer);
	if ((value & 0xff) >= (value >> 8)) {
		return;
	}
	d2r::Engine<void (*)(uint32_t, void*, uint32_t, int32_t, uint32_t, Unit*) noexcept>(SetListStatOf)(
		dataFlag, list, StatChargedSkill, value + 1, layer, item);
	const uint32_t n = ++g_chargesReplenished;
	const int32_t ownerId = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemOwnerId)(item);
	Unit* owner = ownerId != -1 ? d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(game, 0, ownerId)
	                            : nullptr;
	// The playerbody guard is D2R's durability step's (0x46f4f0); PD2's charge step sends to any player owner
	if (owner == nullptr || d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(owner, StatePlayerBody) != 0) {
		return;
	}
	if (void* client = d2r::Engine<void* (*)(Unit*) noexcept>(PlayerClient)(owner)) {
		d2r::Engine<void (*)(void*, Unit*, int32_t, uint32_t, int32_t, uint32_t) noexcept>(SendUnitStat)(
			client, item, 1, StatChargedSkill, value + 1, layer);
	}
}

auto ChargesReplenishEvent(Game* game, Unit* item, int32_t type) noexcept -> void {
	using namespace d2r::rva;
	const int32_t durability = item != nullptr ? Stat(item, StatDurability) : 0;
	const int32_t quantity = item != nullptr ? Stat(item, StatQuantity) : 0;
	g_replenishEventOriginal(game, item, type);
	if (game == nullptr || item == nullptr || Stat(item, StatReplenishCharges) <= 0 ||
	    Stat(item, StatDurability) != durability || Stat(item, StatQuantity) != quantity) {
		return;
	}
	AddCharge(game, item);
	ScheduleCharges(game, item, false);
}

}  // namespace itemrules
