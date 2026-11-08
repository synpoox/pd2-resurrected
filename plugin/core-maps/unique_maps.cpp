// The unique maps' boss deaths (MS7b, MS7c; docs/research/pd2-map-system-dll.md, section 6). PD2's monster-death
// handler (ProjectDiablo.dll 0x1037a330) switches on the dead monster's class (0x10378d80); each unique map boss's entry
// acts only while the game's map event (+0x1df4) holds its map's value, which opening the map sets (map_events.cpp):
// - 0x1e Zhar's Sanctum: ZharTheMad (0x10376ea0) opens a gold portal to Harrogath at the body and drops a magic grand
//   charm at item level 99; each of his three mini-bosses (0x10376dc0) drops one at 90, 1 in 2, until Zhar dies.
// - 0x1f Warlord of Blood: WarlordOfBlood (0x10376fb0) opens the gold portal, drops 2-3 random uniques and kills the
//   `demonpillar`s in his room and the rooms next to it.
// - 0x20 Fallen Gardens: Iskatu (0x103774f0) opens the gold portal and drops one of the eight Rainbow Facets
//   (0x103841d0, uniques 392-399); Rakanoth (0x10377490) opens a red portal to the Diamond Gate.
// - 0x22 Imperial Palace: the mini-boss Kamyr (0x103775b0) opens a red portal into the boss arena and drops a Puzzle
//   Piece; the boss (0x10377670) opens the gold portal and drops a Puzzle Box.
// - 0x23 City of Ureh: the boss (0x10377270) opens the gold portal and drops 3 random uniques of socketable bases.
// - 0x21 Outer Void: PD2's preset rewrite (0x1038bfc0, level 180) puts a random boss of one of three lists on each of
//   the level's three placeholder monsters, at 200/175/150% max life with state `mapevent`'s list (+5% damage, +10 to
//   the four masteries) on it and its minions, and a red portal on each placeholder object, linked to a spot deeper
//   in the level. After the death switch (0x10379170) a death from the next list steps the level: twice a ghostly
//   portal to a deeper spot, then the gold portal; each step drops one of nine uber keys and parts.
// - 9 dungeons: the same rewrite makes each dungeon's boss preset (lesserhydra1) its boss and its blocker presets its
//   barriers; the boss's death (0x10377890, 0x103778f0, 0x10377a30) opens them, and a barrier made after it opens at
//   once (the barriers' InitFn, 0x1038daf0).
// A random unique is a uniqueitems row PD2 accepts (enabled, rarity above 0, level 1-99): PD2 drops a unique of that
// row's base item (0x103a3a50), and the engine's own unique roll then picks the row, as here.
// The trigger is the SDK's monster-death listener (key_drops.cpp); the portal, the drop and the kill are engine calls
// (rathma.cpp, lucion.cpp), as no SDK service makes them. The uniques' bases are read from DataTableService after each
// table load, as that service answers Busy on the death listener's thread. What is left out is in docs/deviations.md.
#include "unique_maps.h"

#include "log.h"
#include "lucion.h"
#include "map_events.h"
#include "map_open.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// The bosses, in the port's monstats ids (PD2's hcIdx + 17)
constexpr int32_t Zhar = 1075, Warlord = 1077, Iskatu = 1111, Rakanoth = 1113, Kamyr = 1165, ImperialBoss = 1183,
                  UrehBoss = 1191;
constexpr int32_t ZharMiniBosses[] = {1078, 1079, 1080};  // ZharMiniBossBigHead, ZharMiniBossBaboon, ZharMiniBossCantor
constexpr int32_t DemonPillar = 1074;                     // demonpillar (PD2 1057)
// The map event values (PD2 0x10383060)
constexpr int32_t ZharEvent = 0x1e, WarlordEvent = 0x1f, GardensEvent = 0x20, ImperialEvent = 0x22, UrehEvent = 0x23;
// Port level ids: Harrogath, the Diamond Gate (PD2 184), the Imperial Boss Map (PD2 194)
constexpr int32_t Harrogath = 109, DiamondGate = 180, ImperialArena = 190;
constexpr int32_t GoldPortal = 589;  // xactportal_gold (PD2 586)
constexpr int32_t RedPortal = 587;   // xactportal_red (PD2 584)
constexpr int32_t QualityMagic = 4, QualityUnique = 7;
constexpr uint32_t GrandCharm = D2RL::Items::MakeItemCode("cm3");
constexpr uint32_t PuzzlePiece = D2RL::Items::MakeItemCode("lpp");
constexpr uint32_t PuzzleBox = D2RL::Items::MakeItemCode("lbox");
constexpr uint32_t Jewel = D2RL::Items::MakeItemCode("jew");
constexpr int32_t FirstFacet = 392;  // Rainbow Facet, runtime uniqueitems rows 392-399 (merge/ids-uniqueitems.tsv)
// PD2's game +0x262a, Zhar's Sanctum's progress: below 4 the mini-bosses drop charms; Zhar's death sets it to 4
constexpr uint32_t ZharKey = 0x4d5a4852;  // 'MZHR'
constexpr int MaxPillars = 32;

