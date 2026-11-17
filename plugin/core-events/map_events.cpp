// PD2's map events (ProjectDiablo.dll; docs/research/pd2-map-system-dll.md, section 4), kept for the native Cartographer
// maps (PD2's own tiered and unique maps are cut, #964). The first room of a native map rolls
// one event for the game (0x10383060): rare and better 34%, magic 17%, normal 11%, always with `map_force_event`
// (a Catalyst Shard); then up to 40 draws of 12, the first naming an event wins (Altar of the Catalyst never when
// forced). The event spawns as the map's rooms activate (0x1036fef0), once the level's progress, rooms activated x
// 100 / PD2's room count for the level, is far enough; it spawns at the activating room's centre.
//
// Shadow of Mendeln, Horazon and Gheed spawn once (Gheed with an event marker object), Dark Wanderer and Treasure Fallen
// up to three times. The Altar of the Catalyst places its altar once: the first use calls its guardian, whose death
// (event_portals.cpp's listener) lets the second use drop 2-3 Catalyst Shards. The Spire of Darkness places its altar
// and three fog objects; once used, the rooms send shadow waves until the progress passes 90, which kills them. Void /
// Tomb opens a portal pair to the Bitter Depths with a marker. The invaders (MS10) place up to three portals; the
// portal's think and the invaders' drops are lane-c/invaders.cpp's. Horazon's death is event_portals.cpp's. The altars' use is PD2's operate 79 (0x103aa460) in D2R's free
// operate slot 35 (pd2_port.FUNCS), which also serves the Temple's Sacrifice Altar (Lucion's Desecration), written into D2R's operate table as map_open.cpp writes slot 60.
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
#include "placements.h"
#include "lucion.h"
#include "map_open.h"
#include "rathma.h"

#include "cartographer_room_populate_v1.h"
#include "event_link.h"
#include "event_roll.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/localization.h>
#include <D2RLPlugin/plugin_communication.h>

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

// 'MT' and the level: its room count, read once from the built level (native maps only)
constexpr auto TotalKey(int32_t level) noexcept -> uint32_t {
	return 0x4d540000u | static_cast<uint32_t>(level & 0xffff);
}

