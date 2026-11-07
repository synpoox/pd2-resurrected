// PD2's monster and player map stats (ProjectDiablo.dll; docs/research/pd2-map-system-dll.md, section 2, and
// docs/research/ms3-map-stats.md). When a map opens, PD2 routes each of its stats by the stat's itemstatcost `Divide`
// column: 1-999 to the monster list as that stat, 1000-1999 to the player list as `Divide - 1000`, 2000-2999 to the
// area (MS4), 3000 to phys-as-extra, 0 nowhere, and the default 1024 to the monster list as itself. D2R has no
// `Divide` column, so Routes below carries PD2's values in the port's stat ids.
//
// A monster that spawns in a map area gets the monster list, and a player standing in one the player list, as a stat
// list of state `map` (PD2 198). A player leaving the map areas loses it. The lists of an open map are kept in D2R's
// per-game counters (rva::GameCounter), so a new game starts without them.
#include "map_stats.h"

#include "map_open.h"

#include "log.h"
#include "uber_scaling.h"

#include <intrin.h>

#include <atomic>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

enum class Route : uint8_t { Monster, Player, Area, Elsewhere, PhysAsExtra };

struct Routed {
	int16_t stat;  // the map stat (port id)
	Route route;
	int16_t target;  // the stat it becomes (port id) for Monster and Player; the area slot (`Divide - 2000`) for Area
};

// Every PD2 stat whose `Divide` is not the default 1024, by port id (merge/ids-itemstatcost.tsv). Area: `map_glob_*`,
// `treacherous` and `phasing`; Elsewhere: `map_force_event` (MS8).
constexpr Routed Routes[] = {
	{371, Route::Area, 7},          {372, Route::Area, 8},          {387, Route::Area, 9},
	{388, Route::Area, 9},          {398, Route::Area, 11},         {399, Route::Area, 12},
	{400, Route::Area, 13},         {401, Route::Area, 15},         {402, Route::Area, 14},
	{403, Route::Monster, 119},     {404, Route::Area, 14},         {423, Route::Player, 80},
	{424, Route::Player, 79},       {425, Route::Area, 0},          {426, Route::Player, 85},
	{427, Route::Area, 1},          {428, Route::Area, 2},          {429, Route::Monster, 48},
	{430, Route::Monster, 49},      {431, Route::Monster, 50},      {432, Route::Monster, 51},
	{433, Route::Monster, 52},      {434, Route::Monster, 53},      {435, Route::Monster, 54},
	{436, Route::Monster, 55},      {437, Route::Monster, 56},      {438, Route::Monster, 57},
	{439, Route::Monster, 58},      {440, Route::Monster, 59},      {441, Route::Monster, 329},
	{442, Route::Monster, 330},     {443, Route::Monster, 331},     {444, Route::Monster, 332},
	{445, Route::Monster, 93},      {446, Route::Monster, 105},     {447, Route::Monster, 119},
	{448, Route::Monster, 16},      {449, Route::Monster, 148},     {450, Route::Monster, 146},
	{451, Route::Monster, 144},     {452, Route::Monster, 142},     {453, Route::Monster, 34},
	{454, Route::Monster, 67},      {455, Route::Monster, 74},      {456, Route::Monster, 60},
	{457, Route::Monster, 99},      {458, Route::Monster, 76},      {459, Route::Monster, 156},
	{460, Route::Monster, 135},     {461, Route::Monster, 136},     {462, Route::Monster, 109},
	{463, Route::Player, 16},       {464, Route::Player, 99},       {465, Route::Player, 20},
	{466, Route::Player, 74},       {467, Route::Monster, 333},     {468, Route::Monster, 334},
	{469, Route::Monster, 335},     {470, Route::Monster, 336},     {471, Route::Player, 40},
	{472, Route::Player, 42},       {473, Route::Player, 44},       {474, Route::Player, 46},
	{479, Route::Monster, 25},      {480, Route::Monster, 412},     {481, Route::Player, 39},
	{482, Route::Player, 41},       {483, Route::Player, 43},       {484, Route::Player, 45},
	{485, Route::PhysAsExtra, 0},   {486, Route::PhysAsExtra, 0},   {487, Route::PhysAsExtra, 0},
	{488, Route::PhysAsExtra, 0},   {489, Route::PhysAsExtra, 0},   {490, Route::Area, 3},
	{491, Route::Area, 3},          {492, Route::Area, 3},          {493, Route::Area, 3},
	{494, Route::Area, 3},          {495, Route::Area, 3},          {502, Route::Monster, 141},
	{503, Route::Monster, 153},     {504, Route::Player, 93},       {505, Route::Player, 105},
	{506, Route::Monster, 197},     {507, Route::Player, 76},       {508, Route::Player, 77},
	{509, Route::Player, 36},       {510, Route::Player, 67},       {520, Route::Player, 89},
	{523, Route::Area, 3},          {524, Route::Area, 3},          {546, Route::Area, 4},
	{551, Route::Area, 5},          {552, Route::Area, 3},          {553, Route::Elsewhere, 0},
	{556, Route::Area, 6},          {558, Route::Area, 9},          {561, Route::Area, 10},
};
// The ear and drop stats (405-411, 547-550, 555, 559) route to themselves, as the default does.

// Phys-as-extra (port 485-489, PD2 432-436): elemental damage of v% of the monster's physical damage. A length of 0
// (the stat's layer) takes the default.
struct Extra {
	int16_t stat;
	int16_t min, max, length, defaultLength;
};
constexpr Extra Extras[] = {
	{485, 50, 51, 0, 0},     // lightning
	{486, 54, 55, 56, 25},   // cold
	{487, 48, 49, 0, 0},     // fire
	{488, 57, 58, 59, 125},  // poison
	{489, 52, 53, 0, 0},     // magic
};

