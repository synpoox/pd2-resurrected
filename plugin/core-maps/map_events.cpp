// PD2's map events (ProjectDiablo.dll; docs/research/pd2-map-system-dll.md, section 4). Opening a tiered map rolls
// one event for the game (0x10383060): rare and better 34%, magic 17%, normal 11%, always with `map_force_event`
// (a Catalyst Shard); then up to 40 draws of 12, the first naming an event wins (Altar of the Catalyst never when
// forced). The event spawns as the map's rooms activate (0x1036fef0), once the level's progress, rooms activated x
// 100 / PD2's room count for the level, is far enough; it spawns at the activating room's centre.
//
// This slice spawns the events made of one monster: Shadow of Mendeln once, Dark Wanderer and Treasure Fallen up to
// three times. The others are rolled and logged but spawn nothing yet: Gheed, Altar of the Catalyst and Spire of
// Darkness (MS8), Horazon and Void/Tomb (MS9), the invaders (MS10).
//
// Each spawn sends the event's notify string to every player as a red chat line (#336; PD2 0x1037dbc0, packet 0x26
// colour 1). The SDK's localization service gives the text; no SDK service sends a chat line, so it goes through
// D2R's server chat broadcast (rva::GameChat).
//
// No PluginSDK service reports a room activating, so this hooks D2R's room monster spawn (rva::RoomPopulate), the
// function PD2 wraps. The event and the counts live in D2R's per-game counters (rva::GameCounter), so a new game
// starts without them.
#include "map_events.h"

#include "log.h"

#include <D2RLPlugin/localization.h>

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
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
constexpr int32_t ModeNeutral = 1;

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
	char text[256] {};  // GameChat copies 256 bytes
	uint32_t need = 0;
	if (key == nullptr || g_strings == nullptr ||
	    g_strings->getStringByKey(g_context, key, text, sizeof(text), &need) != D2RL::Localization::Result::Success) {
		plog::Warn("map events: no notify text for event %d (%s)", event, key != nullptr ? key : "no key");
		return;
	}
	constexpr uint8_t Red = 1;
	d2r::Engine<void (*)(Game*, const char*, uint8_t) noexcept>(d2r::rva::GameChat)(game, text, Red);
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

// PD2's 0x1038a170 at the room's centre: a free spot near it, then the monster.
// shortcut: D2R's portal spot search stands in for PD2's 0x10389c20 (radius 12, 8 in Arcane and River of Blood,
// collision 0x805); read 0x10389c20 if an event ever spawns in a wall
auto SpawnAt(Game* game, void* room, int32_t monster) noexcept -> Unit* {
	using namespace d2r::rva;
	int32_t xy[2] = {At<int32_t>(room, RoomX) + At<int32_t>(room, RoomWidth) / 2,
	                 At<int32_t>(room, RoomY) + At<int32_t>(room, RoomHeight) / 2};
	void* found = nullptr;
	d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(FindFreeSpot)(
		game, room, xy, 3, 0x400, &found, 5, 100);
	if (found == nullptr) {
		return nullptr;
	}
	return d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster)(game, found, xy[0], xy[1], monster, ModeNeutral, -1, 0x42);
}

// PD2's 0x1036fef0, before the room's own monsters: one roll of 100 per room, against the event's rule.
// shortcut: PD2 also skips a room its 0x103a2e90 (an engine call through a pointer, not read) refuses, and marks
// Mendeln's monster (+0xd0 = 11); read both if an event spawns where PD2's would not, or Mendeln acts differently
auto SpawnEvent(Game* game, void* room, int32_t level, int32_t progress) noexcept -> void {
	const int32_t event = Get(game, EventKey);
	const int32_t spawns = Get(game, SpawnsKey);
	const int32_t roll = Rand() % 100;
	int32_t monster = -1;
	if (event == Mendeln && spawns == 0 && roll + progress > 140) {
		monster = MendelnMonster;
	} else if ((event == DarkWanderer || event == TreasureFallen) && spawns < 3 && (spawns + 1) * 25 < progress &&
	           roll < 25) {
		monster = event == DarkWanderer ? DarkWandererMonster : TreasureFallenMonster;
	}
	if (monster < 0) {
		return;
	}
	if (SpawnAt(game, room, monster) == nullptr) {
		plog::Warn("map events: event %d found no spot for monster %d in level %d (%u)", event, monster, level,
		           ++g_noSpot);
		return;
	}
	Set(game, SpawnsKey, spawns + 1);
	plog::Info("map events: event %d spawn %d, monster %d in level %d at progress %d (%u spawned)", event, spawns + 1,
	           monster, level, progress, ++g_spawned);
	Notify(game, event);
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
		SpawnEvent(game, room, level, Get(game, RoomsKey(level)) * 100 / tier->rooms);
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

auto MapEvent(Game* game) noexcept -> int32_t {
	return game != nullptr ? Get(game, EventKey) : 0;
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
	Set(game, EventKey, event);
	Set(game, SpawnsKey, 0);
	++g_rolled[event - NoEvent];
	plog::Info("map events: level %d (quality %d%s) rolled event %d (%u rolls)", level, quality,
	           forced ? ", forced" : "", event, ++g_rolls);
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
	std::snprintf(report, reportSize, "room activation%s%s", rooms ? "" : " FAILED (bytes differ)",
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