// The Outer Void (PD2 level 180), in act V
constexpr int32_t OuterVoidEvent = 0x21, OuterVoid = 176, OuterVoidAct = 4;
constexpr int32_t UnitObject = 2;
constexpr int32_t GhostlyPortal = 588;  // xactportal_ghostly (PD2 585)
constexpr int32_t MapEventState = 259;  // states `mapevent` (PD2 213)
constexpr int32_t StatDamagePercent = 25, FirstMastery = 329, LastMastery = 332;  // the fire to poison masteries
// PD2's three boss lists (0x105df4cc, 0x105df3c4, 0x105df544), which the death sets (0x105deb54, 0x105dea4c,
// 0x105debcc) repeat, in port monstats ids (PD2's + 17 past the vanilla rows)
constexpr int32_t BossesA[] = {1043, 817, 1042, 911, 940, 941, 943, 944, 1259, 1014};
constexpr int32_t BossesB[] = {763, 1013, 767, 932, 1183, 1181, 1192, 1191, 1257, 1260};
constexpr int32_t BossesC[] = {247, 248, 249, 841, 987, 988, 989, 990};
struct BossList {
	int32_t placeholder;  // the preset monster PD2 replaces: lesserhydra1-3 (PD2 738-740)
	const int32_t* ids;
	size_t count;
	uint32_t lifePercent;
};
constexpr BossList Bosses[] = {{755, BossesA, std::size(BossesA), 200},
                               {756, BossesB, std::size(BossesB), 175},
                               {757, BossesC, std::size(BossesC), 150}};
constexpr int32_t PortalPlaceholders[] = {604, 605, 606};  // preset_placeholder_A-C (PD2 601-603)
// Where the links lead, in subtiles from the level's origin: the three preset portals, then the two ghostly steps
constexpr int32_t Links[][2] = {{0x18 * 5, 0x43 * 5}, {0x41 * 5, 0x86 * 5}, {0xa6 * 5, 0x2c * 5}, {485, 930}, {845, 590}};
// PD2 0x105debc4: Uber Horn, Soul and Blood, Splinter, Rathma Ingredient, Torch Fragment, Lucion Keys A-C
constexpr uint32_t StepDrops[] = {
	D2RL::Items::MakeItemCode("dcho"), D2RL::Items::MakeItemCode("dcso"), D2RL::Items::MakeItemCode("dcbl"),
	D2RL::Items::MakeItemCode("rtmv"), D2RL::Items::MakeItemCode("rtmo"), D2RL::Items::MakeItemCode("cm2f"),
	D2RL::Items::MakeItemCode("lucb"), D2RL::Items::MakeItemCode("lucc"), D2RL::Items::MakeItemCode("lucd")};
constexpr uint32_t StepKey = 0x4f565354;  // 'OVST', PD2's game +0x2628 in the Outer Void: 0-3
constexpr uint32_t LinkKey = 0x4f564c00;  // 'OVL' + link: the linked portal's unit id + 1
constexpr int MaxSpawned = 64;
constexpr int MaxNearUnits = 512;

// The dungeons (map event 9; PD2 0x1038bfc0): the level's boss preset, lesserhydra1, becomes the dungeon's boss, and its
// boss blocker presets become the barriers that open when the boss dies. Port ids (PD2's: levels 164, 171, 186;
// monsters 743, 1104, 1105; objects 616-619).
constexpr int32_t DungeonEvent = 9;
constexpr int32_t BossPlaceholder = 755;  // lesserhydra1 (PD2 0x2e2)
constexpr int32_t BlockerX = 619, BlockerY = 620, BlockerXWarp = 621, BlockerYWarp = 622;  // bossblocker_x, _y, _warps
constexpr int MaxBarriers = 7;  // PD2's game +0x2604-0x261c
struct Dungeon {
	int32_t level;
	int32_t boss;
};
constexpr Dungeon Dungeons[] = {{163, 760},    // Plains of Torment: willowispboss
                                {168, 1121},   // Sanctuary of Sin: RadamentBoss
                                {182, 1122}};  // Steppes of Daken-Shar: SharpToothBoss
constexpr uint32_t DungeonBossKey = 0x4d444742;  // 'MDGB': the boss's unit id + 1 (PD2's game +0x2600)
constexpr uint32_t BarrierKey = 0x4d444700;      // 'MDG' + slot: a barrier's unit id + 1 (PD2's game +0x2604 + slot * 4)