constexpr int32_t StatLevel = 12;
constexpr int32_t MapState = 244;           // states `map` (PD2 198)
constexpr int32_t MaxHpPercent = 76;        // item_maxhp_percent: on a monster, PD2 scales its maxhp instead
constexpr int32_t MaxHpScaledMark = 485;    // PD2 marks that in the list with stat 432 (port 485) = 1
constexpr int32_t FirstMapLevel = 138;      // the port's PD2 areas, 138-201 (PD2 137-210 without 202-206):
constexpr int32_t LastMapLevel = 201;       // PD2's map-area test 0x1037c510
constexpr int MaxEntries = 64;  // per list; a map rolls far fewer stats
constexpr int AreaSlots = 16;    // PD2's area slots 0-15
constexpr int32_t StatCorrupted = 413, StatCorruptor = 414;  // PD2 360 and 361
// merged properties.txt *Id corrupt, corruptnum, desecrate, desecratenum (PD2 271, 272, 426, 427 by row)
constexpr int32_t PropCorrupt = 288, PropCorruptNum = 289, PropDesecrate = 443, PropDesecrateNum = 444;
constexpr int32_t DesecrateFloor = 851;  // PD2's Desecration roll, 851-999

// The game counter keys of the lists: 'M', the list ('m', 'p', or 'r' for the area entries), the entry and the field
// (0: stat << 16 | layer, 1: value); entry and field 0xff hold the count.
constexpr auto Key(char list, int entry, int field) noexcept -> uint32_t {
	return 0x4d000000u | static_cast<uint32_t>(static_cast<uint8_t>(list)) << 16 |
	       static_cast<uint32_t>(entry & 0xff) << 8 | static_cast<uint32_t>(field & 0xff);
}

std::atomic<uint32_t> g_routed {0};
std::atomic<uint32_t> g_monsters {0};
std::atomic<uint32_t> g_playersOn {0};
std::atomic<uint32_t> g_playersOff {0};
std::atomic<uint32_t> g_drops {0};  // map-area drops a drop chance stat changed
std::atomic<uint32_t> g_extraBosses {0};  // presets the extra boss stat spawned a second time
std::atomic<bool> g_seenClass[2048] {};  // for the game check: each class's first spawn in a map area is logged

using SpawnedFn = void (*)(void* params, Unit* monster) noexcept;
using RegenFn = void (*)(Game* game, Unit* player, int32_t a, int32_t b) noexcept;
using AreaLevelFn = uint64_t (*)(Game* game, int32_t level) noexcept;
using ItemSpawnFn = Unit* (*)(Game* game, uint8_t* drop, int32_t arg) noexcept;
using PresetSpawnFn = void (*)(Game* game, void* room, const uint8_t* preset) noexcept;
SpawnedFn g_spawnedOriginal = nullptr;
PresetSpawnFn g_presetSpawnOriginal = nullptr;
AreaLevelFn g_areaLevelOriginal = nullptr;
RegenFn g_regenOriginal = nullptr;
ItemSpawnFn g_itemSpawnOriginal = nullptr;

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto Count(Game* game, char list) noexcept -> int32_t {
	const int32_t* count = Counter(game, Key(list, 0xff, 0xff));
	return count != nullptr ? *count : 0;
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : -1;
}

auto InMapArea(Unit* unit) noexcept -> bool {
	const int32_t level = LevelOf(unit);
	return level >= FirstMapLevel && level <= LastMapLevel;
}

auto Find(int32_t stat) noexcept -> const Routed* {
	for (const Routed& r : Routes) {
		if (r.stat == stat) {
			return &r;
		}
	}
	return nullptr;
}

auto Push(Game* game, char list, int32_t stat, int32_t layer, int32_t value) noexcept -> void {
	// Each Counter call may insert, so no pointer is kept across another call.
	const int32_t n = Count(game, list);
	int32_t* packed = n < MaxEntries ? Counter(game, Key(list, n, 0)) : nullptr;
	if (packed != nullptr) {
		*packed = stat << 16 | (layer & 0xffff);
	}
	int32_t* stored = packed != nullptr ? Counter(game, Key(list, n, 1)) : nullptr;
	int32_t* count = stored != nullptr ? Counter(game, Key(list, 0xff, 0xff)) : nullptr;
	if (count == nullptr) {
		plog::Warn("map stats: list '%c' full, stat %d dropped", list, stat);
		return;
	}
	*stored = value;
	*count = n + 1;
}

// PD2's 0x1038aa00 phys-as-extra: (int)(v / 100.f * damage), from the monster's mindamage and maxdamage.
auto SetExtra(uint32_t dataFlag, void* list, Unit* monster, const Extra& e, int32_t layer, int32_t value) noexcept
	-> void {
	using namespace d2r::rva;
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto set = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const float share = static_cast<float>(value) / 100.0f;
	const auto min = static_cast<int32_t>(share * static_cast<float>(stat(monster, StatMinDamage, 0)));
	const auto max = static_cast<int32_t>(share * static_cast<float>(stat(monster, StatMaxDamage, 0)));
	set(dataFlag, list, e.min, min != 0 ? min : 1, 0);  // the min goes through PD2's 0 -> 1 rule, the max does not
	set(dataFlag, list, e.max, max, 0);
	if (e.length != 0) {
		set(dataFlag, list, e.length, layer != 0 ? layer : e.defaultLength, 0);
	}
}

