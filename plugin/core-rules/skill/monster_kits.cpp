// monster_kits
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace {

constexpr int32_t TorajanBossNest = 631;  // merged id

auto SrvDoTorajanNest(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;  // W36, below
// Lucion's kit (#487), below

}  // namespace

namespace pd2r::skills {

// --- Monster kits (W36; docs/research/skills/w36-monster-kits.md) ---------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 173 at 0x103b3520, cltdo 109 at 0x103abba0, srvdo 181 at 0x103b4cf0, srvst 67 at
// 0x103b6520 with its per-unit callback 0x1036d5a0.
std::atomic<uint32_t> g_eggSpawns {0};

std::atomic<uint32_t> g_eggSpawnClients {0};

std::atomic<uint32_t> g_nestSpawns {0};

std::atomic<uint32_t> g_nestFull {0};

std::atomic<uint32_t> g_cyclerStarts {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t MaggotEgg = 159;  // merged id (vanilla's)

// PoisonEggSpawn (PD2 srvdofunc 173): D2R's nova (srvdo 22) with the cast's skill, then D2R's maggot egg spawn (srvdo 84)
// as skill MaggotEgg at the cast's level.

// PoisonEggSpawn (PD2 srvdofunc 173): D2R's nova (srvdo 22) with the cast's skill, then D2R's maggot egg spawn (srvdo 84)
// as skill MaggotEgg at the cast's level.
auto SrvDoPoisonEggSpawn(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const int64_t nova = VanillaSlot<SrvDoFn>(SrvDoTable, d2r::rva::SrvDoNova)(game, unit, skillId, level);
	const int64_t eggs = VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoMaggotEgg)(game, unit, MaggotEgg, level);
	++g_eggSpawns;
	return eggs;
}

// Client half (PD2 cltdofunc 109, here 126): D2R's nova (cltdo 25), then MaggotEgg's (cltdo 46).

// Client half (PD2 cltdofunc 109, here 126): D2R's nova (cltdo 25), then MaggotEgg's (cltdo 46).
auto CltDoPoisonEggSpawn(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	VanillaSlot<CltDoFn>(CltDoTable, d2r::rva::CltDoNova)(unit, skillId, level);
	++g_eggSpawnClients;
	return VanillaSlot<CltDoFn>(CltDoTable, CltDoMaggotEgg)(unit, MaggotEgg, level);
}

// How many monsters each nest has spawned (PD2 keeps it in the nest's 1.13c AI data, +0x28 then +0x1c), by game and
// unit id. A game's frame only grows, so an entry seen at a later frame than now belongs to an earlier game at the same
// address and starts over.
// shortcut: entries are never removed, and a later game at the same address whose nest gets the same id and first
// casts at a later frame keeps the old count; move the count onto the unit if a D2R field for it turns up.

// How many monsters each nest has spawned (PD2 keeps it in the nest's 1.13c AI data, +0x28 then +0x1c), by game and
// unit id. A game's frame only grows, so an entry seen at a later frame than now belongs to an earlier game at the same
// address and starts over.
// shortcut: entries are never removed, and a later game at the same address whose nest gets the same id and first
// casts at a later frame keeps the old count; move the count onto the unit if a D2R field for it turns up.
struct NestCount {
	uint32_t count;
	int32_t frame;  // the game frame of the last cast
};

std::mutex g_nestMutex;

std::map<std::pair<const void*, uint32_t>, NestCount> g_nestCounts;

// TorajanBossNest (PD2 srvdofunc 181 -> router 190): D2R's Nest (srvdo 91) spawning one monster per cast, at most
// Param1 over the nest's life, each with no treasure class and no experience and sumoverlay.

// TorajanBossNest (PD2 srvdofunc 181 -> router 190): D2R's Nest (srvdo 91) spawning one monster per cast, at most
// Param1 over the nest's life, each with no treasure class and no experience and sumoverlay.
auto SrvDoTorajanNest(Game* game, Unit* unit, int32_t skillId, int32_t) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	const uint8_t* uskill = d2r::Engine<const uint8_t* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	if (skill == nullptr || uskill == nullptr) {
		return 0;
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemoveUnitEvent)(game, unit, 1, 0);
	if (At<int32_t>(unit, UnitType) != UnitMonster) {
		return 0;
	}
	const auto cap = At<uint32_t>(skill, SkParam1);
	std::lock_guard lock(g_nestMutex);
	NestCount& nest = g_nestCounts[{game, At<uint32_t>(unit, UnitIdField)}];
	const int32_t frame = At<int32_t>(game, GameFrame);
	if (frame < nest.frame) {
		nest.count = 0;  // another game at the same address
	}
	nest.frame = frame;
	uint32_t& count = nest.count;
	if (count >= cap) {
		++g_nestFull;
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(NestHold)(unit, 0);
	const int32_t x = At<int32_t>(uskill, USkillSpawnX), y = At<int32_t>(uskill, USkillSpawnY);
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(
		d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), x, y);
	if (room == nullptr || x == 0 || y == 0) {
		return 0;
	}
	d2r::Engine<void (*)(void*, int32_t, int32_t, int32_t, int32_t) noexcept>(ClearSpawnSpot)(room, x, y, 1, 0x100);
	const int32_t mon = At<int16_t>(uskill, USkillSpawnMon), mode = At<int32_t>(uskill, USkillSpawnMode);
	Unit* spawn = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster)(game, room, x, y, mon, mode, 1, 0);
	if (spawn == nullptr) {
		return 0;
	}
	++count;
	*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(spawn) + UnitMainFlags) |= UnitNoTreasure | UnitNoExperience;
	PutOverlay(spawn, At<int16_t>(skill, SkSumOverlay));
	++g_nestSpawns;
	return 1;
}


