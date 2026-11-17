// PD2's uber entry split out of map_open.cpp (#943), unchanged: opening an uber area at the Blood Fountain, the portal
// entry's area rules (heal, pets, buffs), the grey re-entry portals, the boss rooms kept and the uber levels' missing
// waypoints. The ubers lane's (docs/uber-bosses.md); map_open.cpp keeps the item spell hooks and calls these.
#include "uber_entry.h"

#include "ai_hooks.h"
#include "arena_spots.h"
#include "log.h"
#include "placements.h"
#include "rathma.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t Hell = 2;
constexpr int32_t Harrogath = 109;
constexpr int32_t UnitObject = 2;  // D2's unit type of an object
constexpr int32_t MinCharLevel = 80;
constexpr int32_t StatLevel = 12;      // itemstatcost level
// Uber keys: Diablo Clone `dcma`, Rathma `rtma`, Lucion `luca`, Uber Tristram `ubtm` (PD2 levels 137, 161, 188, 185
// in the port's ids)
// The levels are bound in plugin/placements/ (the uber flag marks these four)
auto UberTristram() noexcept -> int32_t {
	return placements::LevelOf("tristram");
}
// PD2 0x1037d840 is a squared distance to the Blood Fountain (5074, 5114): the user must be within 0x95 of it, and
// the warp takes every player under 0x96
constexpr int32_t FountainUse = 0x95, FountainWarp = 0x96;
constexpr int32_t StatUberTier = 370;  // uber_difficulty (PD2 185): the key's tier, 0-2
// The game counter keys of the open uber fight: its tier (PD2 game +0x2629) and entries (+0x2628)
constexpr uint32_t TierKey = 0x55425254;   // 'UBRT'
constexpr uint32_t EntryKey = 0x55425245;  // 'UBRE'
// PD2's three tier-0 re-entry portals (object 595 `xactportal_grey`, the port's 598) around the fountain
constexpr int32_t GreyPortal = 598;
constexpr const char* GreyPortalIds[] = {"grey0", "grey1", "grey2"};  // placements/uber_entry.cpp
constexpr int32_t CantEnter = 0x14;  // operate 15's refusal message
// The bosses PD2's entry heals by 20% of max life (its set 0x105deec4): Dclone, Rathma and Mendeln and their clones,
// Lucion, in the port's monstats ids
constexpr int32_t UberBosses[] = {806, 950, 951, 952, 953, 1129};
constexpr int32_t MapEventState = 259;  // states `mapevent` (PD2 213)
// PD2 removes the waypoints on arriving in its 137, 161-163 and 185 (and D2R's Uber Tristram 136, turned off)
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


// The uber keys' own item spell (#951): Cartographer owns the item spell dispatchers and the map spell 16, and a key
// that took 16 reached no handler, so the keys (misc pSpell 19 in misc.txt) use D2R's free entry 19
// {check 0, use 0, keep 0}. Both of its handler indexes are slot 0, D2R's `mov al,1; ret` stub in the check table and
// the use table; the slots are written here (no hook), and every other item that reaches slot 0 passes (it did before).
constexpr int32_t UberKeySpell = 19, RiteOfPassage = 39;
constexpr int32_t CantDoThat = 0x15;
constexpr uint64_t ItemSpellCheckTable = 0x1d3ea30;  // 11 check handlers: uint64 fn(game, player, item)
constexpr uint64_t KeyStubOriginal = 0x77ee0;       // slot 0 of both tables
std::atomic<uint32_t> g_keyChecks {0};
std::atomic<uint32_t> g_keyRefused {0};
std::atomic<uint32_t> g_keyUses {0};

