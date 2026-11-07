// PD2's map use (ProjectDiablo.dll 0x383060; docs/research/pd2-map-system-dll.md, section 1): a right-click on a map
// in Harrogath opens a red portal to the level its misc `len` names. The merge moves PD2's map spell 12 to D2R's free
// item spell 16 and writes `len` in the port's level ids, so this hooks D2R's item spell check and use dispatchers and
// handles spell 16 there; every other spell goes on to D2R.
//
// The gates, in PD2's order: Hell, character level 80+, Rite of Passage done in Hell, standing in Harrogath, and no map
// opened yet in this game; then a free spot for the portal (PD2 places it unchecked). A refused map stays in the
// inventory and the player gets D2R's "can't do that" (D2R's caller sends none of its own). The portal is D2R's cube
// "Red Portal" output, which places object 60 by the player and counts the opening per game; its max of 1 on OpenKey
// is the one-per-game gate. PD2's uber-area exception (Uber Tristram, UB3) and its refusal of levels 152, 153 and 255
// belong to `ubtm` and the Monastery map, which keep a blank pSpell.
//
// Uber keys (UB2, docs/research/pd2-uber-bosses-dll.md, section 2) use the same spell: their `len` is an uber level.
// One more gate, standing at the Blood Fountain; then no portal: every player at the fountain is warped in, after
// their pets are removed and their buffs stripped (section 3). The key's tier is kept for the fight, and the opening
// counts on OpenKey, the gate shared with maps. A tier-0 key also leaves PD2's three grey portals by the fountain for
// re-entry. They take PD2's portal operate (77 in PD2, D2R's free operate slot 60 here): each runs the area rules on
// entry, warps the player in and goes. Arriving in an uber level removes its waypoints (section 3).
#include "map_open.h"

#include "d2r.h"
#include "log.h"
#include "map_stats.h"

#include <algorithm>
#include <atomic>
#include <iterator>
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
// Uber keys: Diablo Clone `dcma`, Rathma `rtma`, Lucion `luca` (PD2 levels 137, 161, 188 in the port's ids)
constexpr int32_t UberLevels[] = {138, 160, 184};
// PD2 0x1037d840 is a squared distance to the Blood Fountain (5074, 5114): the user must be within 0x95 of it, and
// the warp takes every player under 0x96
constexpr int32_t FountainX = 5074, FountainY = 5114;
constexpr int32_t FountainUse = 0x95, FountainWarp = 0x96;
constexpr int32_t StatUberTier = 370;  // uber_difficulty (PD2 185): the key's tier, 0-2
// The game counter keys of the open uber fight: its tier (PD2 game +0x2629) and entries (+0x2628)
constexpr uint32_t TierKey = 0x55425254;   // 'UBRT'
constexpr uint32_t EntryKey = 0x55425245;  // 'UBRE'
// PD2's three tier-0 re-entry portals (object 595 `xactportal_grey`, the port's 598) around the fountain
constexpr int32_t GreyPortal = 598;
constexpr int32_t GreyPortalXY[][2] = {{5076, 5119}, {5068, 5108}, {5079, 5110}};
constexpr int32_t UnitObject = 2;
constexpr int32_t CantEnter = 0x14;  // operate 15's refusal message
// The bosses PD2's entry heals by 20% of max life (its set 0x105deec4): Dclone, Rathma and Mendeln and their clones,
// Lucion, in the port's monstats ids
constexpr int32_t UberBosses[] = {806, 950, 951, 952, 953, 1129};
// PD2 removes the waypoints on arriving in its 137 and 161-163 (and 136 and 185, Uber Tristram's, UB3)
constexpr int32_t NoWaypointLevels[] = {138, 160, 161, 162};
// PD2 0x1037f4a0: the states stripped on entering an uber area, in the port's ids: enchant, coldenchant, venomclaws,
// shout, battleorders, battlecommand, wolf, bear, fade, quickness, energyshield, shiverarmor, chillingarmor, bonearmor,
// cyclonearmor, bladeshield, holyshield, holysword, thunderstorm, thawing, antidote, the ten shrine states,
// monsterset, blood_mana, battlecry, openwounds, freeze, cold
constexpr int32_t StrippedStates[] = {16,  223, 31,  26,  32,  51,  139, 140, 159, 157, 30,  88,  20,
                                      14,  151, 158, 101, 279, 38,  179, 178, 128, 129, 130, 131, 132,
                                      133, 134, 135, 136, 137, 176, 114, 89,  62,  1,   11};