// PD2's 0x1038aa00: the unit's state-`map` stat list from a routed list, unless it has one.
auto Apply(Game* game, Unit* unit, char which) noexcept -> bool {
	using namespace d2r::rva;
	const int32_t count = Count(game, which);
	if (count <= 0 || d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, MapState) != nullptr) {
		return false;
	}
	const int32_t type = At<int32_t>(unit, UnitType);
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(unit, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(0, 0, type, id);
	if (list == nullptr) {
		return false;
	}
	d2r::Engine<void (*)(void*, int32_t) noexcept>(SetListState)(list, MapState);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto set = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	for (int32_t i = 0; i < count && i < MaxEntries; ++i) {
		const int32_t* packed = Counter(game, Key(which, i, 0));
		const int32_t* stored = Counter(game, Key(which, i, 1));
		if (packed == nullptr || stored == nullptr) {
			continue;
		}
		const int32_t stat = static_cast<uint32_t>(*packed) >> 16;
		const int32_t layer = *packed & 0xffff;
		const int32_t value = *stored;
		const Extra* extra = nullptr;
		for (const Extra& e : Extras) {
			extra = e.stat == stat ? &e : extra;
		}
		if (extra != nullptr) {
			SetExtra(dataFlag, list, unit, *extra, layer, value);
		} else if (stat == MaxHpPercent && type == UnitMonster) {
			// Unsigned as in PD2, so a negative value would cap maxhp; no PD2 map rolls one.
			// shortcut: life stays as it was (PD2 raises only maxhp); upgrade if play shows monsters spawning hurt
			const auto maxhp = static_cast<uint32_t>(
				d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(unit, StatMaxLife, 0));
			uint64_t scaled = maxhp + static_cast<uint64_t>(maxhp / 100) * static_cast<uint32_t>(value);
			scaled = scaled > 0x7fffffffu ? 0x7fffffffu : scaled;
			d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(
				unit, StatMaxLife, static_cast<int32_t>(scaled), 0);
			set(dataFlag, list, MaxHpScaledMark, 1, 0);
		} else {
			set(dataFlag, list, stat, value != 0 ? value : 1, layer);
		}
	}
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(unit, list, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, MapState, 1);
	return true;
}

auto Remove(Game* game, Unit* unit) noexcept -> bool {
	using namespace d2r::rva;
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, MapState);
	if (list == nullptr) {
		return false;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(ClearUnitState)(unit, MapState, 0);
	d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(unit, list);
	d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(d2r::DataFlag(game), list);
	return true;
}

// For the game check: each routed stat's total on the unit, in one log line.
auto LogTotals(Game* game, Unit* unit, char which) noexcept -> void {
	char line[600] = "";
	size_t used = 0;
	for (int32_t i = 0; i < Count(game, which) && i < MaxEntries && used < sizeof(line) - 24; ++i) {
		const int32_t* packed = Counter(game, Key(which, i, 0));
		if (packed == nullptr) {
			continue;
		}
		const auto stat = static_cast<int32_t>(static_cast<uint32_t>(*packed) >> 16);
		const int32_t total =
			d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, *packed & 0xffff);
		used += static_cast<size_t>(std::snprintf(line + used, sizeof(line) - used, " %d=%d", stat, total));
	}
	plog::Info("map stats:   totals%s", line);
}

struct AreaEntry {
	int32_t slot, value;
};

// PD2's use handler 0x383060 hard-codes the other levels of each multi-level map by its entry level (PD2 ids in the
// comments; merge/ids-levels.tsv gives the port's). Outer Void and Na-Krul's Abyss are one level.
struct MultiLevel {
	int32_t entry, more[3];
};
constexpr MultiLevel MultiLevels[] = {
	{163, {164}},            // Realm of Terror (164-165)
	{168, {169}},            // Sanctuary of Sin (171-172)
	{182, {183}},            // Steppes of Daken-Shar (186-187)
	{172, {173, 174, 175}},  // Zhar's Sanctum (176-179)
	{177, {178}},            // Warlord of Blood (181-182)
	{179, {180}},            // Fallen Gardens (183-184)
	{188, {190}},            // Imperial Palace (192, 194)
	{191, {192}},            // City of Ureh (195-196)
	{193, {194, 195}},       // Djinn's Domain (197-199)
};

// An area slot's value in this game: the first entry of that slot, as PD2's getter 0x1038a9e0 reads it; 0 for none.
auto AreaSlot(Game* game, int slot) noexcept -> int32_t {
	const int32_t* v = Counter(game, Key('a', slot, 0));
	return v != nullptr ? *v : 0;
}