std::atomic<uint32_t> g_ubers {0};
std::atomic<uint32_t> g_warped {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_entries {0};
std::atomic<uint32_t> g_waypoints {0};
std::atomic<uint32_t> g_bossRoomsKept {0};
int32_t (*g_releasableOriginal)(void* act, void* room) noexcept = nullptr;

auto FountainDistance(Unit* player) noexcept -> int32_t {
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::rva::UnitCoords)(player, xy);
	const auto fountain = placements::SpotOf("harrogath", "fountain");
	return (xy[0] - fountain.x) * (xy[0] - fountain.x) + (xy[1] - fountain.y) * (xy[1] - fountain.y);
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
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(player, state, 0);
		d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(player, list);
		d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(d2r::DataFlag(game), list);
		++stripped;
	}
	return stripped;
}

// How operate 15 removes a used portal: the room's clients are told, then the unit is freed. Returns its room.
auto RemoveObject(Game* game, Unit* object) noexcept -> void* {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(object);
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(object, __FILE__, __LINE__);
	d2r::Engine<void (*)(void*, int32_t, int32_t) noexcept>(RoomUnitRemoved)(room, UnitObject, id);
	d2r::Engine<void (*)(Game*, Unit*) noexcept>(RemoveUnit)(game, object);
	return room;
}

// Operate 15 then clears its room's portal mark; the grey portals share a room, so it is cleared with the last one
auto RemoveGreyPortal(Game* game, Unit* portal) noexcept -> void {
	using namespace d2r::rva;
	void* room = RemoveObject(game, portal);
	for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(room); unit != nullptr;
	     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
		if (At<int32_t>(unit, UnitType) == UnitObject && At<int32_t>(unit, UnitClassId) == GreyPortal) {
			return;
		}
	}
	d2r::Engine<void (*)(void*, int32_t) noexcept>(RoomPortalMark)(room, 1);
}

// PD2 0x103a9780: each entry heals the fight's bosses by 20% of their max life, capped at max. They are still in the
// game because KeepBossRoom keeps their rooms.
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
			const int32_t before = stat(unit, StatLife, 0);
			const int32_t life = std::min(max, before + max / 5);
			d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(unit, StatLife, life, 0);
			plog::Info("uber: boss %d healed, life %d -> %d of %d", classId, before >> 8, life >> 8, max >> 8);
			++healed;
		}
	}
	return healed;
}

// PD2 0x102fc6c0, on D2Game's room-release check: a room holding a live uber boss is never released, so the boss keeps
// its life between entries and HealBosses finds it. A hook: no SDK service covers room lifetime, and a room mark
// (RoomPortalMark) would stay on the room the boss walks out of.
// The boss is marked alone too: its next think starts PD2's 15-tick idle window (game+0x26fe, ai_hooks.h).
auto KeepBossRoom(void* act, void* room) noexcept -> int32_t {
	using namespace d2r::rva;
	for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(room); unit != nullptr;
	     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
		// also an Outer Void boss (state `mapevent`): PD2 puts its list back when its room comes back (0x10371520 keeps
		// the state in D2Game's monster store, 0x10371590 remakes the list); keeping the room keeps the list
		if (At<int32_t>(unit, UnitType) == UnitMonster &&
		    (std::find(std::begin(UberBosses), std::end(UberBosses), At<int32_t>(unit, UnitClassId)) != std::end(UberBosses) ||
		     d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(unit, MapEventState) != 0) &&
		    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
			MarkBossAlone(unit);
			if (g_bossRoomsKept++ == 0) {
				plog::Info("uber: boss %d keeps its room", At<int32_t>(unit, UnitClassId));
			}
			return 0;
		}
	}
	return g_releasableOriginal(act, room);
}

