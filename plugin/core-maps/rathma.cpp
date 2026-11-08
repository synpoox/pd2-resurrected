// The Rathma fight's stages outside the AI (docs/research/pd2-uber-bosses-dll.md, sections 5 and 6).
//
// Room activation (ProjectDiablo.dll 0x1036fef0): the first room to activate in each Rathma level sets the stage
// (0 -> 1 in the first level, 2 -> 3 in the second, 4 -> 5 in the third) and spawns that stage's bosses (Mendeln,
// then Rathma, then both clones) at fixed spots, with a ghostly portal that takes the group into the arena (PD2 links
// it to an `empty` marker by the bosses). No room of the arena (80 x 80 subtiles about the bosses' spot) spawns its
// own monsters. It rides map_events' room activation hook: no SDK service reports a room activating.
//
// Deaths, on key_drops' monster-death listener (the SDK's): Mendeln (0x10376760) and Rathma (0x103767d0), when killed
// before their retreat, set the stage to 2 or 4 and open a ghostly portal at their spot to the next level. The first
// clone's death (0x10376840) sets stage 6 and the survivor casts RathmaDeath's buff through PD2's helper 0x1036d7d0
// (its AI keeps it on); the second's sets 7, kills the void knights, golems and totem, drops the Tainted Worldstone
// Shard, rolls 1 in (50 - 6 x tier) for an unlimited tome or key and, at tiers 1-2, 1 in 200 for one of three uniques
// (all at item level 130), and opens a gold portal to Harrogath.
#include "rathma.h"

#include "log.h"
#include "map_open.h"
#include "rathma_ai.h"
#include "uber_drops.h"

#include <D2RLPlugin/item.h>

#include <intrin.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// Merged monstats ids (PD2 933-936)
constexpr int32_t RathmaBone = 950, RathmaPoison = 951, BoneClone = 952, PoisonClone = 953;
constexpr uint32_t TierKey = 0x55425254;   // 'UBRT', as map_open.cpp
constexpr uint32_t StageKey = 0x55425247;  // 'UBRG', as rathma_ai.cpp: PD2 game +0x262a
constexpr int32_t Harrogath = 109, HarrogathAct = 4;  // the Rathma levels are in act V too
constexpr int32_t GoldPortal = 589;     // xactportal_gold (PD2 586)
constexpr int32_t GhostlyPortal = 588;  // xactportal_ghostly (PD2 585)
constexpr int32_t ModeNeutral = 1;
constexpr int32_t ArenaHalf = 0x28;  // PD2's arena rects are the bosses' spot +-0x28
constexpr int32_t QualityUnique = 7;
constexpr uint32_t Shard = D2RL::Items::MakeItemCode("cwss");
constexpr uint32_t Tomes[] = {D2RL::Items::MakeItemCode("rid"), D2RL::Items::MakeItemCode("rtp"),
                              D2RL::Items::MakeItemCode("rkey")};
// The Third Eye (ram), Cage of the Unsullied (rar), Band of Skulls (rbe): code and runtime uniqueitems row
struct UniqueDrop {
	uint32_t code;
	int32_t row;
};
constexpr UniqueDrop Uniques[] = {{D2RL::Items::MakeItemCode("ram"), 455},
                                  {D2RL::Items::MakeItemCode("rar"), 454},
                                  {D2RL::Items::MakeItemCode("rbe"), 453}};

struct Spot {
	int32_t x, y;
};
struct Boss {
	int32_t classId;
	Spot at;
};
// PD2 0x1036fef0's three Rathma levels (PD2 161-163, the port's 160-162)
struct Stage {
	int32_t level, from;  // the stage it moves on from (to from + 1)
	Spot portals[4];      // the in-level ghostly portal; the third level's picks one of three, then tries 1-3 less 3
	int32_t picks;
	Spot marker;  // where the portal takes the group (PD2's `empty` object 587), PD2's tier-0 re-entry spot too
	Boss bosses[2];
	int32_t bossCount;
	Spot arena;  // the arena's centre
};
constexpr Stage Stages[] = {
	{160, 0, {{0x4862, 0x363d}}, 1, {0x476b, 0x35d3}, {{RathmaPoison, {0x476c, 0x35bf}}}, 1, {0x476c, 0x35bf}},
	{161, 2, {{0x488a, 0x3a25}}, 1, {0x47d7, 0x3961}, {{RathmaBone, {0x47d3, 0x394b}}}, 1, {0x47d3, 0x394b}},
	{162,
	 4,
	 {{0x4fb1, 0x1590}, {0x4ff0, 0x151c}, {0x5096, 0x1549}},
	 3,
	 {0x4e98, 0x1419},
	 {{PoisonClone, {0x4e97, 0x1403}}, {BoneClone, {0x4e9d, 0x1403}}},
	 2,
	 {0x4e9a, 0x1403}},
};
static_assert(Stages[2].arena.x - ArenaHalf == 0x4e72 && Stages[0].arena.y + ArenaHalf == 0x35e7);