static_assert(std::size(StrippedStates) == 37);
// PD2 0x1037fb10 walks every pet type but 3 (`golem`)
// shortcut: 31 is the merged pettype.txt's row count; read it from the tables if a row is ever added
constexpr int32_t PetTypes = 31, KeptPetType = 3;

using CheckFn = uint64_t (*)(Game* game, Unit* player, Unit* item, Unit* target) noexcept;
using UseFn = void (*)(Game* game, Unit* player, Unit* item, Unit* target) noexcept;

CheckFn g_checkOriginal = nullptr;
UseFn g_useOriginal = nullptr;
std::atomic<uint32_t> g_checks {0};
std::atomic<uint32_t> g_refused {0};
std::atomic<uint32_t> g_opened {0};
std::atomic<uint32_t> g_failed {0};
std::atomic<uint32_t> g_ubers {0};
std::atomic<uint32_t> g_warped {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_entries {0};
std::atomic<uint32_t> g_waypoints {0};
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

auto CounterOf(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto Openings(Game* game) noexcept -> int32_t {
	const int32_t* counter = CounterOf(game, OpenKey);
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

auto IsUberLevel(int32_t level) noexcept -> bool {
	return std::find(std::begin(UberLevels), std::end(UberLevels), level) != std::end(UberLevels);
}

auto FountainDistance(Unit* player) noexcept -> int32_t {
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::rva::UnitCoords)(player, xy);
	return (xy[0] - FountainX) * (xy[0] - FountainX) + (xy[1] - FountainY) * (xy[1] - FountainY);
}

// OpenRedPortal's own spot search: false when it would find no free spot, which would use the map up for nothing
auto PortalSpotFree(Game* game, Unit* player) noexcept -> bool {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	if (room == nullptr) {
		return false;
	}
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, xy);
	void* found = nullptr;
	d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(FindFreeSpot)(
		game, room, xy, 3, 0x400, &found, 5, 100);
	return found != nullptr;
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
	} else if (IsUberLevel(level)) {
		const int32_t d2 = FountainDistance(player);
		if (d2 <= FountainUse) {
			return false;
		}
		std::snprintf(why, size, "not at the Blood Fountain (squared distance %d over %d)", d2, FountainUse);
	} else if (!PortalSpotFree(game, player)) {
		std::snprintf(why, size, "no free spot for the portal here");
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

// PD2 0x1037fb10 (flag 0): every pet but the golems goes; the hireling is killed
// shortcut: D2R's RemovePet kills every pet, where PD2 removes the summons without a death; upgrade if their corpses
// in town matter
auto RemovePets(Game* game, Unit* player) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* data = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitPlayerData)(player);
	void* pets = data != nullptr ? At<void*>(data, PlayerPets) : nullptr;
	int32_t removed = 0;
	for (int32_t type = 0; pets != nullptr && type < PetTypes; ++type) {
		const uint8_t* entry = type == KeptPetType ? nullptr
			: d2r::Engine<uint8_t* (*)(uint32_t, void*, int32_t) noexcept>(PetEntry)(d2r::DataFlag(game), pets, type);
		// bounded by the count before, in case a kill ever leaves the pet on the list
		for (int32_t n = entry != nullptr ? At<int32_t>(entry, PetCount) : 0; n > 0; --n) {
			Unit* pet = d2r::Engine<Unit* (*)(Game*, Unit*, int32_t, int32_t) noexcept>(FirstPet)(game, player, type, 1);
			if (pet == nullptr) {
				break;
			}
			const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(pet, __FILE__,
			                                                                                        __LINE__);
			d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemovePet)(game, player, id, 1);
			++removed;
		}
	}
	return removed;
}