// D2R's warp lands a player at the level's default spawn, which in the Diablo Clone, Lucion and Rathma arenas is by
// the boss (#1260): the arena's `arrival` spot moves them clear of it. No spot or no room: left as is.
auto PlaceAtArrival(Game* game, Unit* player, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	const placements::Binding* bound = ArenaReady(level) ? placements::BindingAt(level) : nullptr;
	placements::Spot at{0, 0};
	void* near = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	if (bound == nullptr || near == nullptr || !ArenaSpotOf(bound->encounter, "arrival", &at)) {
		return;
	}
	int32_t before[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, before);
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(near, at.x, at.y);
	if (room == nullptr) {
		plog::Warn("uber: no room at %s's arrival (%d, %d), player left at (%d, %d)", bound->encounter, at.x, at.y,
		           before[0], before[1]);
		return;
	}
	d2r::Engine<void (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, int32_t) noexcept>(TeleportUnit)(
		game, player, room, at.x, at.y, 0, 0);
	int32_t after[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, after);
	plog::Info("uber: %s arrival moved from (%d, %d) to (%d, %d)", bound->encounter, before[0], before[1], after[0],
	           after[1]);
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
		if (PlayerLevelId(player) == level) {
			PlaceAtArrival(game, player, level);
		}
		const int32_t now = PlayerLevelId(player);
		plog::Info("uber: player warped to level %d (now in %d), %d pets removed, %d states stripped", level, now,
		           pets, states);
		arrived += now == level ? 1 : 0;
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
auto OpenGreyPortals(Game* game, void* near, int32_t level, Unit* (&made)[std::size(GreyPortalIds)]) noexcept -> int32_t {
	int32_t count = 0;
	for (size_t i = 0; i < std::size(GreyPortalIds); ++i) {
		const auto xy = placements::SpotOf("harrogath", GreyPortalIds[i]);
		made[i] = OpenPortalAt(game, near, GreyPortal, xy.x, xy.y, level);
		count += made[i] != nullptr ? 1 : 0;
	}
	return count;
}
}  // namespace


auto OpenUber(Game* game, Unit* player, Unit* item, int32_t level) noexcept -> void {
	const int32_t tier = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		item, StatUberTier, 0);
	// the portals are made while the user still stands by the fountain, and taken back if the opening fails
	Unit* portals[std::size(GreyPortalIds)] = {};
	const int32_t made = tier == 0 ? OpenGreyPortals(game, d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(
	                                                           player), level, portals)
	                               : 0;
	const int32_t arrived = WarpGroup(game, player, level);
	if (arrived == 0) {
		for (Unit* portal : portals) {
			if (portal != nullptr) {
				RemoveGreyPortal(game, portal);
			}
		}
		plog::Warn("uber: nobody reached level %d (tier %d), %u failed", level, tier, ++g_openFailed);
		return;
	}
	*OpenCounter(game, OpenKey) += 1;
	*OpenCounter(game, TierKey) = tier;
	// PD2 0x10383060 keeps the arrival count in game +0x2628, but sets it to 0 for Uber Tristram
	*OpenCounter(game, EntryKey) = level == UberTristram() ? 0 : arrived;
	g_warped += static_cast<uint32_t>(arrived);
	// stock areas (Vanilla mini-ubers, D2R's Tristram) and the authored private ones open through this same path
	const placements::Binding* bound = placements::BindingAt(level);
	plog::Info("uber: level %d (%s, source %d) open at tier %d, %d players in (%u opened)", level,
	           bound != nullptr ? bound->encounter : "?", bound != nullptr ? static_cast<int>(bound->source) : -1, tier,
	           arrived, ++g_ubers);
	if (tier == 0) {
		g_portals += static_cast<uint32_t>(made);
		plog::Info("uber: %d re-entry portals by the fountain", made);
	}
}



namespace {

auto KeyRecord(Game* game, Unit* item) noexcept -> const uint8_t* {
	using namespace d2r::rva;
	const int32_t row = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitClassOf)(item, __FILE__,
	                                                                                               __LINE__);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord)(d2r::DataFlag(game), row);
	return record != nullptr && At<int32_t>(record, ItemPSpell) == UberKeySpell ? record : nullptr;
}

auto Openings(Game* game) noexcept -> int32_t {
	const int32_t* counter = OpenCounter(game, OpenKey);
	return counter != nullptr ? *counter : 0;
}