constexpr auto TomeOdds(int32_t tier) noexcept -> uint32_t {
	return static_cast<uint32_t>(50 - 6 * tier);
}
static_assert(TomeOdds(0) == 50 && TomeOdds(1) == 44 && TomeOdds(2) == 38);

// PD2 0x1037d900: the half-open spans [a1, a2) and [b1, b2) meet
constexpr auto Meets(int32_t a1, int32_t a2, int32_t b1, int32_t b2) noexcept -> bool {
	return (a1 >= b1 && a1 < b2) || (a2 >= b1 && a2 < b2) || (a1 < b1 && a2 >= b2);
}
static_assert(Meets(0, 10, 5, 6) && Meets(0, 10, 9, 20) && !Meets(0, 10, 11, 20) && !Meets(0, 10, -5, 0));

std::atomic<uint32_t> g_kills {0};
std::atomic<uint32_t> g_tomes {0};
std::atomic<uint32_t> g_uniques {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_stages {0};
std::atomic<uint32_t> g_spawns {0};
std::atomic<uint32_t> g_arenaRooms {0};
std::atomic<uint32_t> g_arenaTrips {0};

auto Counter(Game* game, uint32_t key) noexcept -> int32_t& {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto StageOf(int32_t level) noexcept -> const Stage* {
	for (const Stage& s : Stages) {
		if (s.level == level) {
			return &s;
		}
	}
	return nullptr;
}

// PD2 0x10389f00: the room holding (x, y) near `near`, else found in the level's rooms and built
auto RoomHolding(Game* game, void* near, int32_t level, Spot at) noexcept -> void* {
	using namespace d2r::rva;
	void* room = near != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(near, at.x, at.y)
	                             : nullptr;
	if (room != nullptr || At<uint64_t>(game, GameActCount) <= HarrogathAct) {
		return room;
	}
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	void* act = At<void**>(game, GameActs)[HarrogathAct];
	void* drlg = act != nullptr ? d2r::Engine<void* (*)(uint8_t, void*, int32_t) noexcept>(DrlgLevelOf)(
									  dataFlag, At<void*>(act, ActDrlg), level)
	                            : nullptr;
	void* drlgRoom = drlg != nullptr && At<void*>(drlg, DrlgLevelRooms) != nullptr
		? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(DrlgRoomAt)(drlg, at.x / 5, at.y / 5)
		: nullptr;
	return drlgRoom != nullptr ? d2r::Engine<void* (*)(uint8_t, void*) noexcept>(DrlgRoomInit)(dataFlag, drlgRoom)
	                           : nullptr;
}

// PD2 0x1038a060 for a monster, on the free spot nearest its spot: D2R's spawn refused the poison clone's own spot.
// shortcut: PD2 also sets the spawn's 1.13c flags 0x3000000; set them if a boss ever acts differently from PD2's
auto SpawnBoss(Game* game, void* near, int32_t level, const Boss& boss) noexcept -> bool {
	using namespace d2r::rva;
	void* room = RoomHolding(game, near, level, boss.at);
	int32_t xy[2] = {boss.at.x, boss.at.y};
	void* found = nullptr;
	if (room != nullptr) {
		d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(
			FindFreeSpot)(game, room, xy, 3, 0x400, &found, 5, 100);
	}
	Unit* unit = found != nullptr
		? d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
			  SpawnMonster)(game, found, xy[0], xy[1], boss.classId, ModeNeutral, -1, 0x42)
		: nullptr;
	if (unit == nullptr) {
		plog::Warn("rathma: no monster %d at (0x%x, 0x%x) in level %d", boss.classId, boss.at.x, boss.at.y, level);
		return false;
	}
	++g_spawns;
	return true;
}

// The stage's ghostly portal: PD2 makes it, then links it to the marker in the same level (0x1038a510); the port's
// portal leads to its own level, which RathmaArenaPortal reads as "to the marker".
// shortcut: PD2's marker object (`empty`) and its waypoint removal by the marker are not made; the waypoints go on
// arrival (map_open.cpp), as from any portal
auto OpenArenaPortal(Game* game, void* near, const Stage& s) noexcept -> bool {
	const int32_t pick = s.picks > 1 ? static_cast<int32_t>(__rdtsc() % static_cast<uint64_t>(s.picks)) : 0;
	Spot tries[4] = {s.portals[pick]};
	int n = 1;
	for (int i = 0; s.picks > 1 && i < s.picks; ++i) {
		tries[n++] = {s.portals[i].x - 3, s.portals[i].y - 3};
	}
	for (int i = 0; i < n; ++i) {
		void* room = RoomHolding(game, near, s.level, tries[i]);
		if (room != nullptr && OpenPortalAt(game, room, GhostlyPortal, tries[i].x, tries[i].y, s.level) != nullptr) {
			++g_portals;
			plog::Info("rathma: arena portal at (0x%x, 0x%x) in level %d, to (0x%x, 0x%x)", tries[i].x, tries[i].y,
			           s.level, s.marker.x, s.marker.y);
			return true;
		}
	}
	return false;
}

auto OpenNextLevelPortal(Game* game, Unit* monster, Spot at, int32_t level) noexcept -> void {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(monster);
	if (OpenPortalAt(game, room, GhostlyPortal, at.x, at.y, level) != nullptr) {
		++g_portals;
		plog::Info("rathma: ghostly portal at (0x%x, 0x%x) to level %d", at.x, at.y, level);
	}
}

// PD2 makes these at item level 130 (the three uniques are level 131)
constexpr int32_t DropLevel = 130;
auto Drop(Game* game, Unit* monster, uint32_t code, int32_t quality, int32_t uniqueRow) noexcept -> void {
	if (!DropUniqueAt(game, monster, code, uniqueRow, DropLevel, quality)) {
		plog::Warn("rathma: %.4s did not drop at item level %d", reinterpret_cast<const char*>(&code), DropLevel);
	}
}

// PD2 0x10376840, stage 6 -> 7
auto PairDead(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	const int killed = KillRathmaAdds(game);
	const int32_t tier = Counter(game, TierKey);
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster);
	Drop(game, monster, Shard, QualityNormal, -1);
	if (d2r::RandomBelow(seed, TomeOdds(tier)) == 0) {
		Drop(game, monster, Tomes[d2r::RandomBelow(seed, 3)], QualityNormal, -1);
		++g_tomes;
	}
	if (tier >= 1 && d2r::RandomBelow(seed, 200) == 0) {
		const UniqueDrop& u = Uniques[d2r::RandomBelow(seed, 3)];
		Drop(game, monster, u.code, QualityUnique, u.row);
		++g_uniques;
	}
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(monster, xy);
	if (OpenPortalAt(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(monster), GoldPortal, xy[0], xy[1],
	                 Harrogath) != nullptr) {
		++g_portals;
	}
	plog::Info("rathma: pair dead at tier %d, %d adds killed (%u kills, %u tome rolls hit, %u uniques, %u portals)",
	           tier, killed, ++g_kills, g_tomes.load(), g_uniques.load(), g_portals.load());
}

}  // namespace