struct CyclerHeal {
	int32_t heal;  // whole points
	int32_t stat;  // 6 life or 8 mana
	int32_t healed;
	int32_t before, after, max;  // a player's stat in whole points, for the game check (#268)
};

// One unit in range (PD2's 0x1036d5a0): the stat + heal, at most its maximum.

// One unit in range (PD2's 0x1036d5a0): the stat + heal, at most its maximum.
auto CyclerHealOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* c = static_cast<CyclerHeal*>(area->arg);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t max = unitStat(victim, c->stat == StatMana ? StatMaxMana : StatMaxLife, 0);
	const int32_t before = unitStat(victim, c->stat, 0);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(
		victim, c->stat, std::min(before + c->heal * 256, max), 0);
	if (At<int32_t>(victim, UnitType) == 0) {  // a player
		c->before = before >> 8, c->after = unitStat(victim, c->stat, 0) >> 8, c->max = max >> 8;
	}
	++c->healed;
	return 1;
}

// Corpse/Vine Cycler start (PD2 srvstfunc 67, here 83): a monster takes its target corpse (state 118, as D2R's
// srvst 63) and, instead of 63's delayed missile, at once heals every unit within aurarangecalc (aurafilter) by
// calc1-calc2 of stat calc3 (6 life, 8 mana).

