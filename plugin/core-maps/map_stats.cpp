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

#include "log.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

enum class Route : uint8_t { Monster, Player, Elsewhere, PhysAsExtra };

struct Routed {
	int16_t stat;  // the map stat (port id)
	Route route;
	int16_t target;  // the stat it becomes (port id), for Monster and Player
};

// Every PD2 stat whose `Divide` is not the default 1024, by port id (merge/ids-itemstatcost.tsv). Elsewhere: area stats
// (`map_glob_*`, `treacherous`, `phasing`; MS4) and `map_force_event` (MS8).
constexpr Routed Routes[] = {
	{371, Route::Elsewhere, 0},     {372, Route::Elsewhere, 0},     {387, Route::Elsewhere, 0},
	{388, Route::Elsewhere, 0},     {398, Route::Elsewhere, 0},     {399, Route::Elsewhere, 0},
	{400, Route::Elsewhere, 0},     {401, Route::Elsewhere, 0},     {402, Route::Elsewhere, 0},
	{403, Route::Monster, 119},     {404, Route::Elsewhere, 0},     {423, Route::Player, 80},
	{424, Route::Player, 79},       {425, Route::Elsewhere, 0},     {426, Route::Player, 85},
	{427, Route::Elsewhere, 0},     {428, Route::Elsewhere, 0},     {429, Route::Monster, 48},
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
	{488, Route::PhysAsExtra, 0},   {489, Route::PhysAsExtra, 0},   {490, Route::Elsewhere, 0},
	{491, Route::Elsewhere, 0},     {492, Route::Elsewhere, 0},     {493, Route::Elsewhere, 0},
	{494, Route::Elsewhere, 0},     {495, Route::Elsewhere, 0},     {502, Route::Monster, 141},
	{503, Route::Monster, 153},     {504, Route::Player, 93},       {505, Route::Player, 105},
	{506, Route::Monster, 197},     {507, Route::Player, 76},       {508, Route::Player, 77},
	{509, Route::Player, 36},       {510, Route::Player, 67},       {520, Route::Player, 89},
	{523, Route::Elsewhere, 0},     {524, Route::Elsewhere, 0},     {546, Route::Elsewhere, 0},
	{551, Route::Elsewhere, 0},     {552, Route::Elsewhere, 0},     {553, Route::Elsewhere, 0},
	{556, Route::Elsewhere, 0},     {558, Route::Elsewhere, 0},     {561, Route::Elsewhere, 0},
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

constexpr int32_t MapState = 244;           // states `map` (PD2 198)
constexpr int32_t MaxHpPercent = 76;        // item_maxhp_percent: on a monster, PD2 scales its maxhp instead
constexpr int32_t MaxHpScaledMark = 485;    // PD2 marks that in the list with stat 432 (port 485) = 1
constexpr int32_t FirstMapLevel = 138;      // the port's PD2 areas, 138-201 (PD2 137-210 without 202-206):
constexpr int32_t LastMapLevel = 201;       // PD2's map-area test 0x1037c510
constexpr int MaxEntries = 64;  // per list; a map rolls far fewer stats

// The game counter keys of the lists: 'M', the list ('m' or 'p'), the entry and the field (0: stat << 16 | layer,
// 1: value); entry and field 0xff hold the count.
constexpr auto Key(char list, int entry, int field) noexcept -> uint32_t {
	return 0x4d000000u | static_cast<uint32_t>(static_cast<uint8_t>(list)) << 16 |
	       static_cast<uint32_t>(entry & 0xff) << 8 | static_cast<uint32_t>(field & 0xff);
}

std::atomic<uint32_t> g_routed {0};
std::atomic<uint32_t> g_monsters {0};
std::atomic<uint32_t> g_playersOn {0};
std::atomic<uint32_t> g_playersOff {0};

using SpawnedFn = void (*)(void* params, Unit* monster) noexcept;
using RegenFn = void (*)(Game* game, Unit* player, int32_t a, int32_t b) noexcept;
SpawnedFn g_spawnedOriginal = nullptr;
RegenFn g_regenOriginal = nullptr;

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
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		d2r::DataFlag(game), At<int32_t>(monster, UnitClassId));
	if (record == nullptr || At<uint8_t>(record, MonAlign) != 0) {
		return;
	}
	const int32_t maxhp = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatMaxLife, 0);
	if (Apply(game, monster, 'm') && plog::Sample(g_monsters)) {
		plog::Info("map stats: monster %d in level %d, maxhp %d -> %d (%u so far)", At<int32_t>(monster, UnitClassId),
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

}  // namespace

auto RouteMapStats(Game* game, Unit* item) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr) {
		return;
	}
	// A new map replaces the last one's lists. Units that already have a list keep it until they lose it.
	for (const char which : {'m', 'p'}) {
		if (int32_t* count = Counter(game, Key(which, 0xff, 0xff)); count != nullptr) {
			*count = 0;
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
	for (uint64_t i = 0; entries != nullptr && i < count; ++i) {
		const Entry& e = entries[i];
		const Routed* r = Find(e.stat);
		const Route route = r != nullptr ? r->route : Route::Monster;
		const int32_t target = r != nullptr && route != Route::PhysAsExtra ? r->target : e.stat;
		const char* where = "elsewhere (area or event)";
		if (route == Route::Monster || route == Route::PhysAsExtra) {
			Push(game, 'm', target, e.layer, e.value);
			where = "monsters";
		} else if (route == Route::Player) {
			Push(game, 'p', target, e.layer, e.value);
			where = "players";
		}
		plog::Info("map stats: stat %d layer %d value %d -> %s as %d", e.stat, e.layer, e.value, where, target);
	}
	++g_routed;
}

auto InstallMapStats(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool monsters = context->InstallInlineHook(MonsterSpawned, MonsterSpawnedEntry, sizeof(MonsterSpawnedEntry),
	                                                 OnMonsterSpawned, &g_spawnedOriginal);
	const bool players = context->InstallInlineHook(PlayerRegenEvent, PlayerRegenEventEntry,
	                                                sizeof(PlayerRegenEventEntry), PlayerFrame, &g_regenOriginal);
	std::snprintf(report, reportSize, "monsters%s, players%s", monsters ? "" : " FAILED (bytes differ)",
	              players ? "" : " FAILED (bytes differ)");
	return (monsters ? 1 : 0) + (players ? 1 : 0);
}

auto MapStatsCount() noexcept -> int {
	return 2;
}

auto MapStatsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "map stats routed %u, monsters %u, player lists on %u, off %u", g_routed.load(),
	              g_monsters.load(), g_playersOn.load(), g_playersOff.load());
}