auto RathmaDeath(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	const int32_t classId = At<int32_t>(monster, UnitClassId);
	if (classId < RathmaBone || classId > PoisonClone) {
		return;
	}
	int32_t& stage = Counter(game, StageKey);
	const int32_t before = stage;
	if (classId == RathmaPoison && stage < 2) {
		stage = 2;
		OpenNextLevelPortal(game, monster, Stages[0].arena, Stages[1].level);
	} else if (classId == RathmaBone && stage < 4) {
		stage = 4;
		OpenNextLevelPortal(game, monster, Stages[1].arena, Stages[2].level);
	} else if ((classId == BoneClone || classId == PoisonClone) && (stage == 5 || stage == 6)) {
		stage = stage + 1;
	}
	if (stage != before) {
		++g_stages;
		plog::Info("rathma: stage %d -> %d (class %d died)", before, stage, classId);
		if (stage == 6) {
			BuffRathmaSurvivor(game, monster);
		}
		if (stage == 7) {
			PairDead(game, monster);
		}
	}
}

auto RathmaRoom(Game* game, void* room, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	const Stage* s = game != nullptr && room != nullptr ? StageOf(level) : nullptr;
	if (s == nullptr) {
		return false;
	}
	// shortcut: PD2 also needs the game's area type to be Rathma's (game +0x1df4); the port's levels 160-162 open from
	// the Rathma key alone, so the level stands in for it
	if (int32_t& stage = Counter(game, StageKey); stage == s->from) {
		stage = s->from + 1;
		++g_stages;
		plog::Info("rathma: stage %d -> %d (level %d's first room)", s->from, stage, level);
		const bool portal = OpenArenaPortal(game, room, *s);
		int spawned = 0;
		for (int i = 0; i < s->bossCount; ++i) {
			spawned += SpawnBoss(game, room, level, s->bosses[i]) ? 1 : 0;
		}
		plog::Info("rathma: level %d, %d of %d bosses spawned, arena portal %s", level, spawned, s->bossCount,
		           portal ? "open" : "FAILED");
	}
	const int32_t x = At<int32_t>(room, RoomX), y = At<int32_t>(room, RoomY);
	const bool arena = Meets(s->arena.x - ArenaHalf, s->arena.x + ArenaHalf, x, x + At<int32_t>(room, RoomWidth)) &&
	                   Meets(s->arena.y - ArenaHalf, s->arena.y + ArenaHalf, y, y + At<int32_t>(room, RoomHeight));
	if (arena) {
		++g_arenaRooms;
	}
	return arena;
}

