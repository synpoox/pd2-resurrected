// PD2's map events (ProjectDiablo.dll; docs/research/pd2-map-system-dll.md, section 4). Opening a tiered map rolls
// one event for the game (0x10383060): rare and better 34%, magic 17%, normal 11%, always with `map_force_event`
// (a Catalyst Shard); then up to 40 draws of 12, the first naming an event wins (Altar of the Catalyst never when
// forced). The event spawns as the map's rooms activate (0x1036fef0), once the level's progress, rooms activated x
// 100 / PD2's room count for the level, is far enough; it spawns at the activating room's centre.
//
// Shadow of Mendeln, Horazon and Gheed spawn once (Gheed with an event marker object), Dark Wanderer and Treasure Fallen
// up to three times. The Altar of the Catalyst places its altar once: the first use calls its guardian, whose death
// (event_portals.cpp's listener) lets the second use drop 2-3 Catalyst Shards. The Spire of Darkness places its altar
// and three fog objects; once used, the rooms send shadow waves until the progress passes 90, which kills them. Void /
// Tomb opens a portal pair to the Bitter Depths with a marker. The invaders (MS10) are rolled and logged but spawn
// nothing yet. Horazon's death is event_portals.cpp's. The altars' use is PD2's operate 79 (0x103aa460) in D2R's free
// operate slot 35 (pd2_ids.FUNCS), written into D2R's operate table as map_open.cpp writes slot 60.
//
// Each spawn sends the event's notify string to every player as a red chat line (#336; PD2 0x1037dbc0, packet 0x26
// type 4, colour 1). The SDK's localization service gives the text; no SDK service sends a chat line, so it goes
// through D2R's chat packet sender (rva::SendChat).
//
// No PluginSDK service reports a room activating, so this hooks D2R's room monster spawn (rva::RoomPopulate), the
// function PD2 wraps. The event and the counts live in D2R's per-game counters (rva::GameCounter), so a new game
// starts without them.
#include "map_events.h"

#include "event_portals.h"
#include "level_fx.h"
#include "log.h"
#include "lucion.h"
#include "map_open.h"
#include "rathma.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/localization.h>

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr uint32_t EventKey = 0x4d455654;   // 'MEVT': the event (PD2's game +0x1df4), 10 for none rolled
constexpr uint32_t SpawnsKey = 0x4d45564e;  // 'MEVN': the event's spawns so far (PD2's game +0x2628)
// 'MR' and the level: rooms activated in it, the 1.13c monster region's +0x04 that PD2's progress reads
constexpr auto RoomsKey(int32_t level) noexcept -> uint32_t {
	return 0x4d520000u | static_cast<uint32_t>(level & 0xffff);
}

constexpr int32_t StatForceEvent = 553;  // map_force_event (PD2 500)
constexpr int32_t QualityMagic = 4;  // rva::QualityNormal is 2
constexpr int32_t NoEvent = 10;
enum : int32_t {
	Mendeln = 11, Horazon = 12, DarkWanderer = 14, TreasureFallen = 15, Spire = 16, Gheed = 18, Altar = 19,
	Invaders = 20, Tomb = 21,
};
// PD2's draw (rand() % 12) -> event; 0 draws again
constexpr int32_t Draws[12] = {0, Mendeln, Horazon, 0, DarkWanderer, TreasureFallen, Spire, 0, Gheed, Altar, Invaders,
                               Tomb};
// The events' monsters, PD2 922 nihlathakMap, 921 darkwandererMap and 945 treasurefallenMap in port monstats ids
constexpr int32_t MendelnMonster = 939, DarkWandererMonster = 938, TreasureFallenMonster = 962;
constexpr int32_t GheedMonster = 147, HorazonMonster = 936;  // PD2 919 summonerMap
constexpr int32_t EventMarker = 597;  // PD2 object 594 empty_eventmarker: invisible, AutoMap 1499
constexpr int32_t ShardAltar = 614;   // PD2 611 ForceShardAltar
const uint32_t ShardCode = D2RL::Items::MakeItemCode("iwss");  // the Catalyst Shard
constexpr int32_t SpireAltar = 596;   // PD2 593 TaintedSunMapEventAltar
constexpr int32_t Fog = 374;          // PD2 374, D2R's Fog (the same row)
// PD2's waves: 993-995 shadowofhate, shadowofterror, shadowofdestruction
constexpr int32_t Shadows[3] = {1010, 1011, 1012};
constexpr int32_t SpireOver = 50;     // PD2's spawn count once 0x1036eb30 ends the waves
constexpr int32_t BitterDepths = 198;  // PD2 207 Tomb Event Map, Act V
// PD2's Spire sounds (sounds.txt rows): 0xa78 shrine_storm and 0x24 on the use, 0x26d bloodraven_mist_end at the end
constexpr int32_t SpireSounds[2] = {0xa78, 0x24}, SpireEndSound = 0x26d;
// PD2's Catalyst altar sounds: 0x29c once the guardian is called, 0x23 with the shards, 0x26d on the guardian's death
constexpr int32_t GuardianSound = 0x29c, ShardSound = 0x23, GuardianDeathSound = 0x26d;
constexpr int32_t GuardianClone = 1082;  // GuardianOfFateClone (PD2 1065)
constexpr int32_t ModeNeutral = 1;
constexpr int32_t UnitObject = 2;