// PD2's 0x1038b4d0 on the level's monster region (rva::GameMonRegions), in the map's stat order: density (0) and
// rarity (2) scale by 1 + v/100, each add-monster (3) is one more group, fortify (4) scales by v/100. Fortify, drop
// bonus (12) and treacherous (14) also add monster stats, after the routed ones. Truncations are PD2's (ftol, then a
// byte for the pack counts). D2R never reads the region's monster level, so area level (1) is AreaMonsterLevel's.
// As in PD2, only the first call (the entry level) adds the monster stats.
auto ApplyArea(Game* game, int32_t level, const AreaEntry* entries, int count, bool first) noexcept -> void {
	using namespace d2r::rva;
	auto* const* regions = At<uint8_t**>(game, GameMonRegions);
	uint8_t* r = regions != nullptr && level > 0 && level <= LastMapLevel ? regions[level] : nullptr;
	if (r == nullptr) {
		plog::Warn("map area: no monster region for level %d", level);
		return;
	}
	auto& den = *reinterpret_cast<uint32_t*>(r + RegionMonDen);
	const auto scalePacks = [r](float by) noexcept {
		r[RegionBossMin] = static_cast<uint8_t>(static_cast<int32_t>(static_cast<float>(r[RegionBossMin]) * by));
		r[RegionBossMax] = static_cast<uint8_t>(static_cast<int32_t>(static_cast<float>(r[RegionBossMax]) * by));
	};
	plog::Info("map area: level %d before: density %u, packs %u-%u, %u groups", level, den, r[RegionBossMin],
	           r[RegionBossMax], r[RegionCount]);
	for (int i = 0; i < count; ++i) {
		const int32_t v = entries[i].value;
		switch (entries[i].slot) {
		case 0:
			den = static_cast<uint32_t>(static_cast<float>(den) * (static_cast<float>(v) / 100.0f + 1.0f));
			break;
		case 2:
			scalePacks(static_cast<float>(v) / 100.0f + 1.0f);
			break;
		case 3: {
			if (d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(d2r::DataFlag(game), v) ==
			    nullptr) {
				plog::Warn("map area: no monstats row %d to add", v);
				break;
			}
			// PD2 overwrites the 13th group when full but still counts it; the port keeps the count at 13
			const int slot = r[RegionSpawnCount] < RegionMaxGroups ? r[RegionSpawnCount] : RegionMaxGroups - 1;
			uint8_t* g = r + RegionGroups + slot * RegionGroupSize;
			const bool replacing = r[RegionCount] >= RegionMaxGroups;
			r[RegionRarity] = static_cast<uint8_t>(r[RegionRarity] + 1 - (replacing ? g[2] : 0));
			for (int b = 0; b < RegionGroupSize; ++b) {
				g[b] = 0;
			}
			*reinterpret_cast<int16_t*>(g) = static_cast<int16_t>(v);
			g[2] = 1;  // rarity
			g[3] = 1;  // one component pick, all default (PD2's 0x101)
			r[RegionCount] = static_cast<uint8_t>(replacing ? RegionMaxGroups : r[RegionCount] + 1);
			r[RegionSpawnCount] = static_cast<uint8_t>(slot + 1);
			plog::Info("map area: level %d adds monster %d as group %d, rarity sum %u", level, v, slot, r[RegionRarity]);
			break;
		}
		case 4:
			den = static_cast<uint32_t>(static_cast<float>(den) * (static_cast<float>(v) / 100.0f));
			scalePacks(static_cast<float>(v) / 100.0f);
			for (const auto& [stat, value] : {std::pair {MaxHpPercent, 100}, {25, 40}, {329, 40}, {331, 40},
			                                  {330, 40}, {332, 40}, {357, 20}, {546, v}}) {
				if (first) {
					Push(game, 'm', stat, 0, value);  // max life %, damage %, the masteries, map_glob_skirmish_mode
				}
			}
			break;
		case 12:
			if (first) {
				Push(game, 'm', 399, 0, v);  // map_glob_dropbonus
			}
			break;
		case 14:
			if (first) {
				Push(game, 'm', MaxHpPercent, 0, 100);
				Push(game, 'm', 402, 0, v);  // treacherous
			}
			break;
		default:
			break;
		}
	}
	plog::Info("map area: level %d after: density %u, packs %u-%u, %u groups; area level +%d", level, den,
	           r[RegionBossMin], r[RegionBossMax], r[RegionCount], AreaSlot(game, 1));
}

// PD2's monster spawn hook 0x1038af90: a hostile monster spawning in a map area gets the monster list. PD2 gives a
// monster with a monstats Align an empty list under the state.
// shortcut: an aligned monster gets no list and no state; upgrade if something reads the map state on minions
auto OnMonsterSpawned(void* params, Unit* monster) noexcept -> void {
	g_spawnedOriginal(params, monster);
	using namespace d2r::rva;
	Game* game = params != nullptr ? *static_cast<Game**>(params) : nullptr;  // params +0: the game (0x493cc0)
	if (game == nullptr || monster == nullptr || At<int32_t>(monster, UnitType) != UnitMonster ||
	    !InMapArea(monster)) {
		return;
	}
	// PD2 scales after the map list; no map list is open while an uber fight is, so the order is moot here
	ScaleUberMonster(game, monster);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		d2r::DataFlag(game), At<int32_t>(monster, UnitClassId));
	if (record == nullptr || At<uint8_t>(record, MonAlign) != 0) {
		return;
	}
	const int32_t maxhp = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatMaxLife, 0);
	const int32_t classId = At<int32_t>(monster, UnitClassId);
	if (classId >= 0 && classId < static_cast<int32_t>(std::size(g_seenClass)) && !g_seenClass[classId].exchange(true)) {
		plog::Info("map stats: first monster %d in level %d, monster level %d", classId, LevelOf(monster),
		           d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatLevel, 0));
	}
	if (Apply(game, monster, 'm') && plog::Sample(g_monsters)) {
		plog::Info("map stats: monster %d (monster level %d) in level %d, maxhp %d -> %d (%u so far)",
		           At<int32_t>(monster, UnitClassId),
		           d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatLevel, 0),
		           LevelOf(monster), maxhp >> 8,
		           d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatMaxLife, 0) >> 8,
		           g_monsters.load());
		LogTotals(game, monster, 'm');
	}
}