auto RiteDone(Unit* player) noexcept -> bool {
	using namespace d2r::rva;
	const uint8_t* data = d2r::Engine<const uint8_t* (*)(Unit*) noexcept>(UnitPlayerData)(player);
	void* quests = data != nullptr ? At<void*>(data, PlayerQuests + Hell * 8) : nullptr;
	return quests != nullptr && d2r::Engine<int32_t (*)(void*, int32_t, int32_t) noexcept>(QuestFlag)(
	                                quests, RiteOfPassage, 0) != 0;
}

// The gates of PD2's key use (0x1037d840), in its order, as map_open.cpp's Refusal runs them for a map, plus the
// destination: a level no placement binds as an uber fight (a reserved private id before its author flips the
// binding, or a stray `len`) is refused before anything else, so no key is spent on an unfinished area.
auto KeyRefusal(Game* game, Unit* player, int32_t level, char* why, size_t size) noexcept -> bool {
	const int32_t charLevel = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		player, StatLevel, 0);
	const int32_t where = PlayerLevelId(player);
	if (!placements::IsUberLevel(level)) {
		std::snprintf(why, size, "level %d is not a finished uber area", level);
	} else if (placements::BindingAt(level)->source == placements::Source::Private && !ArenaReady(level)) {
		std::snprintf(why, size, "level %d: Cartographer did not build its arena (no cartographer.arena.v1 or no arenas.txt row)", level);
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
		return UberFountainRefusal(player, why, size);
	}
	return true;
}

auto KeyLevel(Game* game, Unit* player, Unit* item, const uint8_t* record) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(uint32_t, Unit*, Unit*, uint32_t) noexcept>(d2r::rva::ItemCalc)(
		d2r::DataFlag(game), player, item, At<uint32_t>(record, d2r::rva::ItemLenCalc));
}

// Item spell check slot 0: a refused key stays in the inventory and the player gets D2R's "can't do that"
auto KeyCheck(Game* game, Unit* player, Unit* item) noexcept -> uint64_t {
	const uint8_t* record = game != nullptr && player != nullptr && item != nullptr ? KeyRecord(game, item) : nullptr;
	if (record == nullptr) {
		return 1;
	}
	++g_keyChecks;
	const int32_t level = KeyLevel(game, player, item, record);
	char why[96];
	if (KeyRefusal(game, player, level, why, sizeof(why))) {
		plog::Info("uber: key for level %d refused: %s (%u refused)", level, why, ++g_keyRefused);
		d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(d2r::rva::ClientMessage)(player, CantDoThat, player);
		return 0;
	}
	return 1;
}

// Item spell use slot 0: the check has passed, so the key opens its area (entry 19's keep is 0: D2R then uses it up)
auto KeyUse(Game* game, Unit* player, Unit* item) noexcept -> uint8_t {
	const uint8_t* record = game != nullptr && player != nullptr && item != nullptr ? KeyRecord(game, item) : nullptr;
	if (record != nullptr) {
		++g_keyUses;
		OpenUber(game, player, item, KeyLevel(game, player, item, record));
	}
	return 1;
}

}  // namespace

auto InstallUberKey(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const uint64_t expected = d2r::imageBase + KeyStubOriginal;
	using CheckFn = uint64_t (*)(Game*, Unit*, Unit*) noexcept;
	using UseFn = uint8_t (*)(Game*, Unit*, Unit*) noexcept;
	const bool check = context->PatchWriteU64(ItemSpellCheckTable, &expected, sizeof(expected),
	                                          reinterpret_cast<uint64_t>(static_cast<CheckFn>(KeyCheck)));
	const bool use = context->PatchWriteU64(ItemSpellUseTable, &expected, sizeof(expected),
	                                        reinterpret_cast<uint64_t>(static_cast<UseFn>(KeyUse)));
	std::snprintf(report, reportSize, "uber key item spell %d, check slot 0%s, use slot 0%s", UberKeySpell,
	              check ? "" : " FAILED (the slot differs)", use ? "" : " FAILED (the slot differs)");
	return (check ? 1 : 0) + (use ? 1 : 0);
}

auto UberKeyCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "uber keys: checked %u, refused %u, used %u", g_keyChecks.load(), g_keyRefused.load(),
	              g_keyUses.load());
}