struct Tiered {
	int16_t level;  // port level id
	int16_t rooms;  // PD2's 0x1038b1d0: the level's room count for the progress, 200 when it names none
};
// The tiered maps, which alone roll an event: PD2's Horazon lists (0x10377ca0), which match the misc `len` tiers,
// in port level ids (merge/ids-levels.tsv)
constexpr Tiered TieredLevels[] = {
	{144, 200}, {146, 270}, {147, 180}, {149, 240}, {152, 170}, {156, 160}, {158, 120}, {165, 220}, {166, 200},
	{199, 200},  // tier 1
	{142, 210}, {143, 220}, {151, 190}, {157, 170}, {159, 240}, {170, 210}, {171, 230}, {189, 280}, {200, 200},
	{201, 200},  // tier 2
	{140, 240}, {145, 220}, {148, 210}, {150, 180}, {155, 190}, {167, 210}, {186, 180}, {187, 260}, {197, 270},  // tier 3
};
static_assert(std::size(TieredLevels) == 29);
// PD2's unique map values (0x1e-0x24) by entry level, and its dungeon set 0x105df228, in port level ids
struct UniqueValue {
	int16_t level;
	int16_t value;
};
constexpr UniqueValue UniqueValues[] = {
	{172, 0x1e},  // Zhar's Sanctum (PD2 176)
	{177, 0x1f},  // Warlord of Blood (181)
	{179, 0x20},  // Fallen Gardens (183)
	{176, 0x21},  // Outer Void (180)
	{188, 0x22},  // Imperial Palace (192)
	{191, 0x23},  // City of Ureh (195)
	{193, 0x24},  // Djinn's Domain (197)
};
constexpr int32_t Dungeons[] = {163, 168, 182};  // Plains of Torment, Sanctuary of Sin, Steppes (PD2 164, 171, 186)
constexpr int32_t DungeonEvent = 9;
// PD2 0x1036fef0: with the Warlord of Blood's value, a room of the Hellcave Fortress (port 178, PD2 182) that overlaps
// this box (subtiles) gets no monsters, the Warlord's arena
constexpr int32_t WarlordEvent = 0x1f, Fortress = 178;
constexpr int32_t ArenaX0 = 0x5a10, ArenaX1 = 0x5a88, ArenaY0 = 0x25c2, ArenaY1 = 0x263a;
// PD2's 0x1037d900 per axis: [lo, hi] meets the room's [start, start + size)
constexpr auto Overlaps(int32_t lo, int32_t hi, int32_t start, int32_t size) noexcept -> bool {
	return lo < start + size && hi >= start;
}
static_assert(Overlaps(10, 20, 20, 5) && !Overlaps(10, 20, 21, 5) && Overlaps(10, 20, 0, 11) && !Overlaps(10, 20, 0, 10));
std::atomic<uint32_t> g_arenaRooms {0};

using PopulateFn = void (*)(Game* game, void* room) noexcept;
PopulateFn g_populateOriginal = nullptr;

std::atomic<uint32_t> g_rolls {0};
std::atomic<uint32_t> g_rolled[12] {};  // by event - 10
std::atomic<uint32_t> g_rooms {0};  // rooms activated anywhere
std::atomic<uint32_t> g_spawned {0};
std::atomic<uint32_t> g_noSpot {0};
std::atomic<uint32_t> g_notified {0};

const D2RL::PluginContext* g_context = nullptr;
const D2RL::LocalizationService* g_strings = nullptr;