// PD2 0x1037f4a0 (flag 1): each state off and its stat list freed, as RemoveSkillState does per state
auto StripStates(Game* game, Unit* player) noexcept -> int32_t {
	using namespace d2r::rva;
	int32_t stripped = 0;
	for (const int32_t state : StrippedStates) {
		void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(player, state);
		if (list == nullptr) {
			continue;
		}
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(ClearUnitState)(player, state, 0);
		d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(player, list);
		d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(d2r::DataFlag(game), list);
		++stripped;
	}
	return stripped;
}

// The argument block D2R's operate dispatcher 0x592c40 passes an operate function
struct ObjectOperate {
	Game* game;
	Unit* object;
	Unit* player;
	uint64_t seed;
	int32_t classId;
};

// How operate 15 removes a used portal: the room's clients are told, then the unit is freed
auto RemoveObject(Game* game, Unit* object) noexcept -> void {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(object);
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(object, __FILE__, __LINE__);
	d2r::Engine<void (*)(void*, int32_t, int32_t) noexcept>(RoomUnitRemoved)(room, UnitObject, id);
	d2r::Engine<void (*)(Game*, Unit*) noexcept>(RemoveUnit)(game, object);
	d2r::Engine<void (*)(void*) noexcept>(RoomChanged)(room);
}

// The tail of PD2 0x1038a680, and its portal entry: arriving in an uber level removes the level's `Waypoint` objects.
// shortcut: only the player's room and its neighbours are walked, where the warp puts a player (by the level's
// waypoint); walk every room of the level if a waypoint ever shows further off
auto RemoveWaypoints(Game* game, Unit* player, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	if (std::find(std::begin(NoWaypointLevels), std::end(NoWaypointLevels), level) == std::end(NoWaypointLevels)) {
		return;
	}
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	void** rooms = nullptr;
	uint32_t count = 0;
	if (room != nullptr) {
		d2r::Engine<void (*)(void*, void***, uint32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	}
	// picked first, so no unit is freed while its room's list is walked
	constexpr int Max = 16;
	Unit* found[Max] = {};
	int n = 0;
	for (uint32_t r = 0; rooms != nullptr && r < count; ++r) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[r]); unit != nullptr && n < Max;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			const auto* record = At<int32_t>(unit, UnitType) == UnitObject
				? d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(
					  d2r::DataFlag(game), At<int32_t>(unit, UnitClassId))
				: nullptr;
			if (record != nullptr && At<uint8_t>(record, ObjOperateFn) == OperateWaypoint) {
				found[n++] = unit;
			}
		}
	}
	for (int i = 0; i < n; ++i) {
		RemoveObject(game, found[i]);
	}
	g_waypoints += static_cast<uint32_t>(n);
	if (n > 0) {
		plog::Info("uber: %d waypoints removed in level %d", n, level);
	}
}

// PD2 0x103a9780: each entry heals the fight's bosses by 20% of their max life, capped at max
// shortcut: the bosses are found by class in the level, where PD2 heals the ones its phase scripts registered; use
// those once the fights' scripts are ported
auto HealBosses(Game* game, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	int32_t healed = 0;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* unit = At<Unit*>(game, GameMonsters + bucket * 8); unit != nullptr;
		     unit = At<Unit*>(unit, UnitListNext)) {
			const int32_t classId = At<int32_t>(unit, UnitClassId);
			if (std::find(std::begin(UberBosses), std::end(UberBosses), classId) == std::end(UberBosses) ||
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) != 0 || PlayerLevelId(unit) != level) {
				continue;
			}
			const int32_t max = stat(unit, StatMaxLife, 0);
			const int32_t life = std::min(max, stat(unit, StatLife, 0) + max / 5);
			d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(unit, StatLife, life, 0);
			++healed;
		}
	}
	return healed;
}