// D2R's compiled uniqueitems row (uniqueitems.bin, 348 bytes): 1.13c's to +0x2c, then 4 more bytes before rarity
constexpr uint32_t UniqueRowSize = 348;
constexpr int UqCode = 0x28, UqFlags = 0x2c, UqRarity = 0x34, UqLevel = 0x38;
// PD2 tests the flags for non-zero, which in 1.13c means the enabled bit; D2R also sets 0x8 on rows that never spawn
constexpr uint32_t UqSpawnable = 1;
constexpr uint32_t ItemRowSize = 448;  // weapons.bin, armor.bin and misc.bin rows
constexpr int ItemGemSockets = 0x148;  // uint8 gemsockets: 1.13c's +0x138, which PD2's Ureh drop tests
constexpr int MaxBases = 4096;

// The base codes of the rows a random unique may take: all of them, and those whose base takes sockets (Ureh)
uint32_t g_anyBases[MaxBases];
uint32_t g_socketBases[MaxBases];
std::atomic<int> g_anyCount {0};
std::atomic<int> g_socketCount {0};
const D2RL::DataTableService* g_tables = nullptr;

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_drops {0};
std::atomic<uint32_t> g_pillars {0};
std::atomic<uint32_t> g_voidBosses {0};
std::atomic<uint32_t> g_voidWarps {0};
std::atomic<uint32_t> g_barriers {0};        // blocker presets made into barriers
std::atomic<uint32_t> g_barriersOpened {0};  // barriers opened by a boss's death

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// shortcut: the portal stands at the body, as rathma.cpp's, where PD2 first moves it to a free spot (0x10389ad0); in
// the Imperial Palace it can land behind bars. Use event_portals.cpp's FindFreeSpot if players can't reach one
auto Portal(Game* game, Unit* monster, int32_t object, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(monster, xy);
	Unit* portal = OpenPortalAt(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(monster), object, xy[0], xy[1],
	                            level);
	if (portal != nullptr) {
		plog::Info("unique maps: portal %d to level %d at (%d, %d) (%u)", object, level, xy[0], xy[1], ++g_portals);
	}
	return portal;
}

// shortcut: the item takes the monster's item level, where PD2 names one (90, 99 or 130); set it if a drop ever
// differs by level
auto Drop(Game* game, Unit* monster, uint32_t code, int32_t quality, int32_t uniqueRow) noexcept -> void {
	using namespace d2r::rva;
	const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(
		static_cast<uint8_t>(d2r::DataFlag(game)), code);
	if (row < 0) {
		plog::Warn("unique maps: no item \"%.4s\"", reinterpret_cast<const char*>(&code));
		return;
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
		game, monster, row, quality, uniqueRow + 1, 0);
	plog::Info("unique maps: dropped %.4s (quality %d) (%u)", reinterpret_cast<const char*>(&code), quality, ++g_drops);
}

// PD2 draws rows at random until one passes (20 tries for the Warlord, 40 for Ureh, then it drops no more); here the
// draw is over the passing rows only
auto RandomUniques(Game* game, Unit* monster, uint32_t* seed, uint32_t count, bool socketed) noexcept -> void {
	const uint32_t* bases = socketed ? g_socketBases : g_anyBases;
	const int n = (socketed ? g_socketCount : g_anyCount).load(std::memory_order_acquire);
	if (n <= 0) {
		plog::Warn("unique maps: no random unique bases read");
		return;
	}
	for (uint32_t i = 0; i < count; ++i) {
		Drop(game, monster, bases[d2r::RandomBelow(seed, static_cast<uint32_t>(n))], QualityUnique, -1);
	}
}

// PD2 0x10376fb0: the pillars in the Warlord's room and its neighbours, picked first so no kill runs while a room's
// units are walked
auto KillPillars(Game* game, Unit* warlord) noexcept -> void {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(warlord);
	void** rooms = nullptr;
	uint32_t count = 0;
	if (room != nullptr) {
		d2r::Engine<void (*)(void*, void***, uint32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	}
	Unit* found[MaxPillars] = {};
	int n = 0;
	for (uint32_t r = 0; rooms != nullptr && r < count; ++r) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[r]); unit != nullptr && n < MaxPillars;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			if (At<int32_t>(unit, UnitType) == UnitMonster && At<int32_t>(unit, UnitClassId) == DemonPillar &&
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
				found[n++] = unit;
			}
		}
	}
	for (int i = 0; i < n; ++i) {
		KillQuietly(game, found[i]);
	}
	g_pillars.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
	plog::Info("unique maps: %d demon pillars killed in %u rooms", n, count);
}

// PD2 links each Outer Void portal to an `empty` marker (0x1038a510) at the level's origin plus Links[link]; here the
// portal's operate (OuterVoidWarp) moves the player there. The link holds the portal's unit id.
auto Link(Game* game, Unit* portal, int link) noexcept -> void {
	if (int32_t* c = portal != nullptr ? Counter(game, LinkKey + static_cast<uint32_t>(link)) : nullptr; c != nullptr) {
		*c = static_cast<int32_t>(At<uint32_t>(portal, d2r::rva::UnitIdField)) + 1;
	}
}

