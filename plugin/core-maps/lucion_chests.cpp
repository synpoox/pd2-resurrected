// The Temple of the Triune's chests (ProjectDiablo.dll 0x103a94b0, read for #438). PD2 wraps the chest operate
// (OperateFn 4): for its LucionChest (PD2 object 623, the port's 626) in mode 0 it plays the chest sound, sets mode 1,
// drops LucionChestLow (at most 3 items) and LucionChestHigh (at most 2 + tier: 2, 3 or 4) at the item level of the
// player's area, and opens on event 1 after the open frames. D2R's own chest roll never runs for it. Any other chest
// goes to the original.
//
// No PluginSDK service reports or changes an object's operate, so this wraps the operate table slot (as map_open.cpp
// takes slot 60). The sound, the item level and the TC drop are engine calls.
// shortcut: PD2 also calls a D2Common function on the player first (0x10309650, ordinal 10331, not identified) and
// clears bit 2 of the chest's 1.13c unit flags; D2R's chest is unselectable from mode 1 on anyway. Port both if the
// opened chest ever acts differently from PD2's.
#include "lucion_chests.h"

#include "pd2_engine.h"
#include "log.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t LucionChest = 626;      // PD2 623
constexpr uint32_t TierKey = 0x55425254;  // 'UBRT', as map_open.cpp
constexpr int LowMax = 3;

// The argument block D2R's operate dispatcher 0x592c40 passes an operate function (as map_open.cpp)
struct ObjectOperate {
	Game* game;
	Unit* object;
	Unit* player;
	uint64_t seed;
	int32_t classId;
};
using OperateFn = int32_t (*)(ObjectOperate*) noexcept;

// PD2: the High TC's cap is 3 at tier 1, 4 at tier 2, else 2
constexpr auto HighMax(int32_t tier) noexcept -> int {
	return tier == 1 ? 3 : tier == 2 ? 4 : 2;
}
static_assert(HighMax(0) == 2 && HighMax(1) == 3 && HighMax(2) == 4 && HighMax(3) == 2);

OperateFn g_original = nullptr;
std::atomic<uint32_t> g_opened {0};
std::atomic<uint32_t> g_items {0};

// One TC drop by the chest; `max` caps its items, as PD2 passes it (the drop resets the count each call)
auto Drop(Game* game, Unit* chest, Unit* player, const char* name, int32_t ilvl, int32_t max) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
	const int32_t index = tables != nullptr ? d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
												  At<void*>(tables, TablesTcNames), name, 0)
	                                        : 0;
	const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
										dataFlag, static_cast<uint16_t>(index), 0)
	                              : nullptr;
	if (tc == nullptr) {
		plog::Warn("temple: no TC \"%s\"", name);
		return 0;
	}
	Unit* items[6] = {};
	int32_t count = 0;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, Unit**, int32_t*, int32_t) noexcept>(
		TcDrop)(game, chest, player, tc, 0, ilvl, 0, items, &count, max);
	return count;
}

auto OperateTempleChest(ObjectOperate* args) noexcept -> int32_t {
	using namespace d2r::rva;
	Game* game = args->game;
	Unit* chest = args->object;
	Unit* player = args->player;
	if (game == nullptr || chest == nullptr || player == nullptr || args->classId != LucionChest) {
		return g_original(args);
	}
	if (At<int32_t>(chest, UnitMode) != 0) {
		return 1;
	}
	d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(ClientMessage)(player, SoundChestOpen, player);
	if (void* client = d2r::Engine<void* (*)(Unit*) noexcept>(PlayerClient)(player); client != nullptr) {
		d2r::Engine<void (*)(Unit*, void*) noexcept>(SoundPacket)(player, client);
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(chest, 1);
	// PD2's item level is 1.13c's for a new item by the player: its area's monster level, as D2R's chest drop takes it
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	const int32_t level = room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : 0;
	const auto ilvl = static_cast<int32_t>(d2r::Engine<uint64_t (*)(Game*, int32_t, uint8_t, bool) noexcept>(
		AreaMonsterLevelFn)(game, level, At<uint8_t>(game, GameDifficulty), At<uint8_t>(game, GameDataFlag) != 1));
	uint32_t key = TierKey;
	const int32_t tier = *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(GameCounter)(
		reinterpret_cast<uint8_t*>(game) + GameCounters, &key);
	const int32_t low = Drop(game, chest, player, "LucionChestLow", ilvl, LowMax);
	const int32_t high = Drop(game, chest, player, "LucionChestHigh", ilvl, HighMax(tier));
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(d2r::DataFlag(game),
	                                                                                                LucionChest);
	if (record != nullptr) {
		const int32_t open = At<int32_t>(game, GameFrame) + 1 + static_cast<int32_t>(At<uint32_t>(record, ObjOpenFrames) >> 8);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
			game, chest, UnitEventOpen, open, 0, 0, 0);
	}
	g_items.fetch_add(static_cast<uint32_t>(low + high), std::memory_order_relaxed);
	plog::Info("temple: chest opened at tier %d, difficulty %d, item level %d: %d of %d low, %d of %d high (%u so far)",
	           tier, At<uint8_t>(game, GameDifficulty), ilvl, low, LowMax, high, HighMax(tier),
	           g_opened.fetch_add(1, std::memory_order_relaxed) + 1);
	return 1;
}

}  // namespace

auto InstallLucionChests(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	g_original = reinterpret_cast<OperateFn>(d2r::imageBase + OperateChest);
	const uint64_t expected = d2r::imageBase + OperateChest;
	const bool on = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperateChest) * 8, &expected,
	                                       sizeof(expected), reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperateTempleChest)));
	std::snprintf(report, reportSize, "chest operate %d%s", ObjectOperateChest, on ? "" : " FAILED (slot differs)");
	return on ? 1 : 0;
}

auto LucionChestsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "temple: %u chests opened, %u items", g_opened.load(), g_items.load());
}