// PD2 0x1038a680: the user, then every other player in Harrogath at level 80+ within reach of the fountain, loses
// pets and buffs and is warped in. As in PD2, nobody else goes when the user does not arrive. Returns how many
// arrived.
// shortcut: pets go for everyone; PD2 removes them only when 0x1037c6a0 (not read yet) says so, read it if a pet
// ever stays in PD2
auto WarpGroup(Game* game, Unit* user, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	int32_t arrived = 0;
	const auto warp = [&](Unit* player) {
		const int32_t pets = RemovePets(game, player);
		const int32_t states = StripStates(game, player);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(WarpToLevel)(game, player, level, 0);
		const int32_t now = PlayerLevelId(player);
		plog::Info("uber: player warped to level %d (now in %d), %d pets removed, %d states stripped", level, now,
		           pets, states);
		if (now == level) {
			++arrived;
			RemoveWaypoints(game, player, level);
		}
	};
	// the others are picked before anyone moves, so a warp never runs while the client list is walked
	constexpr int MaxPlayers = 8;
	Unit* others[MaxPlayers] = {};
	int count = 0;
	for (auto* client = At<uint8_t*>(game, GameClients); client != nullptr && count < MaxPlayers;
	     client = At<uint8_t*>(client, ClientNext)) {
		Unit* player = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
		if (player != nullptr && player != user && PlayerLevelId(player) == Harrogath &&
		    d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(player, StatLevel, 0) >= MinCharLevel &&
		    FountainDistance(player) < FountainWarp) {
			others[count++] = player;
		}
	}
	warp(user);
	for (int i = 0; arrived > 0 && i < count; ++i) {
		warp(others[i]);
	}
	return arrived;
}

// PD2 0x10383060, tier 0: three grey portals to the level around the fountain. `near` is a room by the fountain.
// Each portal goes in `made`, null where none was made.
auto OpenGreyPortals(Game* game, void* near, int32_t level, Unit* (&made)[std::size(GreyPortalXY)]) noexcept -> int32_t {
	int32_t count = 0;
	for (size_t i = 0; i < std::size(GreyPortalXY); ++i) {
		const auto& xy = GreyPortalXY[i];
		made[i] = OpenPortalAt(game, near, GreyPortal, xy[0], xy[1], level);
		count += made[i] != nullptr ? 1 : 0;
	}
	return count;
}

// PD2's portal operate (77), in D2R's operate slot 60: the player goes to the portal's level. Into an uber level the
// area rules run first (PD2 0x103a9780): the entry is counted, the bosses heal, pets go and buffs are stripped. A grey
// portal is single use. The waypoints go on arrival.
// shortcut: Rathma's tier-0 re-entry at the stage's spot (section 3) needs the Rathma fight's stage byte; add it with
// that fight
auto OperatePortal(ObjectOperate* args) noexcept -> int32_t {
	using namespace d2r::rva;
	Game* game = args->game;
	Unit* player = args->player;
	const int32_t level = d2r::Engine<uint16_t (*)(Unit*) noexcept>(PortalLevel)(args->object);
	if (game == nullptr || player == nullptr) {
		return 0;
	}
	if (level <= 0 || level > 255) {
		plog::Warn("uber: portal %d leads nowhere (level %d)", args->classId, level);
		d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(ClientMessage)(player, CantEnter, player);
		return 0;
	}
	if (IsUberLevel(level)) {
		int32_t* entries = CounterOf(game, EntryKey);
		*entries += 1;
		const int32_t healed = HealBosses(game, level);
		const int32_t pets = RemovePets(game, player);
		const int32_t states = StripStates(game, player);
		plog::Info("uber: entry %d into level %d by portal: %d bosses healed, %d pets removed, %d states stripped (%u)",
		           *entries, level, healed, pets, states, ++g_entries);
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(WarpToLevel)(game, player, level, 0);
	const int32_t now = PlayerLevelId(player);
	if (now != level) {
		plog::Warn("uber: the portal to level %d left the player in %d", level, now);
		return 0;
	}
	if (args->classId == GreyPortal) {
		RemoveObject(game, args->object);
	}
	RemoveWaypoints(game, player, level);
	return 1;
}

auto OpenUber(Game* game, Unit* player, Unit* item, int32_t level) noexcept -> void {
	const int32_t tier = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		item, StatUberTier, 0);
	// the portals are made while the user still stands by the fountain, and taken back if the opening fails
	Unit* portals[std::size(GreyPortalXY)] = {};
	const int32_t made = tier == 0 ? OpenGreyPortals(game, d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(
	                                                           player), level, portals)
	                               : 0;
	const int32_t arrived = WarpGroup(game, player, level);
	if (arrived == 0) {
		for (Unit* portal : portals) {
			if (portal != nullptr) {
				RemoveObject(game, portal);
			}
		}
		plog::Warn("uber: nobody reached level %d (tier %d), %u failed", level, tier, ++g_failed);
		return;
	}
	*CounterOf(game, OpenKey) += 1;
	*CounterOf(game, TierKey) = tier;
	*CounterOf(game, EntryKey) = arrived;
	g_warped += static_cast<uint32_t>(arrived);
	plog::Info("uber: level %d open at tier %d, %d players in (%u opened)", level, tier, arrived, ++g_ubers);
	if (tier == 0) {
		g_portals += static_cast<uint32_t>(made);
		plog::Info("uber: %d re-entry portals by the fountain", made);
	}
}

