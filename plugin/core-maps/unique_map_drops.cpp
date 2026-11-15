// The unique maps' drop split out of key_drops.cpp (#943), unchanged: the table of the eight unique maps, the map-area
// roll (obsolete with PD2's maps) and the drop Horazon's 10% also calls.
#include "unique_map_drops.h"

#include "log.h"
#include "tc_roll.h"
#include "unique_maps.h"
#include "uber_drops.h"

#include <D2RLPlugin/item.h>

#include <atomic>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t FirstMapLevel = 138, LastMapLevel = 201;  // PD2's map-area test 0x1037c510, port ids

// PD2's 0x10381510: the eight unique maps (rand & 7), each a unique at item level 131. Code and runtime uniqueitems row.
struct UniqueMap {
	uint32_t code;
	int32_t row;
};
constexpr UniqueMap UniqueMaps[8] = {
	{D2RL::Items::MakeItemCode("t51"), 469}, {D2RL::Items::MakeItemCode("t52"), 470},  // Zhar the Mad, Warlord of Blood
	{D2RL::Items::MakeItemCode("t53"), 473}, {D2RL::Items::MakeItemCode("t55"), 490},  // Fallen Gardens, Outer Void
	{D2RL::Items::MakeItemCode("t54"), 489}, {D2RL::Items::MakeItemCode("t56"), 509},  // Imperial Palace, Ureh City
	{D2RL::Items::MakeItemCode("t57"), 515}, {D2RL::Items::MakeItemCode("t58"), 519},  // Djinn's Domain, Na-Krul's Abyss
};
constexpr int32_t UniqueMapLevel = 131;
// PD2's boss sets 0x105deb54, 0x105dea4c, 0x105debcc and 0x105debdc (0x103760a0 +0x20/+0x28/+0x30/+0x38), port ids:
// these roll "Unique Map MapBoss" (TC 1021) instead of "Unique Map Monsters" (1020)
constexpr int32_t UniqueMapBosses[] = {
	247,  248,  249,  763,  764,  767,  817,  841,  911,  932,  940,  941,  943,  944,  987,  988,
	989,  990,  1013, 1014, 1017, 1042, 1043, 1120, 1181, 1183, 1191, 1192, 1257, 1259, 1260,
};
auto IsIn(int32_t id, const int32_t* ids, size_t n) noexcept -> bool {
	for (size_t i = 0; i < n; ++i) {
		if (ids[i] == id) {
			return true;
		}
	}
	return false;
}
std::atomic<uint32_t> g_uniqueMapRolls {0};
std::atomic<uint32_t> g_uniqueMaps {0};

}  // namespace

// PD2 0x103799d0's unique map roll: in a map area, 1 in N of TC 1020 (1021 for a map boss), N divided by `mult`
auto RollUniqueMap(Game* game, Unit* monster, int32_t level, int32_t players, int64_t mult, uint32_t* seed) noexcept -> void {
	if (level < FirstMapLevel || level > LastMapLevel) {
		return;
	}
	const int32_t classId = At<int32_t>(monster, d2r::rva::UnitClassId);
	const bool boss = IsIn(classId, UniqueMapBosses, sizeof(UniqueMapBosses) / sizeof(UniqueMapBosses[0]));
	const char* tc = boss ? "Unique Map MapBoss" : "Unique Map Monsters";
	const int64_t odds = TcOdds(static_cast<uint8_t>(d2r::DataFlag(game)), tc, players);
	if (plog::Sample(g_uniqueMapRolls)) {
		plog::Info("unique maps: monster %d in level %d rolls TC \"%s\" 1 in %lld x%lld (%u rolls)", classId, level, tc, odds,
		           mult, g_uniqueMapRolls.load());
	}
	if (odds > 0 && odds <= 0xffffffffLL && d2r::RandomBelow(seed, static_cast<uint32_t>(odds)) < mult) {
		DropUniqueMap(game, monster);
	}
}

auto DropUniqueMap(Game* game, Unit* monster) noexcept -> void {
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(monster);
	const UniqueMap& u = UniqueMaps[d2r::RandomBelow(seed, 8)];
	// PD2's 0x1037ff30(game, unique, 0): clear the unique's dropped bit, so a second one in this game drops unique too
	reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(game) + d2r::rva::GameUniquesDropped)[u.row >> 5] &=
		~(1u << (u.row & 31));
	if (DropUniqueAt(game, monster, u.code, u.row, UniqueMapLevel)) {
		plog::Info("unique maps: %.4s dropped (%u so far)", reinterpret_cast<const char*>(&u.code), ++g_uniqueMaps);
	}
}

auto UniqueMapsDropped() noexcept -> uint32_t {
	return g_uniqueMaps.load();
}

auto UniqueMapRolls() noexcept -> uint32_t {
	return g_uniqueMapRolls.load();
}