// PD2's notify string by event (docs/research/pd2-map-system-dll.md, section 4)
auto NotifyKey(int32_t event) noexcept -> const char* {
	switch (event) {
	case Mendeln: return "d2r:MapEventNotifyMendeln";
	case Horazon: return "d2r:MapEventNotifyHorazon";
	case DarkWanderer: return "d2r:MapEventNotifyDarkWanderer";
	case TreasureFallen: return "d2r:MapEventNotifyTreasure";
	case Spire: return "d2r:MapEventNotifyTainted";
	case Gheed: return "d2r:MapEventNotifyGheed";
	case Altar: return "d2r:MapEventNotifyShard";
	case Invaders: return "d2r:MapEventNotifyInvaders";
	case Tomb: return "d2r:MapEventNotifyTomb";
	default: return nullptr;
	}
}

// Every spawn sends it, as PD2's does (Dark Wanderer and Treasure Fallen up to three times)
auto Notify(Game* game, int32_t event) noexcept -> void {
	const char* key = NotifyKey(event);
	char text[256] {};
	uint32_t need = 0;
	if (key == nullptr || g_strings == nullptr ||
	    g_strings->getStringByKey(g_context, key, text, sizeof(text), &need) != D2RL::Localization::Result::Success) {
		plog::Warn("map events: no notify text for event %d (%s)", event, key != nullptr ? key : "no key");
		return;
	}
	// PD2's packet: type 4 (a line with no sender), red
	uint8_t packet[0x147] {};
	packet[0] = 0x26;
	packet[1] = 4;
	packet[8] = 1;
	std::memcpy(packet + 0x47, text, sizeof(text) - 1);
	d2r::Engine<void (*)(void*, uint8_t*) noexcept>(d2r::rva::SendChat)(At<void*>(game, d2r::rva::GameClients), packet);
	plog::Info("map events: event %d notified \"%s\" (%u)", event, text, ++g_notified);
}

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// Each Counter call may insert, so no pointer is kept across another call.
auto Get(Game* game, uint32_t key) noexcept -> int32_t {
	const int32_t* c = Counter(game, key);
	return c != nullptr ? *c : 0;
}

auto Set(Game* game, uint32_t key, int32_t value) noexcept -> void {
	if (int32_t* c = Counter(game, key); c != nullptr) {
		*c = value;
	}
}

auto Tier(int32_t level) noexcept -> const Tiered* {
	for (const Tiered& t : TieredLevels) {
		if (t.level == level) {
			return &t;
		}
	}
	return nullptr;
}

// PD2 rolls with the CRT's rand(), whose state is per thread and starts at seed 1: seed each thread once
auto Rand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

// The events' spot search: PD2 passes size, mask 0x805, reach 8 and the -2, -2 move (0x1038a170)
auto FindSpot(void*& room, int32_t xy[2], int32_t size) noexcept -> bool {
	return FindEventSpot(room, xy, size, 0x805, 8, true);
}

auto SpawnMonsterAt(Game* game, void* room, const int32_t (&xy)[2], int32_t monster) noexcept -> Unit* {
	return d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		d2r::rva::SpawnMonster)(game, room, xy[0], xy[1], monster, ModeNeutral, -1, 0x42);
}

// PD2's 0x1038a060 with unit type 2: an object at x/y, in mode 0 (PD2 passes `type != 2` as the mode: an altar in mode 1
// is not selectable, so no click reaches its operate). No PluginSDK service places objects, so this calls D2R's unit
// spawn (rva::SpawnUnit, argument 8 the mode) as OpenPortalAt (map_open.cpp) does.
// shortcut: PD2 also sets bits 0x3000000 of the 1.13c unit flags (+0xc4), not identified; the altars work without them
auto SpawnObjectAt(Game* game, void* room, int32_t x, int32_t y, int32_t row) noexcept -> Unit* {
	using namespace d2r::rva;
	using SpawnFn = Unit* (*)(int32_t, int32_t, int32_t, int32_t, Game*, void*, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(room, x, y);
	return room != nullptr ? d2r::Engine<SpawnFn>(SpawnUnit)(UnitObject, row, x, y, game, room, 1, 0, 0, 0) : nullptr;
}

// PD2's 0x1038a170: its spot search from `xy` with `size`, then the monster there
auto SpawnMonsterNear(Game* game, void* room, int32_t (&xy)[2], int32_t monster, int32_t size) noexcept -> Unit* {
	return FindSpot(room, xy, size) ? SpawnMonsterAt(game, room, xy, monster) : nullptr;
}

// PD2's 0x1036eb30, once the Spire's progress passes 90: the waves end and every shadow in the game dies, without
// drops. Picked first, so no kill runs while the lists are walked.
// It also lifts the level's act darkness (0x1037dc70 with 0) and plays a sound to the level's players (0x1037f240).
auto EndSpire(Game* game, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	Set(game, SpawnsKey, SpireOver);
	const bool dark = SetLevelDarkness(game, level, false);
	PlayLevelSound(game, level, SpireEndSound);
	constexpr int Max = 256;
	Unit* found[Max] = {};
	int n = 0;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* unit = At<Unit*>(game, GameMonsters + bucket * 8); unit != nullptr && n < Max;
		     unit = At<Unit*>(unit, UnitListNext)) {
			const int32_t classId = At<int32_t>(unit, UnitClassId);
			if (std::find(std::begin(Shadows), std::end(Shadows), classId) != std::end(Shadows) &&
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
				found[n++] = unit;
			}
		}
	}
	for (int i = 0; i < n; ++i) {
		KillQuietly(game, found[i]);
	}
	plog::Info("map events: event %d over in level %d, %d shadows killed, darkness off%s", Spire, level, n,
	           dark ? "" : " FAILED (no player in the level)");
}