// PD2's level-change hook 0x1038b000, checked every frame: a player in a map area has the player list, elsewhere
// not.
auto PlayerFrame(Game* game, Unit* player, int32_t a, int32_t b) noexcept -> void {
	g_regenOriginal(game, player, a, b);
	if (game == nullptr || player == nullptr || At<int32_t>(player, d2r::rva::UnitType) != 0) {
		return;
	}
	if (InMapArea(player)) {
		if (Apply(game, player, 'p')) {
			plog::Info("map stats: player list on in level %d (%u)", LevelOf(player), ++g_playersOn);
			LogTotals(game, player, 'p');
		}
	} else if (Remove(game, player)) {
		plog::Info("map stats: player list off in level %d (%u)", LevelOf(player), ++g_playersOff);
	}
}

// PD2's 0x102fc400 and the region's monster level: a map area's monster level (D2R's 0x508f80, which monster spawns,
// uniques and item levels read) gets the map's area level on top.
auto AreaMonsterLevel(Game* game, int32_t level) noexcept -> uint64_t {
	const uint64_t base = g_areaLevelOriginal(game, level);
	if (game == nullptr || level < FirstMapLevel || level > LastMapLevel) {
		return base;
	}
	return static_cast<uint32_t>(static_cast<int32_t>(base) + AreaSlot(game, 1));
}

// The CRT's rand state is per thread and starts at seed 1: seed each thread once, or every session rolls the same
auto Rand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

// PD2's 0x103649d0: a random corruption outcome from cubemain's corruption rows (op 16 or 18 on `corruptor` with a
// value), unless the item is corrupted. Only rows whose first input matches the item (PD2's own check: class, type,
// quality, socketed, ethereal) and whose second is the Worldstone Shard or nothing count. The group's "destroyed
// (rare)" row (rare usetype output, no third mod) sets the roll to value..999, so the outcome is one of the rows after
// it and never the destroyed one; the first row whose value beats the roll puts its mods on.
// With `desecrate` (PD2's flag 1, Desecration) the roll is 851-999 from the start (an amulet's rows 682-689) and the
// row's corrupt/corruptnum go on as desecrate/desecratenum; the caller checks the item.
auto CorruptItem(Game* game, Unit* item, bool desecrate) noexcept -> bool {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	if (!desecrate && stat(item, StatCorrupted, 0) != 0) {
		return false;
	}
	const auto record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(CubeRecipe);
	const auto itemRecord = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord);
	const auto has = [item](uint64_t fn) noexcept { return d2r::Engine<int32_t (*)(Unit*) noexcept>(fn)(item) != 0; };
	const int32_t rows = d2r::Engine<int32_t (*)(uint32_t) noexcept>(CubeRecipeCount)(dataFlag);
	int32_t floor = desecrate ? DesecrateFloor : 0;
	int32_t roll = desecrate ? DesecrateFloor + Rand() % (1000 - DesecrateFloor) : 0;
	for (int32_t i = 0; i < rows; ++i) {
		const uint8_t* r = record(dataFlag, i);
		if (r == nullptr || r[CubeEnabled] == 0 || (r[CubeOp] != 16 && r[CubeOp] != 18) ||
		    At<int32_t>(r, CubeParam) != StatCorruptor || At<int32_t>(r, CubeValue) == 0) {
			continue;
		}
		const uint8_t* in = r + CubeInputs;
		const int32_t shard = At<int32_t>(in + CubeInputSize, 2);
		const uint8_t* shardRecord = shard > 0 ? itemRecord(dataFlag, shard) : nullptr;
		if (shardRecord != nullptr && At<uint32_t>(shardRecord, ItCode) != 0x20737377) {  // 'wss '
			continue;
		}
		const auto flags = At<uint16_t>(in, 0);
		const int32_t id = At<int32_t>(in, 2);
		if (((flags & 2) != 0 && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(item, id) == 0) ||
		    ((flags & 1) != 0 && id != -1 && id != 0xffff && At<int32_t>(item, UnitClassId) != id) ||
		    (in[6] != 0 && d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item) != in[6]) ||
		    ((flags & 0x20) != 0 && has(ItemIsEthereal)) || ((flags & 0x10) != 0 && !has(ItemIsEthereal)) ||
		    ((flags & 4) != 0 && has(ItemHasSockets)) || ((flags & 8) != 0 && !has(ItemHasSockets))) {
			continue;
		}
		const int32_t value = At<int32_t>(r, CubeValue);
		if (!desecrate && r[CubeOutType] == 0xff && r[CubeOutQuality] == 6 &&
		    At<int32_t>(r, CubeOutMods + 2 * CubeModSize) == -1) {
			floor = value;
			roll = value < 1000 ? value + Rand() % (1000 - value) : value;  // PD2 divides by 0 at 1000
		}
		if (floor == 0 || roll >= value) {
			continue;
		}
		for (int m = 0; m < 5; ++m) {
			const uint8_t* mod = r + CubeOutMods + m * CubeModSize;
			if (At<int32_t>(mod, 0) == -1) {
				break;
			}
			int32_t prop = At<int32_t>(mod, 0);
			if (desecrate) {
				prop = prop == PropCorrupt ? PropDesecrate : prop == PropCorruptNum ? PropDesecrateNum : prop;
			}
			int32_t property[4] = {prop, At<int16_t>(mod, 4), At<int16_t>(mod, 6), At<int16_t>(mod, 8)};
			d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(ApplyCubeMod)(item, property);
		}
		plog::Info("%s: item %d %s by cubemain row %d (roll %d of %d-999)", desecrate ? "desecration" : "map drops",
		           At<int32_t>(item, UnitClassId), desecrate ? "desecrated" : "corrupted", i, roll, floor);
		return true;
	}
	return false;
}