constexpr int32_t StatForceEvent = 553;  // map_force_event (PD2 500)
constexpr int32_t QualityMagic = pd2::event_roll::QualityMagic;  // rva::QualityNormal is 2
constexpr int32_t UnitItem = 4;      // D2 unit type of an item
constexpr int32_t NoEvent = 10;
enum : int32_t {
	Mendeln = 11, Horazon = 12, DarkWanderer = 14, TreasureFallen = 15, Spire = 16, Gheed = 18, Altar = 19,
	Invaders = 20, Tomb = 21,
};
// The events' monsters, PD2 922 nihlathakMap, 921 darkwandererMap and 945 treasurefallenMap in port monstats ids
constexpr int32_t MendelnMonster = 939, DarkWandererMonster = 938, TreasureFallenMonster = 962;
constexpr int32_t InvaderPortalMonster = 1199;  // monstats InvaderPortal (monai Overseer): the invaders' portal (lane-c/invaders.cpp)
constexpr int32_t GheedMonster = 147, HorazonMonster = 936;  // PD2 919 summonerMap
constexpr int32_t EventMarker = 597;  // PD2 object 594 empty_eventmarker: invisible, AutoMap 1499
constexpr int32_t ShardAltar = 614;   // PD2 611 ForceShardAltar
const uint32_t ShardCode = D2RL::Items::MakeItemCode("iwss");  // the Catalyst Shard
constexpr int32_t SacrificeAltar = 624;  // PD2 621
constexpr int32_t SpireAltar = 596;   // PD2 593 TaintedSunMapEventAltar
constexpr int32_t Fog = 374;          // PD2 374, D2R's Fog (the same row)
// PD2's waves: 993-995 shadowofhate, shadowofterror, shadowofdestruction
constexpr int32_t Shadows[3] = {1010, 1011, 1012};
constexpr int32_t SpireOver = 50;     // PD2's spawn count once 0x1036eb30 ends the waves
inline auto BitterDepths() noexcept -> int32_t { return placements::LevelOf("bitterdepths"); }  // PD2 207 Tomb Event Map, Act V (placements/bitterdepths.cpp)
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
using PopulateFn = void (*)(Game* game, void* room) noexcept;
PopulateFn g_populateOriginal = nullptr;
PopulateFn g_activateOriginal = nullptr;
// 0x48f190 (game, room): the room's preset loop (pd2_engine.h PresetSpawn), the first of four calls (0x48f190, 0x503790, 0x510f00, RoomPopulate) both RoomPopulate callers
// (the per-frame room loop 0x408670 and the activation 0x4042a0) make for a room that is not active yet (0x2f0600 == 0).
// Decompiled for #950 (Ghidra, D2R.gpr); entry bytes push rbp; push rsi; push r14; mov rbp,rsp.
constexpr uint64_t RoomActivate = 0x48f190;
constexpr uint8_t RoomActivateEntry[] = {0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8b, 0xec};

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
auto NotifyKeyText(Game* game, int32_t event, const char* key) noexcept -> void {
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

auto Notify(Game* game, int32_t event) noexcept -> void {
	NotifyKeyText(game, event, NotifyKey(event));
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

// The Cartographer slots (docs/cartographer-contracts.md, merge/ids-levels.tsv): every native map is one of these levels
constexpr int32_t NativeFirst = 138, NativeLast = 144;
// Cartographer publishes no room count (event-link RoomProgress), so a native map's progress divides by the room
// count of its built level (RealRooms); NativeTier.rooms (PD2's default 200) serves until it is read.
// For game checks only: PD2R_MAP_ROOMS=<n> in the game's environment replaces the count.
bool g_roomsOverride = false;
Tiered NativeTier = {0, 200};

// The rooms of the room's level, counted down its drlg room list (D2R 93847; the same
// list LevelRoomExAt walks).
auto RealRooms(void* room) noexcept -> int32_t {
	constexpr int Cap = 32767;
	void* drlgRoom = At<void*>(room, d2r::rva::RoomDrlg);
	void* level = drlgRoom != nullptr ? At<void*>(drlgRoom, d2r::rva::DrlgRoomLevel) : nullptr;
	int32_t n = 0;
	for (void* r = level != nullptr ? At<void*>(level, d2r::rva::LevelFirstRoomEx) : nullptr; r != nullptr && n < Cap;
	     r = At<void*>(r, d2r::rva::DrlgRoomNext)) {
		++n;
	}
	return n;
}

// Only the native maps roll an event and count rooms (PD2's tiered and unique maps are cut, #964)
auto Tier(int32_t level) noexcept -> const Tiered* {
	return level >= NativeFirst && level <= NativeLast ? &NativeTier : nullptr;
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
	room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(room, x, y);
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
// search runs from the room's centre with size 12.
// shortcut: PD2 also skips a room its 0x103a2e90 (an engine call through a pointer, not read) refuses, and marks
// Mendeln's monster (+0xd0 = 11); read both if an event spawns where PD2's would not, or Mendeln acts differently
auto SpawnEvent(Game* game, void* room, int32_t level, int32_t rooms, int32_t progress) noexcept -> void {
	using namespace d2r::rva;
	const int32_t event = Get(game, EventKey);
	const int32_t spawns = Get(game, SpawnsKey);
	const int32_t roll = Rand() % 100;
	constexpr int32_t size = 12;
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
	} else if ((event == DarkWanderer || event == TreasureFallen || event == Invaders) && spawns < 3 &&
	           (event == Invaders ? 25 + spawns * 20 : (spawns + 1) * 25) < progress && roll < 25) {
		id = event == DarkWanderer ? DarkWandererMonster : event == Invaders ? InvaderPortalMonster : TreasureFallenMonster;
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
	const Unit* made = event == Tomb    ? OpenEventPortal(game, room, xy[0], xy[1], BitterDepths())
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
	if (args->classId == SacrificeAltar) {
		return OperateSacrificeAltar(args->game, args->player);
	}
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

std::atomic<int32_t> g_forceNext {0};

// The roll for a tiered map of `quality` (rva::Quality*), `forced` by a Catalyst Shard's marker
auto RollEvent(Game* game, int32_t level, int32_t quality, bool forced) noexcept -> void {
	int32_t event = pd2::event_roll::Pick(quality, forced, Rand, placements::Finished(BitterDepths()));
	// For game checks only, not PD2's: PD2R_MAP_EVENT=<11-21> in the game's environment rolls that event on every map
	static const int32_t testEvent = [] {
		char v[8] {};
		size_t n = 0;
		const int32_t e = getenv_s(&n, v, sizeof(v), "PD2R_MAP_EVENT") == 0 && n > 0 ? std::atoi(v) : 0;
		return e > NoEvent && e <= Tomb && NotifyKey(e) != nullptr ? e : 0;
	}();
	event = testEvent != 0 ? testEvent : event;
	// The console command `core-events event` (#998): one shot, for the next map rolled
	const int32_t forcedEvent = g_forceNext.exchange(0);
	event = forcedEvent != 0 ? forcedEvent : event;
	// The Bitter Depths is not built (#965): the Tomb opens no portal to it, whichever way it was rolled
	event = event == Tomb && !placements::Finished(BitterDepths()) ? NoEvent : event;
	Set(game, EventKey, event);
	Set(game, SpawnsKey, 0);
	++g_rolled[event - NoEvent];
	plog::Info("map events: level %d (quality %d%s) rolled event %d (%u rolls)", level, quality,
	           forcedEvent != 0 ? ", console event" : testEvent != 0 ? ", PD2R_MAP_EVENT" : forced ? ", forced" : "", event, ++g_rolls);
}

// A native map's first room: the game has no event yet (Cartographer allows one map per game), so roll it from the map
// item the player used. Cartographer takes the item inside its open call, before the first room activates, and the
// item interaction listener never fired for a right-click (almanac.cpp), so the use-item handler's entry keeps the
// item's quality and forcing marker (0x4f40c0 (game, player, packet): the packet holds the item's id at +10, the same
// message Cartographer's spell 16 answers). Without a map use (a map opened another way) it rolls as a normal map.
// A native map is any item of the Cartographer map type cmap (merge/ids-itemtypes.tsv, docs/cartographer-contracts.md)
constexpr int32_t MapItemType = 245;
std::atomic<int32_t> g_usedQuality {0};  // 0 none; else the D2R quality of the map item used last
std::atomic<bool> g_usedForced {false};
using UseItemFn = void (*)(Game*, Unit*, uint8_t*, char) noexcept;
UseItemFn g_useOriginal = nullptr;
constexpr uint64_t UseItem = 0x4f40c0;
constexpr uint8_t UseItemEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56};

auto OnUseItem(Game* game, Unit* player, uint8_t* packet, char flag) noexcept -> void {
	using namespace d2r::rva;
	Unit* item = game != nullptr && packet != nullptr
	                 ? d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(game, UnitItem, At<int32_t>(packet, 10))
	                 : nullptr;
	if (item != nullptr && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(item, MapItemType) != 0) {
		g_usedQuality = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item);
		// the isolated forcing marker is stat 553 on layer 1 only (event_link.h); a layer-0 553 is a player buff
		g_usedForced = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(item, StatForceEvent,
		                                                                                     pd2::event_link::MarkerLayer) > 0;
	}
	g_useOriginal(game, player, packet, flag);
}

auto RollNative(Game* game, int32_t level) noexcept -> void {
	const int32_t quality = g_usedQuality.exchange(0);
	const bool forced = g_usedForced.exchange(false);
	plog::Info("map events: native map %s, quality %d%s", quality != 0 ? "item used" : "item not seen", quality,
	           forced ? ", forced" : "");
	RollEvent(game, level, quality != 0 ? quality : d2r::rva::QualityNormal, forced);
}

auto Divisor(Game* game, int32_t level, const Tiered* tier) noexcept -> int32_t {
	const int32_t total = tier == &NativeTier ? Get(game, TotalKey(level)) : 0;
	return total > 0 ? total : tier->rooms;
}

// Before the room's own monsters: a native map's event roll, then PD2's event spawn
auto BeforeRoom(Game* game, void* room, int32_t level, const Tiered* tier) noexcept -> void {
	if (plog::Sample(g_rooms)) {
		plog::Info("map events: room activated in level %d (%u so far)", level, g_rooms.load());
	}
	if (tier == &NativeTier && Get(game, EventKey) == 0) {
		RollNative(game, level);
	}
	if (tier == &NativeTier && !g_roomsOverride && Get(game, TotalKey(level)) == 0) {
		const int32_t total = RealRooms(room);
		Set(game, TotalKey(level), total);
		plog::Info("map events: level %d has %d rooms, progress divides by that", level, total);
	}
	if (tier != nullptr) {
		const int32_t rooms = Get(game, RoomsKey(level));
		SpawnEvent(game, room, level, rooms, rooms * 100 / Divisor(game, level, tier));
	}
}

auto AfterRoom(Game* game, int32_t level, const Tiered* tier) noexcept -> void {
	const int32_t rooms = Get(game, RoomsKey(level)) + 1;
	Set(game, RoomsKey(level), rooms);
	if (rooms % 5 == 0) {  // for the game check
		plog::Info("map events: level %d, %d rooms activated (progress %d), event %d, %d spawned", level, rooms,
		           rooms * 100 / Divisor(game, level, tier), Get(game, EventKey), Get(game, SpawnsKey));
	}
}

auto OnRoomPopulate(Game* game, void* room) noexcept -> void {
	const int32_t level = game != nullptr && room != nullptr
		? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room)
		: -1;
	const Tiered* tier = Tier(level);
	BeforeRoom(game, room, level, tier);
	if (RathmaRoom(game, room, level)) {
		return;  // PD2 0x1036fef0: an arena room spawns none of its own monsters
	}
	g_populateOriginal(game, room);
	if (tier != nullptr) {
		AfterRoom(game, level, tier);
	}
}

// Cartographer's room-populate service (d2r-tools docs/cartographer/room-populate-service.md) runs these for every room
// instead of OnRoomPopulate, which cannot hook RoomPopulate beside Cartographer.
D2RL::PluginCommunication::ServiceLease<carto::RoomPopulateServiceV1> g_populateLease;
uint32_t g_subscription = 0;
std::atomic<bool> g_subscribed {false};
std::atomic<bool> g_subscribeTried {false};

auto __cdecl ServiceBefore(void* game, void* room, int32_t level, void*) noexcept -> uint32_t {
	const Tiered* tier = Tier(level);
	BeforeRoom(static_cast<Game*>(game), room, level, tier);
	return RathmaRoom(static_cast<Game*>(game), room, level) ? carto::RoomPopulateSkipNative : 0;  // PD2 0x1036fef0
}

auto __cdecl ServiceAfter(void* game, void*, int32_t level, bool, void*) noexcept -> void {
	if (const Tiered* tier = Tier(level); tier != nullptr) {
		AfterRoom(static_cast<Game*>(game), level, tier);
	}
}

// Once, at the first room activation (never at load: Cartographer may load after core-events). A miss keeps OnRoomActivate's own events.
auto SubscribeRoomPopulate() noexcept -> void {
	const D2RL::PluginCommunicationService* communication = nullptr;
	static const carto::RoomPopulateCallbacksV1 Callbacks = {sizeof(carto::RoomPopulateCallbacksV1), ServiceBefore, ServiceAfter};
	if (g_context != nullptr && g_context->QueryService(&communication) == D2RL::ServiceQueryResult::Success &&
	    D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize) &&
	    D2RL::PluginCommunication::Acquire(g_context, communication, carto::RoomPopulateServiceProvider,
	                                       carto::RoomPopulateServiceName, carto::RoomPopulateServiceVersion,
	                                       sizeof(carto::RoomPopulateServiceV1), &g_populateLease) ==
	        D2RL::PluginCommunication::Result::Success &&
	    g_populateLease->subscribe("core-maps", &Callbacks, nullptr, &g_subscription)) {
		g_subscribed = true;
		plog::Info("map events: subscribed to %s (handle %u)", carto::RoomPopulateServiceName, g_subscription);
		return;
	}
	plog::Warn("map events: %s not available: map events run from the room activation, the Rathma arena-room skip is off",
	           carto::RoomPopulateServiceName);
}