// PD2's Spire of Darkness branch: with no spawn yet, a 20% roll once the level has more than 15 rooms places the altar
// (at a random point of the room, then the spot search) and three fog objects by it. The altar's use (OperateAltar)
// sets 2; from then each room rolls rand() % 6: 1-3 send a group of one shadow class around its centre. Past progress
// 90 the waves end.
auto SpawnSpire(Game* game, void* room, int32_t (&xy)[2], int32_t level, int32_t rooms, int32_t progress,
                int32_t spawns, int32_t roll, int32_t size) noexcept -> void {
	using namespace d2r::rva;
	if (spawns < 1) {
		if (roll >= 20 || rooms <= 15) {
			return;
		}
		xy[0] = At<int32_t>(room, RoomX) + Rand() % At<int32_t>(room, RoomWidth);  // PD2's 0x10389ea0
		xy[1] = At<int32_t>(room, RoomY) + Rand() % At<int32_t>(room, RoomHeight);
		if (!FindSpot(room, xy, size) || SpawnObjectAt(game, room, xy[0], xy[1], SpireAltar) == nullptr) {
			plog::Warn("map events: event %d found no spot for its altar in level %d (%u)", Spire, level, ++g_noSpot);
			return;
		}
		const int32_t fogs[3][2] = {{xy[0] + 2, xy[1]}, {xy[0], xy[1] + 2}, {xy[0] - 1, xy[1] - 1}};
		int placed = 0;
		for (const auto& f : fogs) {
			placed += SpawnObjectAt(game, room, f[0], f[1], Fog) != nullptr ? 1 : 0;
		}
		Set(game, SpawnsKey, 1);
		plog::Info("map events: event %d altar %d at (%d, %d) in level %d, %d of 3 fog %d (%u spawned)", Spire,
		           SpireAltar, xy[0], xy[1], level, placed, Fog, ++g_spawned);
		Notify(game, Spire);
		return;
	}
	if (spawns < 2 || spawns >= SpireOver) {
		return;
	}
	if (const int pick = Rand() % 6; pick >= 1 && pick <= 3) {
		// PD2 reads the group size from the class's monstats record (MinGrp, MaxGrp)
		const auto* rec = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
			d2r::DataFlag(game), Shadows[pick - 1]);
		const int32_t minGrp = rec != nullptr ? At<uint8_t>(rec, MonMinGrp) : 0;
		const int32_t maxGrp = rec != nullptr ? At<uint8_t>(rec, MonMaxGrp) : 0;
		int32_t count = maxGrp - minGrp + 1;
		count = count > 1 ? minGrp + Rand() % count : count;
		int32_t made = 0;
		for (int32_t i = 0; i < count; ++i) {
			int32_t at[2] = {xy[0] - 5 + Rand() % 10, xy[1] - 5 + Rand() % 10};
			made += SpawnMonsterNear(game, room, at, Shadows[pick - 1], 10) != nullptr ? 1 : 0;
		}
		plog::Info("map events: event %d wave of %d/%d monster %d in level %d at progress %d", Spire, made, count,
		           Shadows[pick - 1], level, progress);
	}
	if (progress > 90) {
		EndSpire(game, level);
	}
}