// Corpse/Vine Cycler start (PD2 srvstfunc 67, here 83): a monster takes its target corpse (state 118, as D2R's
// srvst 63) and, instead of 63's delayed missile, at once heals every unit within aurarangecalc (aurafilter) by
// calc1-calc2 of stat calc3 (6 life, 8 mana).
auto SrvStCycler(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr || At<int32_t>(unit, UnitType) != UnitMonster || skillId <= 0) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	Unit* corpse = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	if (skill == nullptr || corpse == nullptr || OwnedByPlayer(corpse) ||
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(corpse, StateCorpseTaken) != 0) {
		return 0;
	}
	// SetStateBit, as D2R's srvst 63: SetStateGfxFlag's bit is not the one UnitHasState reads, so a vine re-ate the same
	// corpse every cast (#378)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(corpse, StateCorpseTaken, 1);
	d2r::Engine<void (*)(Unit*) noexcept>(QueueUnitUpdate)(corpse);
	const auto calc = [&](int field) { return d2r::EvalCalc(game, unit, skill, field, skillId, level); };
	const int32_t radius = calc(SkAuraRangeCalc), low = calc(SkCalc1), high = calc(SkCalc2);
	CyclerHeal c {low, calc(SkCalc3), 0, -1, -1, -1};
	if (c.stat != StatLife && c.stat != StatMana) {
		return 0;  // PD2 too: the corpse stays taken
	}
	if (high > low) {
		c.heal += static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit),
		                                                static_cast<uint32_t>(high - low)));
	}
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), 0, 0};  // 0, 0: at the caster
	d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, At<uint32_t>(skill, SkAuraFilter), CyclerHealOne, &c, 0);
	d2r::Engine<void (*)(Unit*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(SendChargePacket)(
		unit, corpse, skillId, 1, 0, 0, 0);  // PD2 and D2R's srvst 63 both end with this packet
	++g_cyclerStarts;
	return 1;
}

// One monster for PD2's summons (0x1038a170 with a spot search, 0x1038a0d0 without), mode 1 (neutral), at x/y in the
// room holding x/y (room follows it). D2R native calls: PluginSDK has no server-side monster spawn (its Encounters
// service only runs the console's test spawn for a client), so this calls D2R's RoomAtXY, FindFreeSpot and SpawnMonster.
// Flags 0x42 are what D2R's own spawns and core-events pass to SpawnMonster; PD2's 0xc is not a spawn flag but an
// argument of its spot search (0x10389c20), and its spawn (0x1038a060) passes fixed flags to D2Game.
// shortcut: D2R's portal spot search stands in for PD2's 0x10389c20, as core-events' map events do; PD2's 0x3000000 unit
// flags and its D2Game call on each spawn (0x103a1eb0) are left out, as their meaning is unread
// FindFreeSpot's other arguments, as D2R's red portal (OpenRedPortal) passes them; the names are guesses, unread

// One monster for PD2's summons (0x1038a170 with a spot search, 0x1038a0d0 without), mode 1 (neutral), at x/y in the
// room holding x/y (room follows it). D2R native calls: PluginSDK has no server-side monster spawn (its Encounters
// service only runs the console's test spawn for a client), so this calls D2R's RoomAtXY, FindFreeSpot and SpawnMonster.
// Flags 0x42 are what D2R's own spawns and core-events pass to SpawnMonster; PD2's 0xc is not a spawn flag but an
// argument of its spot search (0x10389c20), and its spawn (0x1038a060) passes fixed flags to D2Game.
// shortcut: D2R's portal spot search stands in for PD2's 0x10389c20, as core-events' map events do; PD2's 0x3000000 unit
// flags and its D2Game call on each spawn (0x103a1eb0) are left out, as their meaning is unread
// FindFreeSpot's other arguments, as D2R's red portal (OpenRedPortal) passes them; the names are guesses, unread
constexpr uint32_t SpotSize = 3, SpotMask = 0x400;

constexpr uint64_t SpotRings = 5;

constexpr int32_t SpotTries = 100;

constexpr int16_t SpawnFlags = 0x42;


auto SummonAt(Game* game, void*& room, int32_t x, int32_t y, int32_t mon, bool search = true) noexcept -> Unit* {
	using namespace d2r::rva;
	void* at = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(room, x, y);
	if (at == nullptr) {
		return nullptr;
	}
	room = at;
	int32_t xy[2] = {x, y};
	void* found = at;
	if (search) {
		found = nullptr;
		d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(
			FindFreeSpot)(game, room, xy, SpotSize, SpotMask, &found, SpotRings, SpotTries);
	}
	return found == nullptr ? nullptr
		: d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(SpawnMonster)(
			game, found, xy[0], xy[1], mon, 1, -1, SpawnFlags);
}