// The room holding the level's origin plus (dx, dy), its rooms built if needed, as ActSpawnSpot finds one; x/y get the
// spot. Null when the act or the room is missing.
auto OuterVoidRoom(Game* game, int32_t dx, int32_t dy, int32_t* x, int32_t* y) noexcept -> void* {
	using namespace d2r::rva;
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	auto* const* acts = At<void**>(game, GameActs);
	if (acts == nullptr || At<uint64_t>(game, GameActCount) <= OuterVoidAct || acts[OuterVoidAct] == nullptr) {
		return nullptr;
	}
	auto* level = d2r::Engine<uint8_t* (*)(uint8_t, void*, int32_t) noexcept>(LevelGet)(
		dataFlag, At<void*>(acts[OuterVoidAct], ActDrlg), OuterVoid);
	if (level == nullptr) {
		return nullptr;
	}
	if (At<void*>(level, LevelFirstRoomEx) == nullptr) {
		d2r::Engine<void (*)(uint8_t, void*) noexcept>(LevelBuild)(dataFlag, level);
	}
	*x = At<int32_t>(level, LevelTileX) * 5 + dx;
	*y = At<int32_t>(level, LevelTileY) * 5 + dy;
	void* roomEx = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(LevelRoomExAt)(level, *x / 5, *y / 5);
	return roomEx != nullptr ? d2r::Engine<void* (*)(uint8_t, void*) noexcept>(RoomExRoom)(dataFlag, roomEx) : nullptr;
}

// The live units of the room and its neighbours, for telling apart what a spawn added
auto NearUnits(void* room, Unit** out, int max) noexcept -> int {
	using namespace d2r::rva;
	void** rooms = nullptr;
	uint32_t count = 0;
	d2r::Engine<void (*)(void*, void***, uint32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	int n = 0;
	for (uint32_t r = 0; rooms != nullptr && r < count; ++r) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[r]); unit != nullptr && n < max;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			out[n++] = unit;
		}
	}
	return n;
}

// Each Counter call may insert, so no pointer is kept across another call.
auto Value(Game* game, uint32_t key) noexcept -> int32_t {
	const int32_t* c = Counter(game, key);
	return c != nullptr ? *c : 0;
}

auto Store(Game* game, uint32_t key, int32_t value) noexcept -> void {
	if (int32_t* c = Counter(game, key); c != nullptr) {
		*c = value;
	}
}

auto DungeonOf(int32_t level) noexcept -> const Dungeon* {
	for (const Dungeon& d : Dungeons) {
		if (d.level == level) {
			return &d;
		}
	}
	return nullptr;
}

auto IsBarrier(int32_t row) noexcept -> bool {
	return row >= BlockerX && row <= BlockerYWarp;
}

// PD2's 0x1038a060 with unit type 2: an object at x/y in the room near `room` that holds it. No PluginSDK service
// places objects, so this calls D2R's unit spawn (rva::SpawnUnit), as OpenPortalAt (map_open.cpp) does; the object's
// InitFn runs.
auto SpawnObject(Game* game, void* room, int32_t row, int32_t x, int32_t y) noexcept -> Unit* {
	using namespace d2r::rva;
	using SpawnFn = Unit* (*)(int32_t, int32_t, int32_t, int32_t, Game*, void*, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(room, x, y);
	// Mode 0, closed: PD2 0x1038a060 spawns an object in mode 0 (mode 1 is the open animation).
	return room != nullptr ? d2r::Engine<SpawnFn>(SpawnUnit)(UnitObject, row, x, y, game, room, 1, 0, 0, 0) : nullptr;
}

// PD2 0x10380590: the barrier plays its open frames (mode 1), then its mode end event opens it (mode 2), which has no
// collision. PD2 also redoes the collision at once (1.13c D2Common #10408); D2R's mode setter (rva::SetObjectMode)
// clears it when it sets a mode whose HasCollision is 0. No PluginSDK service sets an object's mode.
auto OpenBarrier(Game* game, Unit* barrier) noexcept -> bool {
	using namespace d2r::rva;
	if (barrier == nullptr || At<int32_t>(barrier, UnitType) != UnitObject || At<int32_t>(barrier, UnitMode) != 0) {
		return false;
	}
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(
		d2r::DataFlag(game), At<int32_t>(barrier, UnitClassId));
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(barrier, 1);
	const int32_t frames = record != nullptr ? static_cast<int32_t>(At<uint32_t>(record, ObjOpenFrames) >> 8) : 0;
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, barrier, UnitEventOpen, At<int32_t>(game, GameFrame) + 1 + frames, 0, 0, 0);
	return true;
}

// The argument block D2R's object init dispatch passes an InitFn (rva::ObjectInitTable)
struct ObjectInit {
	Game* game;
	Unit* object;
};
using InitFn = void (*)(ObjectInit*) noexcept;
InitFn g_initQuestDoor = nullptr;