auto SpellUse(Game* game, Unit* player, Unit* item, Unit* target) noexcept -> void {
	const uint8_t* record = g_gated && game != nullptr && player != nullptr && item != nullptr ? IsMap(game, item)
	                                                                                         : nullptr;
	if (record == nullptr) {
		g_useOriginal(game, player, item, target);
		return;
	}
	const int32_t level = MapLevel(game, player, item, record);
	if (IsUberLevel(level)) {
		OpenUber(game, player, item, level);
		return;
	}
	plog::Info("map: opening level %d", level);
	const int32_t opened = d2r::Engine<int32_t (*)(Game*, Unit*, uint32_t, int32_t, int32_t) noexcept>(
		d2r::rva::OpenRedPortal)(game, player, OpenKey, level, 1);
	if (opened != 0) {
		plog::Info("map: red portal to level %d, %u opened", level, ++g_opened);
		RouteMapStats(game, item, level);
	} else {
		plog::Warn("map: no red portal to level %d (D2R refused), %u failed", level, ++g_failed);
	}
}

}  // namespace

// Made as the portal maker 0x432ce0 makes one, but with no return portal; it finds the room holding (x, y) from `near`
auto OpenPortalAt(Game* game, void* near, int32_t row, int32_t x, int32_t y, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(d2r::DataFlag(game),
	                                                                                            row);
	using SpawnFn = Unit* (*)(int32_t, int32_t, int32_t, int32_t, Game*, void*, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	void* room = near != nullptr && record != nullptr
		? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(near, x, y)
		: nullptr;
	Unit* portal = room != nullptr ? d2r::Engine<SpawnFn>(SpawnUnit)(UnitObject, row, x, y, game, room, 1, 1, 0, 0)
	                               : nullptr;
	if (portal == nullptr) {
		plog::Warn("uber: no portal %d at (%d, %d)", row, x, y);
		return nullptr;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(portal, 1);
	const int32_t open = At<int32_t>(game, GameFrame) + 1 + static_cast<int32_t>(At<uint32_t>(record, ObjOpenFrames) >> 8);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, portal, UnitEventOpen, open, 0, 0, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(portal, level);
	return portal;
}

auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	// The use first: with only the check, D2R's use would take a map that passed and open nothing
	const bool use = context->InstallInlineHook(ItemSpellUse, ItemSpellUseEntry, sizeof(ItemSpellUseEntry), SpellUse,
	                                            &g_useOriginal);
	const bool check = use && context->InstallInlineHook(ItemSpellCheck, ItemSpellCheckEntry,
	                                                     sizeof(ItemSpellCheckEntry), SpellCheck, &g_checkOriginal);
	g_gated = check;
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool portal = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperatePortal) * 8,
	                                           Empty, sizeof(Empty),
	                                           reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperatePortal)));
	std::snprintf(report, reportSize, "item spell %d%s, portal operate %d%s", MapSpell,
	              check ? "" : use ? " FAILED (check bytes differ)" : " FAILED (use bytes differ)", ObjectOperatePortal,
	              portal ? "" : " FAILED (slot not empty)");
	return (check ? 1 : 0) + (portal ? 1 : 0);
}

auto MapOpenCount() noexcept -> int {
	return 2;
}

auto MapOpenCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "map checks %u, refused %u, opened %u, failed %u; uber areas opened %u, players warped %u, grey "
	              "portals %u, portal entries %u, waypoints removed %u",
	              g_checks.load(), g_refused.load(), g_opened.load(), g_failed.load(), g_ubers.load(), g_warped.load(),
	              g_portals.load(), g_entries.load(), g_waypoints.load());
}