// The first step of a room's first activation, hooked only while Cartographer holds RoomPopulate
auto OnRoomActivate(Game* game, void* room) noexcept -> void {
	if (!g_subscribeTried.exchange(true)) {
		SubscribeRoomPopulate();
	}
	if (g_subscribed) {
		g_activateOriginal(game, room);
		return;
	}
	const int32_t level = game != nullptr && room != nullptr
		? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room)
		: -1;
	const Tiered* tier = Tier(level);
	const bool first = tier == &NativeTier;
	if (first) {
		BeforeRoom(game, room, level, tier);
	}
	g_activateOriginal(game, room);
	if (first) {
		AfterRoom(game, level, tier);
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

auto MapEventSpawns(Game* game) noexcept -> int32_t {
	return game != nullptr ? Get(game, SpawnsKey) : 0;
}

auto MapEventNotify(Game* game, int32_t event, const char* key) noexcept -> void {
	NotifyKeyText(game, event, key);
}

// PD2 0x10378080, in state 2 of the Altar event: with the guardian in a room, a sound on the guardian to every player
// in game, state 3, and every clone in the guardian's room and its neighbours (1.13c's near-room list) dies without a
// drop (0x10356e00). Picked first, so no kill runs while the room lists are walked.
// shortcut: the sound plays on each player, not on the guardian (see OperateAltar); a clone already dead is skipped,
// where PD2 kills it again
auto AltarGuardianDied(Game* game, Unit* guardian) noexcept -> void {
	using namespace d2r::rva;
	if (MapEvent(game) != Altar || Get(game, SpawnsKey) != 2) {
		plog::Info("map events: a guardian died with event %d in state %d: not the altar's fight", MapEvent(game),
		           Get(game, SpawnsKey));
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
	char envRooms[16] = {};
	size_t n = 0;
	if (getenv_s(&n, envRooms, sizeof(envRooms), "PD2R_MAP_ROOMS") == 0 && n > 0 && std::atoi(envRooms) > 0) {
		g_roomsOverride = true;
		NativeTier.rooms = static_cast<int16_t>(std::clamp(std::atoi(envRooms), 1, 32767));
	}
	g_context = context;
	if (context->QueryService(&g_strings) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLocalizationServiceField(g_strings, D2RL::LocalizationServiceRequiredSize)) {
		g_strings = nullptr;  // the events still spawn, without their chat line
	}
	// Cartographer's hook changes the entry bytes: skip the install then, as its failed attempt is what the loader logs.
	const bool held = d2r::imageBase != 0 && std::memcmp(d2r::Engine<const void*>(RoomPopulate), RoomPopulateEntry,
	                                                     sizeof(RoomPopulateEntry)) != 0;
	const bool rooms = !held && context->InstallInlineHook(RoomPopulate, RoomPopulateEntry, sizeof(RoomPopulateEntry),
	                                                       OnRoomPopulate, &g_populateOriginal);
	bool activate = false;
	if (!rooms) {
		// Cartographer hooks RoomPopulate's entry too (its own events) and loads first; the loader's hook engine cannot
		// hook one address twice. Hook the call both of RoomPopulate's callers make just before it (rva 0x48f190): its first run
		// subscribes to Cartographer's room-populate service, which then gives every room (and the Rathma arena-room skip).
		activate = context->InstallInlineHook(RoomActivate, RoomActivateEntry, sizeof(RoomActivateEntry), OnRoomActivate,
		                                      &g_activateOriginal);
	}
	const bool uses = context->InstallInlineHook(UseItem, UseItemEntry, sizeof(UseItemEntry), OnUseItem, &g_useOriginal);
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool altars = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperateFree35) * 8, Empty,
	                                           sizeof(Empty), reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperateAltar)));
	std::snprintf(report, reportSize, "room activation%s, map use %s, altar operate %d%s%s",
	              rooms      ? ""
	              : activate ? " (through 0x48f190 until Cartographer's room-populate service is found)"
	                         : " FAILED (bytes differ)",
	              uses ? "on" : "FAILED (native maps roll as normal)", ObjectOperateFree35, altars ? "" : " FAILED (slot not empty)",
	              g_strings != nullptr ? "" : ", no notify (no localization service)");
	return rooms || activate ? 1 : 0;
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

auto UnsubscribeRoomPopulate() noexcept -> void {
	if (g_subscribed.exchange(false) && g_populateLease) {
		g_populateLease->unsubscribe(g_subscription);
	}
}

auto ForceNextMapEvent(const char* name) noexcept -> bool {
	static constexpr struct { const char* word; int32_t event; } Names[] = {
		{"off", 0}, {"wanderer", DarkWanderer}, {"mendeln", Mendeln}, {"treasure", TreasureFallen}, {"horazon", Horazon}, {"gheed", Gheed}, {"altar", Altar}, {"spire", Spire}, {"tomb", Tomb}, {"invaders", Invaders},
	};
	for (const auto& n : Names) {
		if (std::strcmp(name, n.word) == 0) {
			g_forceNext = n.event;
			return true;
		}
	}
	return false;
}