// RathmaSummon (PD2 srvdofunc 179, ProjectDiablo.dll 0x103b42a0): what Rathma's AI picked (stat 447, here 500, which
// core-events' rathma_ai.cpp sets before the cast) spawns, around the caster at +-20 unless noted:
//   30 void golems (937): 10 / 8 / 6 / 4 below 31 / 51 / 71 / 101 % life, 40 failed spots at most
//   31 four blood golems (938) at the stage's spot (0x103567d0) -25 x, -25 y, +23 x, +25 y: only the first takes the
//      spot search (0x1038a170), the other three spawn on their cell (0x1038a0d0, forced)
//   32 one totem (939), 20 failed spots at most
//   33 void knights cold, fire, poison, then phys, fire, cold below 31 %, phys, poison below 51 %, fire to 70 %: each
//      takes the first free spot, 40 failed spots for the whole list
// Pick 30 is dead code in PD2 as well: its Rathma AI (0x10358510) never enters state 0x1e, so the port's never does.
// PD2 spawns no spire here.

// RathmaSummon (PD2 srvdofunc 179, ProjectDiablo.dll 0x103b42a0): what Rathma's AI picked (stat 447, here 500, which
// core-events' rathma_ai.cpp sets before the cast) spawns, around the caster at +-20 unless noted:
//   30 void golems (937): 10 / 8 / 6 / 4 below 31 / 51 / 71 / 101 % life, 40 failed spots at most
//   31 four blood golems (938) at the stage's spot (0x103567d0) -25 x, -25 y, +23 x, +25 y: only the first takes the
//      spot search (0x1038a170), the other three spawn on their cell (0x1038a0d0, forced)
//   32 one totem (939), 20 failed spots at most
//   33 void knights cold, fire, poison, then phys, fire, cold below 31 %, phys, poison below 51 %, fire to 70 %: each
//      takes the first free spot, 40 failed spots for the whole list
// Pick 30 is dead code in PD2 as well: its Rathma AI (0x10358510) never enters state 0x1e, so the port's never does.
// PD2 spawns no spire here.
constexpr int32_t StatRathmaPick = 500;               // mon_cooldown2 (PD2 447)

constexpr uint32_t RathmaStageKey = 0x55425247;       // 'UBRG', core-events' stage counter (PD2 game +0x262a)

constexpr int32_t VoidGolem = 954, BloodGolem = 955, RathmaTotem = 956;  // merged ids (PD2 937-939)
// merged ids of the void knights (PD2 968, 969, 1106, 1107)

// merged ids of the void knights (PD2 968, 969, 1106, 1107)
constexpr int32_t KnightPoison = 985, KnightFire = 986, KnightCold = 1123, KnightPhys = 1124;

constexpr int32_t RathmaSpread = 20;  // the spots: caster +-20

// PD2 0x103567d0: the stage's spot. PD2's case 31 returns 0 on a zero spot, which 0x103567d0 gives only with no game,
// and the cast already returns on that

// PD2 0x103567d0: the stage's spot. PD2's case 31 returns 0 on a zero spot, which 0x103567d0 gives only with no game,
// and the cast already returns on that
constexpr auto RathmaSpot(int32_t stage) noexcept -> std::pair<int32_t, int32_t> {
	return stage < 3 ? std::pair {0x476c, 0x35bf} : stage < 5 ? std::pair {0x47d3, 0x394b} : std::pair {0x4e9a, 0x1403};
}

// PD2's case 30 count by life percent

// PD2's case 30 count by life percent
constexpr auto VoidGolemCount(int32_t life) noexcept -> int32_t {
	return life < 31 ? 10 : life < 51 ? 8 : life < 71 ? 6 : 4;
}

static_assert(VoidGolemCount(30) == 10 && VoidGolemCount(50) == 8 && VoidGolemCount(70) == 6 && VoidGolemCount(71) == 4);