// PD2's 0x1036fef0, before the room's own monsters: one roll of 100 per room, against the event's rule. The spot
// search runs from the room's centre with size 12 (8 in Arcane and River of Blood).
// shortcut: PD2 also skips a room its 0x103a2e90 (an engine call through a pointer, not read) refuses, and marks
// Mendeln's monster (+0xd0 = 11); read both if an event spawns where PD2's would not, or Mendeln acts differently
auto SpawnEvent(Game* game, void* room, int32_t level, int32_t rooms, int32_t progress) noexcept -> void {
	using namespace d2r::rva;
	constexpr int32_t Arcane = 143, RiverOfBlood = 145;  // PD2 0x8e and 0x90 in port level ids
	const int32_t event = Get(game, EventKey);
	const int32_t spawns = Get(game, SpawnsKey);
	const int32_t roll = Rand() % 100;
	const int32_t size = level == Arcane || level == RiverOfBlood ? 8 : 12;
	int32_t xy[2] = {At<int32_t>(room, RoomX) + At<int32_t>(room, RoomWidth) / 2,
	                 At<int32_t>(room, RoomY) + At<int32_t>(room, RoomHeight) / 2};
	if (event == Spire) {
		SpawnSpire(game, room, xy, level, rooms, progress, spawns, roll, size);
		return;
	}
	int32_t id = -1;  // a monster; for the Altar and Void/Tomb an object
	if ((event == Mendeln || event == Horazon || event == Gheed || event == Altar || event == Tomb) && spawns == 0 &&
	    roll + progress > 140) {
		id = event == Mendeln ? MendelnMonster : event == Horazon ? HorazonMonster : event == Gheed ? GheedMonster
			: event == Altar ? ShardAltar : RedPortal;
	} else if ((event == DarkWanderer || event == TreasureFallen) && spawns < 3 && (spawns + 1) * 25 < progress &&
	           roll < 25) {
		id = event == DarkWanderer ? DarkWandererMonster : TreasureFallenMonster;
	}
	if (id < 0) {
		return;
	}
	if (!FindSpot(room, xy, size)) {
		plog::Warn("map events: event %d found no spot for %d in level %d (%u)", event, id, level, ++g_noSpot);
		return;
	}
	// PD2's Void/Tomb places xactportal_gold here and one in level 207, then links the two (0x1038a510); D2R's portal
	// maker makes the same linked pair, of its red portal 60 (docs/deviations.md)
	const Unit* made = event == Tomb    ? OpenEventPortal(game, room, xy[0], xy[1], BitterDepths)
	                   : event == Altar ? SpawnObjectAt(game, room, xy[0], xy[1], ShardAltar)
	                                    : SpawnMonsterAt(game, room, xy, id);
	if (made == nullptr) {
		plog::Warn("map events: event %d could not place %d at (%d, %d) in level %d", event, id, xy[0], xy[1], level);
		return;
	}
	Set(game, SpawnsKey, spawns + 1);
	plog::Info("map events: event %d spawn %d, %s %d at (%d, %d) in level %d at progress %d (%u spawned)", event,
	           spawns + 1, event == Tomb || event == Altar ? "object" : "monster", id, xy[0], xy[1], level, progress,
	           ++g_spawned);
	// PD2's Gheed branch also files the game's players into a set for its realm (0x10368480): cut, docs/deviations.md
	if (event == Gheed || event == Tomb) {
		const bool marked = SpawnObjectAt(game, room, xy[0], xy[1], EventMarker) != nullptr;
		plog::Info("map events: event %d object %d at (%d, %d)%s", event, EventMarker, xy[0], xy[1],
		           marked ? "" : " FAILED");
	}
	Notify(game, event);
}