// PD2 0x1038daf0, InitFn 30 (0x1036c320 sets it): the four barriers skip the slot's own init, which in D2R opens them
// in a Hell game past act 2 quest 4 (rva::InitQuestDoor), and a barrier made once the dungeon's boss is dead opens.
// No PluginSDK service runs object inits, so this takes the table slot, as PD2 does.
void BarrierInit(ObjectInit* init) noexcept {
	using namespace d2r::rva;
	Unit* object = init != nullptr ? init->object : nullptr;
	if (object == nullptr || !IsBarrier(At<int32_t>(object, UnitClassId))) {
		g_initQuestDoor(init);
		return;
	}
	if (init->game != nullptr && MapEvent(init->game) == DungeonEvent && MapEventStep(init->game) == 2 &&
	    OpenBarrier(init->game, object)) {
		plog::Info("dungeons: barrier %d made after the boss died, opened", At<int32_t>(object, UnitClassId));
	}
}

// PD2 0x1038bfc0's boss: max life times the list's percent, then state `mapevent` with its list
auto EmpowerBoss(Game* game, Unit* monster, uint32_t lifePercent) noexcept -> void {
	using namespace d2r::rva;
	const auto maxhp = static_cast<uint32_t>(
		d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatMaxLife, 0));
	const uint64_t scaled = std::min<uint64_t>(static_cast<uint64_t>(maxhp / 100) * lifePercent, 0x7fffffff);
	// as PD2: only max life rises, life stays as spawned
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(monster, StatMaxLife,
	                                                                           static_cast<int32_t>(scaled), 0);
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(monster, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(0, 0, UnitMonster, id);
	if (list == nullptr) {
		return;
	}
	d2r::Engine<void (*)(void*, int32_t) noexcept>(SetListState)(list, MapEventState);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto set = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	set(dataFlag, list, StatDamagePercent, 5, 0);
	for (int32_t stat = FirstMastery; stat <= LastMastery; ++stat) {
		set(dataFlag, list, stat, 10, 0);
	}
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(monster, list, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(monster, MapEventState, 1);
}

// PD2 0x10379170: a death from the list of the level's step makes the next step. False when it does not.
auto OuterVoidStep(Game* game, Unit* monster, int32_t monsterClass, uint32_t* seed) noexcept -> bool {
	int32_t* step = Counter(game, StepKey);
	if (step == nullptr || *step < 0 || *step > 2) {
		return false;
	}
	const BossList& list = Bosses[*step];
	if (std::find(list.ids, list.ids + list.count, monsterClass) == list.ids + list.count) {
		return false;
	}
	const int done = (*step)++;
	plog::Info("unique maps: Outer Void step %d by monster %d", done + 1, monsterClass);
	// shortcut: the monster's item level, where PD2 drops at 130; these keys and parts roll no stats
	Drop(game, monster, StepDrops[d2r::RandomBelow(seed, static_cast<uint32_t>(std::size(StepDrops)))],
	     d2r::rva::QualityNormal, -1);
	if (done < 2) {
		Link(game, Portal(game, monster, GhostlyPortal, OuterVoid), 3 + done);
	} else {
		Portal(game, monster, GoldPortal, Harrogath);
	}
	return true;
}

// PD2 0x10377890, 0x103778f0, 0x10377a30 (the death switch 0x10378d80's SharpToothBoss, willowispboss and
// RadamentBoss): when the stored boss dies, a dungeon's step goes 1 -> 2 and the stored barriers open. As PD2, the
// barriers open whatever the event.
auto DungeonBossDeath(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	const int32_t boss = Value(game, DungeonBossKey);
	if (boss == 0 || boss != static_cast<int32_t>(At<uint32_t>(monster, UnitIdField)) + 1) {
		return;
	}
	Store(game, DungeonBossKey, 0);
	if (MapEvent(game) == DungeonEvent && MapEventStep(game) == 1) {
		SetMapEventStep(game, 2);
	}
	int opened = 0;
	for (int i = 0; i < MaxBarriers; ++i) {
		const int32_t id = Value(game, BarrierKey + static_cast<uint32_t>(i));
		if (id == 0) {
			continue;
		}
		Store(game, BarrierKey + static_cast<uint32_t>(i), 0);
		Unit* barrier = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(
			game, UnitObject, static_cast<uint32_t>(id - 1));
		const bool ok = OpenBarrier(game, barrier);
		opened += ok ? 1 : 0;
		if (!ok) {
			plog::Warn("dungeons: barrier %d (unit %d) not opened: %s, mode %d", i, id - 1,
			           barrier != nullptr ? "found" : "gone", barrier != nullptr ? At<int32_t>(barrier, UnitMode) : -1);
		}
	}
	g_barriersOpened.fetch_add(static_cast<uint32_t>(opened), std::memory_order_relaxed);
	plog::Info("dungeons: boss %d died, event %d, step %d, %d barriers opened", At<int32_t>(monster, UnitClassId),
	           MapEvent(game), MapEventStep(game), opened);
}

void __cdecl ReadUniqueBases(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*,
                             void*) noexcept {
	using namespace D2RL::DataTables;
	g_anyCount.store(0, std::memory_order_release);
	g_socketCount.store(0, std::memory_order_release);
	TableView view {.structSize = TableViewSize};
	// shortcut: the RotW bank's rows only; a LoD or Classic game draws from them too, and a base its bank lacks drops
	// nothing (Drop's "no item"). Read each game's own bank if those modes ever get the maps
	if (g_tables->getTable(context, Bank::Rotw, TableId::UniqueItems, &view) != Result::Success ||
	    view.rowSize != UniqueRowSize || view.rows == nullptr) {
		plog::Warn("unique maps: no uniqueitems rows (row size %u), no random uniques", view.rowSize);
		return;
	}
	int any = 0, socketed = 0;
	for (uint32_t i = 0; i < view.rowCount && any < MaxBases; ++i) {
		const auto* row = static_cast<const uint8_t*>(view.rows) + static_cast<size_t>(i) * UniqueRowSize;
		const uint16_t level = At<uint16_t>(row, UqLevel);
		if ((At<uint32_t>(row, UqFlags) & UqSpawnable) == 0 || At<uint32_t>(row, UqRarity) == 0 || level == 0 ||
		    level > 99) {
			continue;
		}
		const uint32_t code = At<uint32_t>(row, UqCode);
		g_anyBases[any++] = code;
		RowView item {.structSize = RowViewSize};
		if (g_tables->findRowByCode(context, Bank::Rotw, TableId::Items, code, &item) == Result::Success &&
		    item.rowSize == ItemRowSize && item.row != nullptr && At<uint8_t>(item.row, ItemGemSockets) != 0) {
			g_socketBases[socketed++] = code;
		}
	}
	g_anyCount.store(any, std::memory_order_release);
	g_socketCount.store(socketed, std::memory_order_release);
	plog::Info("unique maps: %d of %u uniques can drop at random, %d of them socketable", any, view.rowCount, socketed);
}

}  // namespace