// PD2's 0x103816c0: a socketable item that is not a throwing potion, thrown weapon or quiver (item types 38, 48, 56)
// gets 1-2 sockets, a two-handed one 2-4, clamped to what its type allows.
auto SocketDrop(Game* game, Unit* item) noexcept -> bool {
	using namespace d2r::rva;
	const uint8_t* rec = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord)(
		d2r::DataFlag(game), At<int32_t>(item, UnitClassId));
	const int16_t type = rec != nullptr ? At<int16_t>(rec, ItType) : -1;
	if (rec == nullptr || type == 38 || type == 48 || type == 56 ||
	    d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemSocketable)(item) == 0) {
		return false;
	}
	const int32_t low = rec[It2Handed] != 0 ? 2 : 1;
	const int32_t n = low + Rand() % (low + 1);  // 1-2 or 2-4
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetItemSockets)(item, n);
	return true;
}

auto Chance(Game* game, int slot) noexcept -> bool {
	const int32_t v = AreaSlot(game, slot);
	return v > 0 && Rand() % 100 < v;
}

// PD2's drop hook 0x10372180, on the item the TC drop (0x444680) makes for a unit in a map area: slot 13's chance to
// make it ethereal (D2R's own ethereal roll, forced as PD2 forces it), then slot 6's to corrupt it and slot 11's to
// socket it. Drops from anywhere else (vendors, cube, gambling, gold) pass through.
auto OnItemSpawn(Game* game, uint8_t* drop, int32_t arg) noexcept -> Unit* {
	using namespace d2r::rva;
	const bool tcDrop = reinterpret_cast<uint64_t>(_ReturnAddress()) == d2r::imageBase + TcDropReturn;
	Unit* dropper = tcDrop && drop != nullptr ? At<Unit*>(drop, DropUnit) : nullptr;
	const bool map = game != nullptr && dropper != nullptr && InMapArea(dropper);
	const bool ethereal = map && Chance(game, 13);
	if (ethereal) {
		*reinterpret_cast<uint32_t*>(drop + DropFlags) |= DropFlagEthereal;
	}
	Unit* item = g_itemSpawnOriginal(game, drop, arg);
	if (map && item != nullptr) {
		const bool madeEthereal =
			ethereal && d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemIsEthereal)(item) != 0;  // not every item can be
		const bool corrupted = Chance(game, 6) && CorruptItem(game, item, false);
		const bool socketed = Chance(game, 11) && SocketDrop(game, item);
		if ((madeEthereal || corrupted || socketed) && plog::Sample(g_drops)) {
			plog::Info("map drops: item %d ethereal %d, corrupted %d, socketed %d (%u so far)",
			           At<int32_t>(item, UnitClassId), madeEthereal, corrupted, socketed, g_drops.load());
		}
	}
	return item;
}

// PD2's six id sets (0x105de938, 0x105de7d4, 0x105de9c8, 0x105de9d8, 0x105de9b0, 0x105de818, filled at start by
// 0x10190e30-0x101911f0) and 1044 (lernaeanhydra1), which 0x10371290 tests: the map bosses and their minions, as port
// monstats ids (tools/pd2_ids.py monstats_ids).
constexpr int16_t ExtraBossIds[] = {
	247,  248,  249,  760,  763,  764,  767,  817,  841,  899,  901,  911,  932,  940,  941,  943,
	944,  980,  985,  986,  987,  988,  989,  990,  1013, 1014, 1017, 1042, 1043, 1061, 1120, 1121,
	1122, 1123, 1124, 1166, 1181, 1183, 1191, 1192, 1200, 1201, 1202, 1203, 1257, 1259, 1260,
};

// PD2's preset wrapper 0x1038bfc0, slot 5: in a map area with map_glob_extra_boss above 0, a monster preset whose id
// is one of the map bosses spawns a second time. No PluginSDK service spawns or reports room presets, so this hooks
// D2R's preset spawn (rva::PresetSpawn).
// The hook also drops the uber levels' waypoint presets (NoUberWaypoint, map_open.cpp).
auto OnPresetSpawn(Game* game, void* room, const uint8_t* preset) noexcept -> void {
	if (game != nullptr && room != nullptr && preset != nullptr && NoUberWaypoint(game, room, preset)) {
		return;
	}
	g_presetSpawnOriginal(game, room, preset);
	using namespace d2r::rva;
	if (game == nullptr || room == nullptr || preset == nullptr || At<int32_t>(preset, PresetType) != UnitMonster) {
		return;
	}
	const int32_t level = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
	const int32_t id = At<int32_t>(preset, PresetId);
	bool boss = false;
	for (const int16_t b : ExtraBossIds) {
		boss = boss || b == id;
	}
	if (level < FirstMapLevel || level > LastMapLevel || !boss || AreaSlot(game, 5) <= 0) {
		return;
	}
	g_presetSpawnOriginal(game, room, preset);
	if (plog::Sample(g_extraBosses)) {
		plog::Info("map area: extra boss %d in level %d (%u so far)", id, level, g_extraBosses.load());
	}
}

// PD2's boss test in 0x103760a0: the monster's class is in one of the six id sets 0x105deb54, 0x105dea4c, 0x105debcc,
// 0x105debdc, 0x105debb4 and 0x105dea84. They hold the same ids as the extra boss sets, without lernaeanhydra1.
auto IsMapBoss(int32_t classId) noexcept -> bool {
	for (const int16_t b : ExtraBossIds) {
		if (b == classId && b != 1061) {  // 1061: lernaeanhydra1 (PD2 1044), only in the extra boss test
			return true;
		}
	}
	return false;
}