// PD2's operate 79 (0x103aa460) for both event altars, in D2R's operate slot 35. The Spire's first use starts the
// waves. The Catalyst altar's first use calls its guardian 10-29 subtiles off (40 tries, spot size 2), its second,
// once the guardian is dead (AltarGuardianDied), drops 2-3 Catalyst Shards (PD2 at item level 90).
// Each use also plays its sounds to the user's level (0x1037f240); the Spire's darkens the act (0x1037dc70).
// shortcut: the sounds play on each player, where PD2 plays them on the altar (its unit in packet 0x2c): D2R's sound
// sender (rva::SoundPacket) is only known to work for a player; the user stands by the altar
// shortcut: the shards drop by the user, where PD2 drops them by the altar (0x1036ebc0); D2R's drop (rva::DropItemAt)
// is only known to work by a unit that carries a level stat
auto OperateAltar(ObjectOperate* args) noexcept -> int32_t {
	using namespace d2r::rva;
	Game* game = args->game;
	Unit* altar = args->object;
	if (game == nullptr || altar == nullptr || (args->classId != SpireAltar && args->classId != ShardAltar)) {
		return 0;
	}
	const int32_t event = Get(game, EventKey);
	const int32_t spawns = Get(game, SpawnsKey);
	plog::Info("map events: altar %d used, event %d, state %d", args->classId, event, spawns);
	const auto mode = d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode);
	// PD2 reads the user's level (0x1030aa50 of its room); a level of -1 plays to no one
	void* userRoom = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(args->player != nullptr ? args->player : altar);
	const int32_t level = userRoom != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(userRoom) : -1;
	if (event == Spire && spawns == 1) {
		mode(altar, 1);
		SetDarkness(game, altar, true);
		for (const int32_t sound : SpireSounds) {
			PlayLevelSound(game, level, sound);
		}
		Set(game, SpawnsKey, 2);
		plog::Info("map events: event %d altar used, darkness on, the waves start", event);
		return 1;
	}
	if (event == Altar && spawns == 1) {
		mode(altar, 1);
		if (const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(
				d2r::DataFlag(game), args->classId);
		    record != nullptr) {
			const int32_t open = At<int32_t>(game, GameFrame) + 1 + static_cast<int32_t>(At<uint32_t>(record, ObjOpenFrames) >> 8);
			d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
				game, altar, UnitEventOpen, open, 0, 0, 0);
		}
		void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(altar);
		for (int i = 0; room != nullptr && i < 40; ++i) {
			int32_t xy[2] = {};
			d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(altar, xy);
			xy[0] += 10 + Rand() % 20;
			xy[1] += 10 + Rand() % 20;
			if (SpawnMonsterNear(game, room, xy, AltarGuardian, 2) != nullptr) {
				PlayLevelSound(game, level, GuardianSound);
				Set(game, SpawnsKey, 2);
				plog::Info("map events: event %d altar used, guardian %d at (%d, %d), sound 0x%x", event, AltarGuardian,
				           xy[0], xy[1], GuardianSound);
				return 1;
			}
		}
		plog::Warn("map events: event %d altar used, no spot for its guardian", event);
		return 0;
	}
	if (event == Altar && spawns == 3 && args->player != nullptr) {
		mode(altar, 3);
		const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
		const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(dataFlag, ShardCode);
		const uint32_t count = 2 + d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(altar), 2);
		for (uint32_t i = 0; row >= 0 && i < count; ++i) {
			d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
				game, args->player, row, QualityNormal, 0, 0);
		}
		PlayLevelSound(game, level, ShardSound);
		Set(game, SpawnsKey, 4);
		plog::Info("map events: event %d altar used again, %u Catalyst Shards (item row %d), sound 0x%x", event, count, row,
		           ShardSound);
		return 1;
	}
	return 0;
}

auto OnRoomPopulate(Game* game, void* room) noexcept -> void {
	const int32_t level = game != nullptr && room != nullptr
		? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room)
		: -1;
	const Tiered* tier = Tier(level);
	if (plog::Sample(g_rooms)) {
		plog::Info("map events: room activated in level %d (%u so far)", level, g_rooms.load());
	}
	if (tier != nullptr) {
		const int32_t rooms = Get(game, RoomsKey(level));
		SpawnEvent(game, room, level, rooms, rooms * 100 / tier->rooms);
	}
	if (RathmaRoom(game, room, level)) {
		return;  // PD2 0x1036fef0: an arena room spawns none of its own monsters
	}
	using namespace d2r::rva;
	if (level == Fortress && Get(game, EventKey) == WarlordEvent &&
	    Overlaps(ArenaX0, ArenaX1, At<int32_t>(room, RoomX), At<int32_t>(room, RoomWidth)) &&
	    Overlaps(ArenaY0, ArenaY1, At<int32_t>(room, RoomY), At<int32_t>(room, RoomHeight))) {
		plog::Info("map events: Warlord arena room at (%d, %d) left empty (%u)", At<int32_t>(room, RoomX),
		           At<int32_t>(room, RoomY), ++g_arenaRooms);
		return;
	}
	g_populateOriginal(game, room);
	if (tier == nullptr) {
		return;
	}
	const int32_t rooms = Get(game, RoomsKey(level)) + 1;
	Set(game, RoomsKey(level), rooms);
	if (rooms % 10 == 0) {  // for the game check
		plog::Info("map events: level %d, %d rooms activated (progress %d), event %d, %d spawned", level, rooms,
		           rooms * 100 / tier->rooms, Get(game, EventKey), Get(game, SpawnsKey));
	}
}

}  // namespace