// shortcut: the area rules of a portal entry (map_open.cpp) are not run for this portal, which stays in its level
auto RathmaArenaPortal(Game* game, Unit* player, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	const Stage* s = StageOf(level);
	void* room = s != nullptr ? RoomHolding(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player), level,
	                                        s->marker)
	                          : nullptr;
	if (room == nullptr) {
		return false;
	}
	d2r::Engine<void (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, int32_t) noexcept>(TeleportUnit)(
		game, player, room, s->marker.x, s->marker.y, 0, 0);
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, xy);
	plog::Info("rathma: player taken to the arena of level %d at (0x%x, 0x%x) (%u so far)", level, xy[0], xy[1],
	           ++g_arenaTrips);
	return true;
}

// The grey portals' exit (PD2's `empty` marker the three portals at game +0x261c-0x2624 are linked to): level 0 until
// the first move, which is also where they lead before it (the portal's own level, D2R's level entry)
constexpr uint32_t ExitLevelKey = 0x5542524c;  // 'UBRL'
constexpr uint32_t ExitXKey = 0x55425258;      // 'UBRX'
constexpr uint32_t ExitYKey = 0x55425259;      // 'UBRY'
std::atomic<uint32_t> g_exitMoves {0};
std::atomic<uint32_t> g_reentries {0};

auto MoveExit(Game* game, int32_t level, Spot at, const char* why) noexcept -> void {
	Counter(game, ExitLevelKey) = level;
	Counter(game, ExitXKey) = at.x;
	Counter(game, ExitYKey) = at.y;
	plog::Info("rathma: tier-0 re-entry now comes out in level %d at (0x%x, 0x%x), %s (%u moves)", level, at.x, at.y,
	           why, ++g_exitMoves);
}

auto RathmaMoveReentry(Game* game, Unit* player, int32_t from, int32_t to) noexcept -> void {
	if (game == nullptr || player == nullptr || from == Harrogath || StageOf(to) == nullptr ||
	    Counter(game, TierKey) != 0) {
		return;
	}
	if (from != to) {
		// PD2 0x1038a1f0 places the marker on a free spot by the player, who still stands by the portal
		int32_t xy[2] = {};
		d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::rva::UnitCoords)(player, xy);
		MoveExit(game, from, {xy[0], xy[1]}, "by the portal to the next level");
		return;
	}
	// The arena portal: the stage's spot, by the stage byte (1, 3, then 5 and on); none in stages 2 and 4
	const int32_t stage = Counter(game, StageKey);
	const int i = stage == 1 ? 0 : stage == 3 ? 1 : stage >= 5 ? 2 : -1;
	if (i >= 0) {
		char why[32];
		std::snprintf(why, sizeof(why), "stage %d's spot", stage);
		MoveExit(game, Stages[i].level, Stages[i].marker, why);
	}
}

auto RathmaReentry(Game* game, int32_t& level, int32_t xy[2]) noexcept -> bool {
	const int32_t exit = game != nullptr && StageOf(level) != nullptr ? Counter(game, ExitLevelKey) : 0;
	if (exit == 0) {
		return false;
	}
	level = exit;
	xy[0] = Counter(game, ExitXKey);
	xy[1] = Counter(game, ExitYKey);
	return true;
}

auto RathmaPlaceAt(Game* game, Unit* player, const int32_t xy[2]) noexcept -> bool {
	using namespace d2r::rva;
	void* near = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	const int32_t level = near != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(near) : -1;
	void* room = RoomHolding(game, near, level, {xy[0], xy[1]});
	if (room == nullptr) {
		plog::Warn("rathma: no room at the re-entry spot (0x%x, 0x%x) in level %d", xy[0], xy[1], level);
		return false;
	}
	d2r::Engine<void (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, int32_t) noexcept>(TeleportUnit)(
		game, player, room, xy[0], xy[1], 0, 0);
	int32_t at[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, at);
	plog::Info("rathma: tier-0 re-entry into level %d at (0x%x, 0x%x) (%u so far)", level, at[0], at[1],
	           ++g_reentries);
	return true;
}

auto RathmaCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "rathma: %u stages, %u bosses spawned, %u arena rooms, %u arena trips, %u pair kills, %u tome rolls "
	              "hit, %u uniques, %u portals, %u re-entry moves, %u re-entries",
	              g_stages.load(), g_spawns.load(), g_arenaRooms.load(), g_arenaTrips.load(), g_kills.load(),
	              g_tomes.load(), g_uniques.load(), g_portals.load(), g_exitMoves.load(), g_reentries.load());
}