// PD2's case 33 list by life percent

// PD2's case 33 list by life percent
auto VoidKnightList(int32_t life, int32_t* out) noexcept -> int {
	int n = 0;
	for (int32_t id : {KnightCold, KnightFire, KnightPoison}) {
		out[n++] = id;
	}
	if (life < 31) {
		for (int32_t id : {KnightPhys, KnightFire, KnightCold}) {
			out[n++] = id;
		}
	} else if (life < 51) {
		out[n++] = KnightPhys;
		out[n++] = KnightPoison;
	} else if (life <= 70) {
		out[n++] = KnightFire;
	}
	return n;
}


auto SrvDoRathmaSummon(Game* game, Unit* unit, int32_t skillId, int32_t) noexcept -> int64_t {
	using namespace d2r::rva;
	// shortcut: PD2 also needs the game's area type to be Rathma's (game +0x1df4 == 0x2b), which the port does not keep
	// (docs/deviations.md); only the Rathma group has the skill
	int32_t x = 0, y = 0;
	if (game == nullptr || unit == nullptr || d2r::api.getSkillRecord(d2r::DataFlag(game), skillId) == nullptr ||
	    !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	if (room == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t life = LifePercent(unitStat(unit, StatLife, 0), unitStat(unit, StatMaxLife, 0));
	const int32_t pick = unitStat(unit, StatRathmaPick, 0);
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	// shortcut: the spots roll the caster's seed, where PD2 calls the C runtime's rand(); the spread is the same
	const auto near = [&](int32_t c) {
		return c - RathmaSpread + static_cast<int32_t>(d2r::RandomBelow(seed, 2 * RathmaSpread));
	};
	int32_t wanted = 0, spawned = 0, failed = 0;
	// count of mon near the caster, giving up after tries failed spots
	const auto spawnNear = [&](int32_t mon, int32_t count, int32_t tries) {
		for (wanted = count; spawned < wanted && failed < tries;) {
			const int32_t sx = near(x), sy = near(y);
			SummonAt(game, room, sx, sy, mon) != nullptr ? ++spawned : ++failed;
		}
	};
	switch (pick) {
	case 30:
		spawnNear(VoidGolem, VoidGolemCount(life), 40);
		break;
	case 32:
		spawnNear(RathmaTotem, 1, 20);
		break;
	case 31: {
		uint32_t key = RathmaStageKey;
		const auto [cx, cy] = RathmaSpot(*d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(GameCounter)(
			reinterpret_cast<uint8_t*>(game) + GameCounters, &key));
		wanted = 4;
		bool search = true;  // the first golem only
		for (const auto& [sx, sy] : {std::pair {cx - 25, cy}, {cx, cy - 25}, {cx + 23, cy}, {cx, cy + 25}}) {
			SummonAt(game, room, sx, sy, BloodGolem, search) != nullptr ? ++spawned : ++failed;
			search = false;
		}
		break;
	}
	case 33: {
		int32_t list[6] = {};
		wanted = VoidKnightList(life, list);
		for (int i = 0; i < wanted; ++i) {
			for (; failed < 40; ++failed) {
				const int32_t sx = near(x), sy = near(y);
				if (SummonAt(game, room, sx, sy, list[i]) != nullptr) {
					++spawned;
					break;
				}
			}
		}
		break;
	}
	default:
		return 0;  // PD2 too
	}
	return 1;
}

// UberDiabSummon (PD2 srvdofunc 177, ProjectDiablo.dll 0x103b3e10): Dclone's skeleton waves around his arena's centre.
// Each wave whose threshold (Param4-6) is above the caster's life percent spawns calc1-3 monsters: the skill's summon,
// then summon + 1, then summon + 2 + rand % 4. Dclone's AI casts it at phases 1, 3 and 5 (#470).
// The square is PD2's 50x50 around its fixed arena centre (15129, 6548); ours follows the `centre` spot of Cartographer's
// Dclone arena (#958), as the AI's does, since the arena is not at PD2's coordinates (#1386).

// UberDiabSummon (PD2 srvdofunc 177, ProjectDiablo.dll 0x103b3e10): Dclone's skeleton waves around his arena's centre.
// Each wave whose threshold (Param4-6) is above the caster's life percent spawns calc1-3 monsters: the skill's summon,
// then summon + 1, then summon + 2 + rand % 4. Dclone's AI casts it at phases 1, 3 and 5 (#470).
// The square is PD2's 50x50 around its fixed arena centre (15129, 6548); ours follows the `centre` spot of Cartographer's
// Dclone arena (#958), as the AI's does, since the arena is not at PD2's coordinates (#1386).
constexpr int32_t WaveTries = 40;  // PD2 gives up a wave after 40 spots with no spawn


auto SrvDoUberDiabSummon(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint8_t* skill = game != nullptr && unit != nullptr ? d2r::api.getSkillRecord(d2r::DataFlag(game), skillId)
	                                                          : nullptr;
	const int32_t summon = skill != nullptr ? At<int16_t>(skill, SkSummon) : -1;
	placements::Spot centre{};
	if (summon < 0 || !ArenaSpotOf("dclone", "centre", &centre)) {
		return 0;  // no arena, nothing is invented
	}
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(
		d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), centre.x, centre.y);
	if (room == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t life = LifePercent(unitStat(unit, StatLife, 0), unitStat(unit, StatMaxLife, 0));
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	const int32_t counts[] = {d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level),
	                          d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level),
	                          d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level)};
	const int32_t below[] = {At<int32_t>(skill, SkParam4), At<int32_t>(skill, SkParam5), At<int32_t>(skill, SkParam6)};
	int32_t spawned[3] = {};
	for (int wave = 0; wave < 3; ++wave) {
		for (int32_t failed = 0; life < below[wave] && spawned[wave] < counts[wave] && failed < WaveTries;) {
			// PD2's spot: rand % 50 + 0x3b00 by rand % 50 + 0x197b, then its free-spot search
			const int32_t sx = centre.x - 25 + static_cast<int32_t>(d2r::RandomBelow(seed, 50));
			const int32_t sy = centre.y - 25 + static_cast<int32_t>(d2r::RandomBelow(seed, 50));
			const int32_t mon = wave < 2 ? summon + wave : summon + 2 + static_cast<int32_t>(d2r::RandomBelow(seed, 4));
			SummonAt(game, room, sx, sy, mon) == nullptr ? ++failed : ++spawned[wave];
		}
	}
	// every cast: Dclone casts it three times a fight, and Sample would log only the first
	return 1;
}

// --- UberDiabRun start (PD2 srvstfunc 71) ---------------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvst 71 at 0x103b6760. D2R's DiabRun start (srvst 54, 0x57dc60) takes the skill's
// target unit and refuses the cast without one; PD2's takes the path's target unit, aims the path at its spot, and
// starts without one too, keeping the point the caster's AI set (Dclone's run to the arena centre, #471).


}  // namespace

PD2R_SLOT(59, SrvDo, 173, static_cast<SrvDoFn>(SrvDoPoisonEggSpawn), "poison egg spawn")
PD2R_SLOT(60, CltDo, 126, static_cast<CltDoFn>(CltDoPoisonEggSpawn), "poison egg spawn")
PD2R_SLOT(61, SrvSt, 83, static_cast<SrvDoFn>(SrvStCycler), "cycler start")
PD2R_SLOT(64, SrvDo, 177, static_cast<SrvDoFn>(SrvDoUberDiabSummon), "uber diablo summon")
PD2R_SLOT(65, SrvDo, 179, static_cast<SrvDoFn>(SrvDoRathmaSummon), "rathma summon")
PD2R_ROUTE(49, TorajanBossNest, SrvDoTorajanNest)