auto UniqueMapDeath(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	DungeonBossDeath(game, monster);
	const int32_t event = MapEvent(game);
	if (event < ZharEvent || event > UrehEvent) {
		return;
	}
	const int32_t monsterClass = At<int32_t>(monster, UnitClassId);
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster);
	const bool miniBoss = monsterClass == ZharMiniBosses[0] || monsterClass == ZharMiniBosses[1] ||
	                      monsterClass == ZharMiniBosses[2];
	if (event == ZharEvent && (monsterClass == Zhar || miniBoss)) {
		int32_t* progress = Counter(game, ZharKey);
		if (progress == nullptr || *progress >= 4) {
			return;
		}
		++g_deaths;
		if (monsterClass == Zhar) {
			*progress = 4;
			Portal(game, monster, GoldPortal, Harrogath);
			// shortcut: PD2 also forces one magic affix on the charm (0x102e5110, not read); read it if the charm
			// should differ from a plain magic one
			Drop(game, monster, GrandCharm, QualityMagic, -1);
		} else if (d2r::RandomBelow(seed, 2) == 0) {
			Drop(game, monster, GrandCharm, QualityMagic, -1);
		}
	} else if ((event == WarlordEvent && monsterClass == Warlord) || (event == UrehEvent && monsterClass == UrehBoss) ||
	           (event == ImperialEvent && monsterClass == ImperialBoss) ||
	           (event == GardensEvent && monsterClass == Iskatu)) {
		++g_deaths;
		Portal(game, monster, GoldPortal, Harrogath);
		if (monsterClass == Iskatu) {
			Drop(game, monster, Jewel, QualityUnique, FirstFacet + static_cast<int32_t>(d2r::RandomBelow(seed, 8)));
		} else if (monsterClass == ImperialBoss) {
			Drop(game, monster, PuzzleBox, QualityUnique, -1);
		} else if (monsterClass == Warlord) {
			RandomUniques(game, monster, seed, 2 + d2r::RandomBelow(seed, 2), false);
			KillPillars(game, monster);
		} else {
			RandomUniques(game, monster, seed, 3, true);
		}
	} else if (event == GardensEvent && monsterClass == Rakanoth) {
		++g_deaths;
		Portal(game, monster, RedPortal, DiamondGate);
	} else if (event == ImperialEvent && monsterClass == Kamyr) {
		++g_deaths;
		// shortcut: PD2 links this portal to an `empty` marker in the arena (0x1038a1f0, 0x1038a510); here it leads to
		// the arena level's own arrival spot
		Portal(game, monster, RedPortal, ImperialArena);
		Drop(game, monster, PuzzlePiece, QualityUnique, -1);
	} else if (event == OuterVoidEvent && OuterVoidStep(game, monster, monsterClass, seed)) {
		++g_deaths;
	} else {
		return;
	}
	plog::Info("unique maps: monster %d died in map event 0x%x (%u boss deaths)", monsterClass, event, g_deaths.load());
}