// PD2's 0x10389c20 (#530): a breadth-first search over a 20 x 20 grid of 4-subtile cells around x/y (cells 10 +-
// reach, reach at most 10, at most 400 steps); each cell's point takes its room and must have a size x size box free
// of `mask`. The first free point wins, moved by -2, -2 with `shift` (PD2's last argument).
// shortcut: the box is centred on the point, as 1.13c's COLLISION_CheckMaskWithSizeXY centres it (PD2 calls it
// through an import table not read here); read 0x103097a0's target if events spawn off by half a box
auto FindEventSpot(void*& room, int32_t xy[2], int32_t size, uint16_t mask, int32_t reach, bool shift) noexcept -> bool {
	using namespace d2r::rva;
	constexpr int Grid = 20, Centre = 10, Step = 4, MaxSteps = 400;
	const int Reach = reach < 10 ? reach : 10;
	const auto roomAt = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY);
	const auto at = d2r::Engine<uint64_t (*)(void*, int32_t, int32_t, uint16_t) noexcept>(CollisionAt);
	bool seen[Grid][Grid] {};
	int8_t queue[Grid * Grid][2];
	int head = 0, tail = 0;
	queue[tail][0] = Centre, queue[tail][1] = Centre, ++tail;
	seen[Centre][Centre] = true;
	for (int steps = 0; head < tail && steps <= MaxSteps; ++steps) {
		const int cx = queue[head][0], cy = queue[head][1];
		++head;
		const int32_t x = xy[0] + (cx - Centre) * Step, y = xy[1] + (cy - Centre) * Step;
		void* here = roomAt(room, x, y);
		bool free = here != nullptr;
		for (int32_t dx = -size / 2; free && dx < size - size / 2; ++dx) {
			for (int32_t dy = -size / 2; free && dy < size - size / 2; ++dy) {
				free = at(here, x + dx, y + dy, mask) == 0;
			}
		}
		if (free) {
			xy[0] = shift ? x - 2 : x, xy[1] = shift ? y - 2 : y;
			room = roomAt(here, xy[0], xy[1]);
			return room != nullptr;
		}
		constexpr int Dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
		for (const auto& d : Dirs) {
			const int nx = cx + d[0], ny = cy + d[1];
			if (nx >= Centre - Reach && nx < Centre + Reach && ny >= Centre - Reach && ny < Centre + Reach &&
			    !seen[nx][ny]) {
				seen[nx][ny] = true;
				queue[tail][0] = static_cast<int8_t>(nx), queue[tail][1] = static_cast<int8_t>(ny), ++tail;
			}
		}
	}
	return false;
}

auto MapEvent(Game* game) noexcept -> int32_t {
	return game != nullptr ? Get(game, EventKey) : 0;
}

auto MapEventStep(Game* game) noexcept -> int32_t {
	return game != nullptr ? Get(game, SpawnsKey) : 0;
}

auto SetMapEventStep(Game* game, int32_t value) noexcept -> void {
	if (game != nullptr) {
		Set(game, SpawnsKey, value);
	}
}

auto RollMapEvent(Game* game, Unit* item, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr || item == nullptr) {
		return;
	}
	// PD2 0x10383060: a unique map's own value, 9 for a dungeon, 10 for any other untiered map; tiered maps roll
	int32_t fixed = Tier(level) == nullptr ? NoEvent : 0;
	for (const UniqueValue& u : UniqueValues) {
		fixed = u.level == level ? u.value : fixed;
	}
	for (const int32_t d : Dungeons) {
		fixed = d == level ? DungeonEvent : fixed;
	}
	if (fixed != 0) {
		Set(game, EventKey, fixed);
		Set(game, SpawnsKey, 0);
		plog::Info("map events: level %d set event 0x%x (no roll)", level, fixed);
		return;
	}
	const bool forced = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(item, StatForceEvent, 0) > 0;
	const int32_t quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item);
	const int32_t chance = quality == QualityNormal ? 11 : quality == QualityMagic ? 17 : 34;
	int32_t event = 0;
	if (forced || Rand() % 100 < chance) {
		for (int draw = 0; draw < 40 && event == 0; ++draw) {
			const int32_t e = Draws[Rand() % 12];
			event = forced && e == Altar ? 0 : e;
		}
	}
	event = event != 0 ? event : NoEvent;
	// For game checks only, not PD2's: PD2R_MAP_EVENT=<11-21> in the game's environment rolls that event on every map
	static const int32_t testEvent = [] {
		char v[8] {};
		size_t n = 0;
		const int32_t e = getenv_s(&n, v, sizeof(v), "PD2R_MAP_EVENT") == 0 && n > 0 ? std::atoi(v) : 0;
		return e > NoEvent && e <= Tomb && NotifyKey(e) != nullptr ? e : 0;
	}();
	event = testEvent != 0 ? testEvent : event;
	Set(game, EventKey, event);
	Set(game, SpawnsKey, 0);
	++g_rolled[event - NoEvent];
	plog::Info("map events: level %d (quality %d%s) rolled event %d (%u rolls)", level, quality,
	           testEvent != 0 ? ", PD2R_MAP_EVENT" : forced ? ", forced" : "", event, ++g_rolls);
}

