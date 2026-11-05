// PD2's map use (ProjectDiablo.dll 0x383060; docs/research/pd2-map-system-dll.md, section 1): a right-click on a map
// in Harrogath opens a red portal to the level its misc `len` names. The merge moves PD2's map spell 12 to D2R's free
// item spell 16 and writes `len` in the port's level ids, so this hooks D2R's item spell check and use dispatchers and
// handles spell 16 there; every other spell goes on to D2R.
//
// The gates, in PD2's order: Hell, character level 80+, Rite of Passage done in Hell, standing in Harrogath, and no map
// opened yet in this game. A refused map stays in the inventory and the player gets D2R's "can't do that". The portal
// is D2R's cube "Red Portal" output, which places object 60 by the player and counts the opening per game; its max of 1
// on OpenKey is the one-per-game gate. PD2's uber-area exception and its refusal of levels 152, 153 and 255 belong to
// the uber keys and the Monastery map, which keep a blank pSpell until UB2.
#include "map_open.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t MapSpell = 16;
// The game counter key of map (and, with UB2, uber area) openings: PD2 allows one per game
constexpr uint32_t OpenKey = 0x4d415053;  // 'MAPS'
constexpr int32_t Hell = 2;
constexpr int32_t MinCharLevel = 80;
constexpr int32_t StatLevel = 12;      // itemstatcost level
constexpr int32_t RiteOfPassage = 39;  // the Ancients
constexpr int32_t Harrogath = 109;
constexpr int32_t CantDoThat = 0x15;

using CheckFn = uint64_t (*)(Game* game, Unit* player, Unit* item, Unit* target) noexcept;
using UseFn = void (*)(Game* game, Unit* player, Unit* item, Unit* target) noexcept;

CheckFn g_checkOriginal = nullptr;
UseFn g_useOriginal = nullptr;
std::atomic<uint32_t> g_checks {0};
std::atomic<uint32_t> g_refused {0};
std::atomic<uint32_t> g_opened {0};
std::atomic<uint32_t> g_failed {0};
// Set once the check hook is in: until then the use hook leaves maps to D2R, so no portal opens ungated
std::atomic<bool> g_gated {false};

auto IsMap(Game* game, Unit* item) noexcept -> const uint8_t* {
	using namespace d2r::rva;
	const int32_t row = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitClassOf)(item, __FILE__,
	                                                                                               __LINE__);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord)(d2r::DataFlag(game), row);
	return record != nullptr && At<int32_t>(record, ItemPSpell) == MapSpell ? record : nullptr;
}

auto MapLevel(Game* game, Unit* player, Unit* item, const uint8_t* record) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(uint32_t, Unit*, Unit*, uint32_t) noexcept>(d2r::rva::ItemCalc)(
		d2r::DataFlag(game), player, item, At<uint32_t>(record, d2r::rva::ItemLenCalc));
}

auto Openings(Game* game) noexcept -> int32_t {
	uint32_t key = OpenKey;
	const int32_t* counter = d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
	return counter != nullptr ? *counter : 0;
}

auto RiteDone(Unit* player) noexcept -> bool {
	using namespace d2r::rva;
	const uint8_t* data = d2r::Engine<const uint8_t* (*)(Unit*) noexcept>(UnitPlayerData)(player);
	void* quests = data != nullptr ? At<void*>(data, PlayerQuests + Hell * 8) : nullptr;
	return quests != nullptr && d2r::Engine<int32_t (*)(void*, int32_t, int32_t) noexcept>(QuestFlag)(
	                                quests, RiteOfPassage, 0) != 0;
}

auto PlayerLevelId(Unit* player) noexcept -> int32_t {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : -1;
}

// True, with the reason in `why`, when PD2 would refuse this map now.
auto Refusal(Game* game, Unit* player, int32_t level, char* why, size_t size) noexcept -> bool {
	const int32_t charLevel = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		player, StatLevel, 0);
	const int32_t where = PlayerLevelId(player);
	if (level <= 0 || level > 255) {
		std::snprintf(why, size, "no area (len %d)", level);
	} else if (At<uint8_t>(game, d2r::rva::GameDifficulty) != Hell) {
		std::snprintf(why, size, "not Hell");
	} else if (charLevel < MinCharLevel) {
		std::snprintf(why, size, "character level %d under %d", charLevel, MinCharLevel);
	} else if (!RiteDone(player)) {
		std::snprintf(why, size, "Rite of Passage not done in Hell");
	} else if (where != Harrogath) {
		std::snprintf(why, size, "in level %d, not Harrogath", where);
	} else if (Openings(game) > 0) {
		std::snprintf(why, size, "a map or uber area is already open in this game");
	} else {
		return false;
	}
	return true;
}

auto SpellCheck(Game* game, Unit* player, Unit* item, Unit* target) noexcept -> uint64_t {
	const uint8_t* record = game != nullptr && player != nullptr && item != nullptr ? IsMap(game, item) : nullptr;
	if (record == nullptr) {
		return g_checkOriginal(game, player, item, target);
	}
	++g_checks;
	const int32_t level = MapLevel(game, player, item, record);
	char why[96];
	if (Refusal(game, player, level, why, sizeof(why))) {
		plog::Info("map: level %d refused: %s (%u refused)", level, why, ++g_refused);
		d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(d2r::rva::ClientMessage)(player, CantDoThat, player);
		return 0;
	}
	return 1;
}

// shortcut: the check passed, so the item is used up even if D2R finds no free spot for the portal (PD2 too places
// it unchecked); add a pre-check of the spot if a game check ever loses a map that way.
auto SpellUse(Game* game, Unit* player, Unit* item, Unit* target) noexcept -> void {
	const uint8_t* record = g_gated && game != nullptr && player != nullptr && item != nullptr ? IsMap(game, item)
	                                                                                         : nullptr;
	if (record == nullptr) {
		g_useOriginal(game, player, item, target);
		return;
	}
	const int32_t level = MapLevel(game, player, item, record);
	plog::Info("map: opening level %d", level);
	const int32_t opened = d2r::Engine<int32_t (*)(Game*, Unit*, uint32_t, int32_t, int32_t) noexcept>(
		d2r::rva::OpenRedPortal)(game, player, OpenKey, level, 1);
	if (opened != 0) {
		plog::Info("map: red portal to level %d, %u opened", level, ++g_opened);
	} else {
		plog::Warn("map: no red portal to level %d (D2R refused), %u failed", level, ++g_failed);
	}
}

}  // namespace

auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	// The use first: with only the check, D2R's use would take a map that passed and open nothing
	const bool use = context->InstallInlineHook(ItemSpellUse, ItemSpellUseEntry, sizeof(ItemSpellUseEntry), SpellUse,
	                                            &g_useOriginal);
	const bool check = use && context->InstallInlineHook(ItemSpellCheck, ItemSpellCheckEntry,
	                                                     sizeof(ItemSpellCheckEntry), SpellCheck, &g_checkOriginal);
	g_gated = check;
	std::snprintf(report, reportSize, "item spell %d%s", MapSpell,
	              check ? "" : use ? " FAILED (check bytes differ)" : " FAILED (use bytes differ)");
	return check ? 1 : 0;
}

auto MapOpenCount() noexcept -> int {
	return 1;
}

auto MapOpenCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "map checks %u, refused %u, opened %u, failed %u", g_checks.load(), g_refused.load(),
	              g_opened.load(), g_failed.load());
}