// PD2's level set 0x105dea8c (Zhar's Sanctum 172-175, Uber Diablo 138, the Necropolis 160, Lucion's arena 184; PD2's
// Uber Ancients level is cut) and Djinn's Domain (193-195): each kill there drops once more.
constexpr int32_t ExtraDropLevels[] = {172, 173, 174, 175, 138, 160, 184, 193, 194, 195};

// PD2's map-stat drops in 0x103799d0: a monster stat, and the TC row its chance drops from (port ids, PD2's in the
// comments). The ear list is 0x1044ed20.
struct StatDrop {
	int16_t stat;
	const char* tc;
};
constexpr StatDrop StatDrops[] = {
	{547, "MapStat DropJewelry"},   // map_mon_dropjewelry (494), TC row 982
	{548, "MapStat DropWeapons"},   // map_mon_dropweapons (495)
	{549, "MapStat DropArmor"},     // map_mon_droparmor (496)
	{550, "MapStat DropCrafting"},  // map_mon_dropcrafting (497)
	{405, "Amazon Ear Drop"},       // zon_ear_drops (279), TC row 1114
	{406, "Assassin Ear Drop"},     // sin_ear_drops (280)
	{407, "Barb Ear Drop"},         // barb_ear_drops (281)
	{411, "Druid Ear Drop"},        // map_mon_dropclassitems (285), as PD2's list pairs them
	{408, "Necro Ear Drop"},        // necro_ear_drops (282)
	{409, "Paladin Ear Drop"},      // paladin_ear_drops (283)
	{410, "Sorc Ear Drop"},         // sorc_ear_drops (284)
	{555, "MapStat DropCharms"},    // map_mon_dropcharms (502), TC row 986
	{559, "MapStat DropJewels"},    // map_mon_dropjewels (506), TC row 988
};

std::atomic<uint32_t> g_deathDrops {0};  // extra drops and stat drops made

}  // namespace

auto DesecrateItem(Game* game, Unit* item) noexcept -> bool {
	return CorruptItem(game, item, true);
}

auto MapAreaSlot(Game* game, Unit* unit, int slot) noexcept -> int32_t {
	return game != nullptr && unit != nullptr && InMapArea(unit) ? AreaSlot(game, slot) : 0;
}

auto MapRollMultiplier(Unit* monster) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	return monster != nullptr && stat(monster, 546, 0) > 0 && !IsMapBoss(At<int32_t>(monster, UnitClassId)) ? 2 : 1;
}

// PD2's map block of 0x103799d0, for a hostile monster that is not a minion dying in a map area. Extra drops (the
// monster's own TC once more, 0x103843d0): fortify (`map_glob_skirmish_mode` on the monster) always, or 25% for a map
// boss; the levels above; `map_glob_dropbonus`'s v%. Then each drop stat's v x MapRollMultiplier % drops its TC at the
// monster's level. Rolls use the monster's seed, as PD2's 0x1036f320 does.
auto MapDeathDrops(Game* game, Unit* monster, Unit* killer, uint32_t* seed) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr || monster == nullptr || killer == nullptr || seed == nullptr || !InMapArea(monster)) {
		return;
	}
	const auto* data = At<const uint8_t*>(monster, UnitData);
	if (data == nullptr || (data[MonDataTypeFlags] & MonTypeMinion) != 0) {
		return;
	}
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t classId = At<int32_t>(monster, UnitClassId);
	const int32_t level = LevelOf(monster);
	struct {
		void* unused;
		Unit* monster;
		Unit* killer;
	} death {nullptr, monster, killer};
	int extra = 0;
	const auto again = [&]() noexcept {
		d2r::Engine<void (*)(Game*, Unit*, int32_t, void*) noexcept>(MonsterTcDrop)(game, monster, 0, &death);
		++extra;
	};
	const bool boss = IsMapBoss(classId);
	const uint32_t roll = d2r::RandomBelow(seed, 100);
	if (stat(monster, 546, 0) > 0 && (!boss || roll < 25)) {
		again();
	}
	for (const int32_t l : ExtraDropLevels) {
		if (l == level) {
			again();
		}
	}
	const int32_t bonus = stat(monster, 399, 0);  // map_glob_dropbonus
	if (bonus > 0 && static_cast<int32_t>(d2r::RandomBelow(seed, 100)) < bonus) {
		again();
	}
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
	// shortcut: the item level is the monster's level (PD2 reads it through an import, 0x103a1b00, not traced)
	const int32_t ilvl = stat(monster, StatLevel, 0);
	const int32_t mult = MapRollMultiplier(monster);
	int made = 0;
	for (const StatDrop& d : StatDrops) {
		const int32_t v = stat(monster, d.stat, 0);
		if (v <= 0 || tables == nullptr || static_cast<int32_t>(d2r::RandomBelow(seed, 100)) >= v * mult) {
			continue;
		}
		const int32_t index = d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
			At<void*>(tables, TablesTcNames), d.tc, 0);
		const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
											dataFlag, static_cast<uint16_t>(index), 0)
		                              : nullptr;
		if (tc == nullptr) {
			plog::Warn("map drops: no TC \"%s\"", d.tc);
			continue;
		}
		d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, void*, int32_t*,
		                     int32_t) noexcept>(TcDrop)(game, monster, killer, tc, 0, ilvl, 0, nullptr, nullptr, 0);
		plog::Info("map drops: monster %d in level %d drops from \"%s\" (stat %d = %d, x%d)", classId, level, d.tc,
		           d.stat, v, mult);
		++made;
	}
	if (extra + made > 0) {
		g_deathDrops.fetch_add(static_cast<uint32_t>(extra + made), std::memory_order_relaxed);
		plog::Info("map drops: monster %d (type flags 0x%02x%s) in level %d: %d extra drops, %d stat drops (%u so far)",
		           classId, data[MonDataTypeFlags], boss ? ", map boss" : "", level, extra, made, g_deathDrops.load());
	}
}