auto OuterVoidPreset(Game* game, void* room, uint8_t* preset, void (*spawn)(Game*, void*, const uint8_t*) noexcept) noexcept
	-> bool {
	using namespace d2r::rva;
	if (d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) != OuterVoid) {
		return false;
	}
	const int32_t type = At<int32_t>(preset, PresetType);
	const int32_t id = At<int32_t>(preset, PresetId);
	if (type == UnitObject) {
		const auto* p = std::find(std::begin(PortalPlaceholders), std::end(PortalPlaceholders), id);
		if (p == std::end(PortalPlaceholders)) {
			return false;
		}
		const int32_t x = At<int32_t>(room, RoomX) + At<int32_t>(preset, PresetX);
		const int32_t y = At<int32_t>(room, RoomY) + At<int32_t>(preset, PresetY);
		const int link = static_cast<int>(p - std::begin(PortalPlaceholders));
		Unit* portal = OpenPortalAt(game, room, RedPortal, x, y, OuterVoid);
		Link(game, portal, link);
		plog::Info("unique maps: Outer Void portal %d at (%d, %d)%s", link, x, y, portal != nullptr ? "" : " FAILED");
		return false;  // the placeholder itself still spawns, as in PD2
	}
	const BossList* list = nullptr;
	for (const BossList& b : Bosses) {
		list = type == UnitMonster && b.placeholder == id ? &b : list;
	}
	if (list == nullptr) {
		return false;
	}
	// The boss spawns as the preset would (with its minions), then the units that spawn added get the boss's stats
	Unit* before[MaxNearUnits];
	const int had = NearUnits(room, before, MaxNearUnits);
	const int32_t boss = list->ids[static_cast<size_t>(std::rand()) % list->count];  // PD2's rand()
	auto* presetId = reinterpret_cast<int32_t*>(preset + PresetId);
	*presetId = boss;
	spawn(game, room, preset);
	*presetId = id;
	Unit* after[MaxNearUnits];
	const int has = NearUnits(room, after, MaxNearUnits);
	int empowered = 0;
	int32_t xy[2] = {};
	for (int i = 0; i < has && empowered < MaxSpawned; ++i) {
		if (At<int32_t>(after[i], UnitType) == UnitMonster && std::find(before, before + had, after[i]) == before + had) {
			EmpowerBoss(game, after[i], list->lifePercent);
			if (empowered++ == 0) {
				d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(after[i], xy);
			}
		}
	}
	plog::Info("unique maps: Outer Void preset %d became boss %d at (%d, %d), %d monsters at %u%% life with state %d (%u)",
	           id, boss, xy[0], xy[1], empowered, list->lifePercent, MapEventState, ++g_voidBosses);
	return true;
}

auto DungeonPreset(Game* game, void* room, uint8_t* preset, void (*spawn)(Game*, void*, const uint8_t*) noexcept) noexcept
	-> bool {
	using namespace d2r::rva;
	const Dungeon* dungeon = DungeonOf(d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room));
	if (dungeon == nullptr) {
		return false;
	}
	const int32_t type = At<int32_t>(preset, PresetType);
	const int32_t id = At<int32_t>(preset, PresetId);
	const int32_t x = At<int32_t>(room, RoomX) + At<int32_t>(preset, PresetX);
	const int32_t y = At<int32_t>(room, RoomY) + At<int32_t>(preset, PresetY);
	if (type == UnitMonster && id == BossPlaceholder) {
		// As PD2, the preset itself is rewritten to the boss. shortcut: PD2 spawns the boss alone on a free spot near
		// the preset (0x1038a0d0); here the preset spawns it, as any boss preset is placed
		Unit* before[MaxNearUnits];
		const int had = NearUnits(room, before, MaxNearUnits);
		*reinterpret_cast<int32_t*>(preset + PresetId) = dungeon->boss;
		spawn(game, room, preset);
		Unit* after[MaxNearUnits];
		const int has = NearUnits(room, after, MaxNearUnits);
		Unit* boss = nullptr;
		for (int i = 0; i < has && boss == nullptr; ++i) {
			if (At<int32_t>(after[i], UnitType) == UnitMonster && At<int32_t>(after[i], UnitClassId) == dungeon->boss &&
			    std::find(before, before + had, after[i]) == before + had) {
				boss = after[i];
			}
		}
		if (boss != nullptr) {
			Store(game, DungeonBossKey, static_cast<int32_t>(At<uint32_t>(boss, UnitIdField)) + 1);
			if (MapEvent(game) == DungeonEvent && MapEventStep(game) == 0) {
				SetMapEventStep(game, 1);
			}
		}
		plog::Info("dungeons: level %d preset %d became boss %d at (%d, %d)%s, event %d, step %d", dungeon->level, id,
		           dungeon->boss, x, y, boss != nullptr ? "" : " NOT FOUND", MapEvent(game), MapEventStep(game));
		return true;  // and no extra boss: PD2 returns before its extra boss test
	}
	if (type != UnitObject || !IsBarrier(id)) {
		return false;
	}
	// PD2's barriers: Plains of Torment a row of 7 per blocker, along its length; Sanctuary of Sin two of bossblocker_y;
	// the Steppes bossblocker_x_warp for bossblocker_x. The other blockers spawn as they are.
	struct Place {
		int32_t row, x, y;
	};
	Place places[MaxBarriers] = {};
	int count = 0;
	if (dungeon->level == Dungeons[0].level && (id == BlockerX || id == BlockerY)) {
		for (int i = 0; i < MaxBarriers; ++i) {
			const int32_t step = -12 + 4 * i;
			places[count++] = id == BlockerY ? Place {id, x + step, y} : Place {id, x, y + step};
		}
	} else if (dungeon->level == Dungeons[1].level && id == BlockerY) {
		places[count++] = {id, x - 5, y + 1};
		places[count++] = {id, x + 5, y + 1};
	} else if (dungeon->level == Dungeons[2].level && id == BlockerX) {
		places[count++] = {BlockerXWarp, x, y};
	}
	if (count == 0) {
		return false;
	}
	int made = 0;
	for (int i = 0; i < count; ++i) {
		Unit* barrier = SpawnObject(game, room, places[i].row, places[i].x, places[i].y);
		if (barrier != nullptr) {
			++made;
			Store(game, BarrierKey + static_cast<uint32_t>(i),
			      static_cast<int32_t>(At<uint32_t>(barrier, UnitIdField)) + 1);
		}
	}
	plog::Info("dungeons: level %d blocker %d at (%d, %d) became %d of %d barriers %d, event %d, step %d (%u)",
	           dungeon->level, id, x, y, made, count, places[0].row, MapEvent(game), MapEventStep(game), ++g_barriers);
	return true;
}