auto IsUberLevel(int32_t level) noexcept -> bool {
	return placements::IsUberLevel(level);
}

auto UberFountainRefusal(Unit* player, char* why, size_t size) noexcept -> bool {
	const int32_t d2 = FountainDistance(player);
	if (d2 <= FountainUse) {
		return false;
	}
	std::snprintf(why, size, "not at the Blood Fountain (squared distance %d over %d)", d2, FountainUse);
	return true;
}

auto UberPortalBegin(ObjectOperate* args, int32_t level, int32_t* result) noexcept -> bool {
	using namespace d2r::rva;
	Game* game = args->game;
	Unit* player = args->player;
	if (game == nullptr || player == nullptr) {
		*result = 0;
		return true;
	}
	if (level <= 0 || level > 255) {
		plog::Warn("uber: portal %d leads nowhere (level %d)", args->classId, level);
		d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(ClientMessage)(player, CantEnter, player);
		*result = 0;
		return true;
	}
	RathmaMoveReentry(game, player, PlayerLevelId(player), level);
	if (level == PlayerLevelId(player) && RathmaArenaPortal(game, player, level)) {
		*result = 1;
		return true;
	}
	return false;
}

auto UberPortalFinish(ObjectOperate* args, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	Game* game = args->game;
	Unit* player = args->player;
	// PD2 relinks the grey portals, so their level is the exit's too
	int32_t to = level;
	int32_t exit[2] = {};
	const bool moved = args->classId == GreyPortal && RathmaReentry(game, to, exit);
	if (IsUberLevel(level) && level != UberTristram()) {
		int32_t* entries = OpenCounter(game, EntryKey);
		*entries += 1;
		const int32_t healed = HealBosses(game, to);
		const int32_t pets = RemovePets(game, player);
		const int32_t states = StripStates(game, player);
		plog::Info("uber: entry %d into level %d by portal: %d bosses healed, %d pets removed, %d states stripped (%u)",
		           *entries, to, healed, pets, states, ++g_entries);
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(WarpToLevel)(game, player, to, 0);
	const int32_t now = PlayerLevelId(player);
	if (now != to) {
		plog::Warn("uber: the portal to level %d left the player in %d", to, now);
		return 0;
	}
	if (moved) {
		RathmaPlaceAt(game, player, exit);
	}
	if (args->classId == GreyPortal) {
		RemoveGreyPortal(game, args->object);
	}
	return 1;
}


// PD2 removes the `Waypoint` objects (operate 23) from the rooms around a player arriving in an uber level (tail of
// 0x1038a680, and its portal entry). D2R spawns the arrival rooms' presets after the warp returns, so a removal there
// finds nothing (#466): the waypoint preset is refused at spawn instead, which leaves the same level.
auto NoUberWaypoint(Game* game, void* room, const uint8_t* preset) noexcept -> bool {
	using namespace d2r::rva;
	if (At<int32_t>(preset, PresetType) != UnitObject) {
		return false;
	}
	const int32_t level = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
	if (!placements::NoWaypointLevel(level)) {
		return false;
	}
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(
		d2r::DataFlag(game), At<int32_t>(preset, PresetId));
	if (record == nullptr || At<uint8_t>(record, ObjOperateFn) != OperateWaypoint) {
		return false;
	}
	plog::Info("uber: waypoint %d not spawned in level %d (%u so far)", At<int32_t>(preset, PresetId), level,
	           ++g_waypoints);
	return true;
}

auto InstallBossRoomKeep(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	return context->InstallInlineHook(RoomReleasable, RoomReleasableEntry, sizeof(RoomReleasableEntry), KeepBossRoom,
	                                  &g_releasableOriginal);
}

auto UberEntryCounts() noexcept -> UberEntryStats {
	return {g_ubers.load(), g_warped.load(), g_portals.load(), g_entries.load(), g_waypoints.load(), g_bossRoomsKept.load()};
}