// PD2 0x10378080, in state 2 of the Altar event: with the guardian in a room, a sound on the guardian to every player
// in game, state 3, and every clone in the guardian's room and its neighbours (1.13c's near-room list) dies without a
// drop (0x10356e00). Picked first, so no kill runs while the room lists are walked.
// shortcut: the sound plays on each player, not on the guardian (see OperateAltar); a clone already dead is skipped,
// where PD2 kills it again
auto AltarGuardianDied(Game* game, Unit* guardian) noexcept -> void {
	using namespace d2r::rva;
	if (MapEvent(game) != Altar || Get(game, SpawnsKey) != 2) {
		return;
	}
	if (guardian == nullptr) {  // not PD2's: the death listener found no unit; the shards still come, no clone dies
		Set(game, SpawnsKey, 3);
		plog::Warn("map events: event %d guardian dead, its unit not found: no clone killed", Altar);
		return;
	}
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(guardian);
	if (room == nullptr) {
		plog::Warn("map events: event %d guardian dead outside a room, state kept", Altar);
		return;
	}
	PlayLevelSound(game, 0, GuardianDeathSound);
	Set(game, SpawnsKey, 3);
	void** rooms = nullptr;
	int32_t count = 0;
	d2r::Engine<void (*)(void*, void***, int32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	constexpr int Max = 64;
	Unit* clones[Max] = {};
	int n = 0;
	for (int32_t i = 0; rooms != nullptr && i < count; ++i) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[i]); unit != nullptr && n < Max;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			if (At<int32_t>(unit, UnitType) == UnitMonster && At<int32_t>(unit, UnitClassId) == GuardianClone &&
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
				clones[n++] = unit;
			}
		}
	}
	for (int i = 0; i < n; ++i) {
		KillQuietly(game, clones[i]);
	}
	plog::Info("map events: event %d guardian dead, %d clones killed in %d rooms, sound 0x%x; the altar gives its shards",
	           Altar, n, count, GuardianDeathSound);
}

auto InstallMapEvents(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	g_context = context;
	if (context->QueryService(&g_strings) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLocalizationServiceField(g_strings, D2RL::LocalizationServiceRequiredSize)) {
		g_strings = nullptr;  // the events still spawn, without their chat line
	}
	const bool rooms = context->InstallInlineHook(RoomPopulate, RoomPopulateEntry, sizeof(RoomPopulateEntry),
	                                              OnRoomPopulate, &g_populateOriginal);
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool altars = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperateAltar) * 8, Empty,
	                                           sizeof(Empty), reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperateAltar)));
	std::snprintf(report, reportSize, "room activation%s, altar operate %d%s%s", rooms ? "" : " FAILED (bytes differ)",
	              ObjectOperateAltar, altars ? "" : " FAILED (slot not empty)",
	              g_strings != nullptr ? "" : ", no notify (no localization service)");
	return rooms ? 1 : 0;
}

auto MapEventsCounters(char* out, size_t size) noexcept -> void {
	int n = std::snprintf(out, size, "map events: %u rolls (by event 10-21:", g_rolls.load());
	for (const auto& r : g_rolled) {
		n += n > 0 && static_cast<size_t>(n) < size ? std::snprintf(out + n, size - n, " %u", r.load()) : 0;
	}
	if (n > 0 && static_cast<size_t>(n) < size) {
		std::snprintf(out + n, size - n, "), %u rooms activated, %u spawned, %u without a spot, %u notified", g_rooms.load(),
		              g_spawned.load(), g_noSpot.load(), g_notified.load());
	}
}