auto DungeonKeepsRoom(Unit* unit) noexcept -> bool {
	using namespace d2r::rva;
	const int32_t type = At<int32_t>(unit, UnitType);
	const int32_t classId = At<int32_t>(unit, UnitClassId);
	if (type == UnitObject) {
		return IsBarrier(classId) && At<int32_t>(unit, UnitMode) == 0;
	}
	if (type != UnitMonster || d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) != 0) {
		return false;
	}
	for (const Dungeon& d : Dungeons) {
		if (d.boss == classId) {
			return true;
		}
	}
	return false;
}

auto OuterVoidWarp(Game* game, Unit* player, Unit* portal) noexcept -> bool {
	using namespace d2r::rva;
	if (game == nullptr || portal == nullptr ||
	    d2r::Engine<uint16_t (*)(Unit*) noexcept>(PortalLevel)(portal) != OuterVoid) {
		return false;
	}
	const auto id = static_cast<int32_t>(At<uint32_t>(portal, UnitIdField)) + 1;
	for (int link = 0; link < static_cast<int>(std::size(Links)); ++link) {
		const int32_t* c = Counter(game, LinkKey + static_cast<uint32_t>(link));
		if (c == nullptr || *c != id) {
			continue;
		}
		int32_t x = 0, y = 0;
		void* room = OuterVoidRoom(game, Links[link][0], Links[link][1], &x, &y);
		if (room == nullptr) {
			plog::Warn("unique maps: no Outer Void room at link %d", link);
			return false;
		}
		d2r::Engine<void (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, int32_t) noexcept>(TeleportUnit)(
			game, player, room, x, y, 0, 0);
		int32_t xy[2] = {};
		d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, xy);
		plog::Info("unique maps: Outer Void link %d to (%d, %d), player at (%d, %d) (%u)", link, x, y, xy[0], xy[1],
		           ++g_voidWarps);
		return true;
	}
	return false;
}

auto InstallDungeonBarriers(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	g_initQuestDoor = reinterpret_cast<InitFn>(d2r::imageBase + InitQuestDoor);
	const uint64_t expected = d2r::imageBase + InitQuestDoor;
	return context->PatchWriteU64(ObjectInitTable + static_cast<uint64_t>(ObjectInitQuestDoor) * 8, &expected,
	                              sizeof(expected), reinterpret_cast<uint64_t>(static_cast<InitFn>(BarrierInit)));
}

auto InstallUniqueMaps(const D2RL::PluginContext* context) noexcept -> bool {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize) ||
	    context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		return false;
	}
	const D2RL::Lifecycle::DataTablesLoadedListener listener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                          .callback = ReadUniqueBases};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	return lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success;
}

auto UniqueMapsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "unique maps: %u boss deaths, %u portals, %u drops, %u pillars killed, %d random unique bases, %u Outer "
	              "Void bosses, %u Outer Void warps, %u dungeon blocker presets, %u barriers opened",
	              g_deaths.load(), g_portals.load(), g_drops.load(), g_pillars.load(), g_anyCount.load(),
	              g_voidBosses.load(), g_voidWarps.load(), g_barriers.load(), g_barriersOpened.load());
}