auto RouteMapStats(Game* game, Unit* item, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr) {
		return;
	}
	// A new map replaces the last one's lists and slots. Units that already have a list keep it until they lose it.
	for (const char which : {'m', 'p', 'r'}) {
		if (int32_t* count = Counter(game, Key(which, 0xff, 0xff)); count != nullptr) {
			*count = 0;
		}
	}
	for (int slot = 0; slot < AreaSlots; ++slot) {
		if (int32_t* v = Counter(game, Key('a', slot, 0)); v != nullptr) {
			*v = 0;
		}
	}
	const uint8_t* list = d2r::Engine<const uint8_t* (*)(Unit*, int32_t, int32_t) noexcept>(UnitFlagList)(item, 0,
	                                                                                                       0x40);
	if (list == nullptr) {
		plog::Info("map stats: the map has no magic list");
		return;
	}
	struct Entry {
		int32_t layer, stat, value, extra;
	};
	const auto* entries = At<const Entry*>(list, ListEntries);
	const uint64_t count = At<uint64_t>(list, ListCount);
	AreaEntry area[MaxEntries];
	int areaCount = 0;
	for (uint64_t i = 0; entries != nullptr && i < count; ++i) {
		const Entry& e = entries[i];
		const Routed* r = Find(e.stat);
		const Route route = r != nullptr ? r->route : Route::Monster;
		const int32_t target = r != nullptr && route != Route::PhysAsExtra ? r->target : e.stat;
		const char* where = "elsewhere (event)";
		if (route == Route::Monster || route == Route::PhysAsExtra) {
			Push(game, 'm', target, e.layer, e.value);
			where = "monsters";
		} else if (route == Route::Player) {
			Push(game, 'p', target, e.layer, e.value);
			where = "players";
		} else if (route == Route::Area && areaCount < MaxEntries) {
			area[areaCount++] = {target, e.value};
			Push(game, 'r', target, 0, e.value);  // kept for ApplyMapArea
			if (int32_t* v = Counter(game, Key('a', target, 0)); v != nullptr && *v == 0) {
				*v = e.value;
			}
			where = "area slot";
		}
		plog::Info("map stats: stat %d layer %d value %d -> %s as %d", e.stat, e.layer, e.value, where, target);
	}
	ApplyArea(game, level, area, areaCount, true);
	for (const MultiLevel& m : MultiLevels) {
		for (const int32_t more : m.more) {
			if (m.entry == level && more != 0) {  // the other levels of a multi-level map
				ApplyArea(game, more, area, areaCount, false);
			}
		}
	}
	++g_routed;
}

auto ApplyMapArea(Game* game, int32_t level) noexcept -> void {
	AreaEntry area[MaxEntries];
	int count = 0;
	for (int32_t i = 0; game != nullptr && i < Count(game, 'r') && i < MaxEntries; ++i) {
		const int32_t* packed = Counter(game, Key('r', i, 0));
		const int32_t* value = Counter(game, Key('r', i, 1));
		if (packed != nullptr && value != nullptr) {
			area[count++] = {static_cast<int32_t>(static_cast<uint32_t>(*packed) >> 16), *value};
		}
	}
	if (game != nullptr) {
		ApplyArea(game, level, area, count, false);
	}
}

auto InstallMapStats(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool monsters = context->InstallInlineHook(MonsterSpawned, MonsterSpawnedEntry, sizeof(MonsterSpawnedEntry),
	                                                 OnMonsterSpawned, &g_spawnedOriginal);
	const bool players = context->InstallInlineHook(PlayerRegenEvent, PlayerRegenEventEntry,
	                                                sizeof(PlayerRegenEventEntry), PlayerFrame, &g_regenOriginal);
	const bool areaLevel = context->InstallInlineHook(AreaMonsterLevelFn, AreaMonsterLevelEntry,
	                                                  sizeof(AreaMonsterLevelEntry), AreaMonsterLevel,
	                                                  &g_areaLevelOriginal);
	const bool drops = context->InstallInlineHook(ItemSpawn, ItemSpawnEntry, sizeof(ItemSpawnEntry), OnItemSpawn,
	                                              &g_itemSpawnOriginal);
	const bool bosses = context->InstallInlineHook(PresetSpawn, PresetSpawnEntry, sizeof(PresetSpawnEntry),
	                                               OnPresetSpawn, &g_presetSpawnOriginal);
	const auto ok = [](bool on) noexcept { return on ? "" : " FAILED (bytes differ)"; };
	std::snprintf(report, reportSize, "monsters%s, players%s, area level%s, drops%s, extra boss%s", ok(monsters),
	              ok(players), ok(areaLevel), ok(drops), ok(bosses));
	return (monsters ? 1 : 0) + (players ? 1 : 0) + (areaLevel ? 1 : 0) + (drops ? 1 : 0) + (bosses ? 1 : 0);
}

auto MapStatsCount() noexcept -> int {
	return 5;
}

auto MapStatsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "map stats routed %u, monsters %u, player lists on %u, off %u, drops changed %u, extra bosses %u, "
	              "death drops %u",
	              g_routed.load(), g_monsters.load(), g_playersOn.load(), g_playersOff.load(), g_drops.load(),
	              g_extraBosses.load(), g_deathDrops.load());
}
