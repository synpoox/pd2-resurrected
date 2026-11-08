// Zhar's Sanctum's seals (MS7c, #531; docs/research/pd2-map-system-dll.md, section 6). The sanctum is four levels: the
// library hub (PD2 176) and three wings (177-179). PD2's preset rewrite 0x1038bfc0 places their portals and tomes:
// - in the hub, placeholders A-C each get an open blue portal (`zharportal_blue`), linked both ways to a second open
//   blue portal at spot 11 of wing 177, 178 or 179 (0x1038a1f0, 0x1038a510);
// - in each wing, placeholder A gets a closed portal back to the hub and placeholder B a tome (`ZharTomeObject`). The
//   game seed (game +0x7c, mod 3) picks the wing whose portal is red (`zharportal_red`); it leads to the hub's spot 0.
//   The two blue ones lead to `empty` markers by the hub's spot 11: (-0x19, +0xf), (-0x19, -0x19), (+0xf, -0x19).
// PD2's object operate 0x103aa670 (operate 80, merged into D2R's free slot 64) runs them all, in map event 0x1e only:
// an open portal warps through PD2's portal operate (0x103a9780); a tome opens the portal in its rooms (blue first)
// and steps the sanctum's progress (game +0x262a, the port's counter 'MZHR'): a blue seal 0 -> 1 -> 2, the red one to 3.
// Each step and each tome plays one of Zhar's taunts to the players in the level (sounds 4704-4707). Anything else is
// refused with the player's "can't" sound (0x13).
//
// No PluginSDK service places preset objects, operates objects or moves a player between levels, so these are the
// preset spawn hook (map_stats.cpp), one operate table slot and engine calls, as the uber portals' (map_open.cpp).
#include "zhar.h"

#include "log.h"
#include "map_events.h"
#include "map_open.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t ZharEvent = 0x1e;
// Port level ids: the hub and its wings (PD2 176-179), in act V
constexpr int32_t Hub = 172, FirstWing = 173, Wings = 3, Act = 4;
// Port objects rows (PD2's + 3): zharportal_blue, zharportal_red, ZharTomeObject (PD2 598-600), preset_placeholder_A-B
// (PD2 601-602)
constexpr int32_t BluePortal = 601, RedPortal = 602, Tome = 603;
constexpr int32_t PlaceholderA = 604, PlaceholderB = 605;
constexpr int32_t UnitObject = 2;
constexpr int32_t ObjectOperateZhar = 64;  // D2R's free operate slot that PD2's operate 80 takes (tools/pd2_ids.py)
constexpr int32_t ModeClosed = 0, ModeOpening = 1, ModeOpen = 2;
constexpr int32_t Cant = 0x13;  // the player's "can't" sound, as PD2 0x1036d1b0 attaches it
// The spots PD2 0x1038a1f0 finds through 0x1038a000: D2Common's act spawn location, a special tile of the level's
// presets (levels Position 1), and the unit size it passes
constexpr int32_t WingSpot = 0xb, RedSpot = 0;
constexpr int32_t PortalSize = 3;
// The blue exits' offsets from the hub's spot 11, by wing
constexpr int32_t ExitOffsets[Wings][2] = {{-0x19, 0xf}, {-0x19, -0x19}, {0xf, -0x19}};
// PD2's sounds (sounds.txt Index) for the seal steps 1-3 and every tome: zhar_intro1-3, zhar_book. D2R's sound table
// lacks them (docs/deviations.md, PD2 tables soundenviron and sounds), so they are logged
constexpr const char* Taunts[] = {"zhar_intro1 (4704)", "zhar_intro2 (4705)", "zhar_intro3 (4706)"};
constexpr const char* BookSound = "zhar_book (4707)";

// PD2's game +0x262a (unique_maps.cpp reads it on the bosses' deaths) and the red wing (1-3; 0 before it is picked)
constexpr uint32_t ProgressKey = 0x4d5a4852;  // 'MZHR'
constexpr uint32_t RedWingKey = 0x4d5a5257;   // 'MZRW'
// One link per portal: the hub's three, the wings' entries, the wings' exits. Fields: unit id + 1, destination
// level, x, y (0, 0 for an exit: its spot is found on use)
constexpr uint32_t LinkKey = 0x5a4c0000;  // 'ZL' + link << 4 + field
constexpr int Links = 3 * Wings;
constexpr int HubLink = 0, EntryLink = Wings, ExitLink = 2 * Wings;

std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_tomes {0};
std::atomic<uint32_t> g_seals {0};
std::atomic<uint32_t> g_trips {0};
std::atomic<uint32_t> g_refused {0};

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto Field(Game* game, int link, int field) noexcept -> int32_t* {
	return Counter(game, LinkKey + (static_cast<uint32_t>(link) << 4) + static_cast<uint32_t>(field));
}

auto SetLink(Game* game, int link, Unit* portal, int32_t level, int32_t x, int32_t y) noexcept -> void {
	const int32_t values[] = {static_cast<int32_t>(At<uint32_t>(portal, d2r::rva::UnitIdField)) + 1, level, x, y};
	for (int f = 0; f < 4; ++f) {
		if (int32_t* c = Field(game, link, f); c != nullptr) {
			*c = values[f];
		}
	}
}

auto LinkOf(Game* game, Unit* portal) noexcept -> int {
	const auto id = static_cast<int32_t>(At<uint32_t>(portal, d2r::rva::UnitIdField)) + 1;
	for (int link = 0; link < Links; ++link) {
		const int32_t* c = Field(game, link, 0);
		if (c != nullptr && *c == id) {
			return link;
		}
	}
	return -1;
}

// PD2 0x1038a060 for an object, then its mode: spawned at (x, y) in the room by `near` holding it. The room keeps its
// units once no player is near, as the rooms PD2's link 0x1038a510 marks (0x1030a9c0); a portal gets its level.
auto Spawn(Game* game, void* near, int32_t row, int32_t x, int32_t y, int32_t mode, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	using SpawnFn = Unit* (*)(int32_t, int32_t, int32_t, int32_t, Game*, void*, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	void* room = near != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(near, x, y) : nullptr;
	Unit* unit = room != nullptr ? d2r::Engine<SpawnFn>(SpawnUnit)(UnitObject, row, x, y, game, room, 1, 1, 0, 0)
	                             : nullptr;
	if (unit == nullptr) {
		plog::Warn("zhar: no object %d at (%d, %d)", row, x, y);
		return nullptr;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(unit, mode);
	if (level > 0) {
		d2r::Engine<void (*)(void*, int32_t) noexcept>(RoomPortalMark)(room, 0);
		d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(unit, level);
	}
	return unit;
}

// PD2 0x1038a000: a level's spawn spot, the level built if needed. Null when the act or the spot is missing.
auto SpawnSpot(Game* game, int32_t level, int32_t spot, int32_t* x, int32_t* y) noexcept -> void* {
	using namespace d2r::rva;
	if (At<uint64_t>(game, GameActCount) <= Act || At<void**>(game, GameActs)[Act] == nullptr) {
		return nullptr;
	}
	return d2r::Engine<void* (*)(uint32_t, void*, int32_t, int32_t, int32_t*, int32_t*, int32_t) noexcept>(ActSpawnSpot)(
		d2r::DataFlag(game), At<void**>(game, GameActs)[Act], level, spot, x, y, PortalSize);
}

auto RedWing(Game* game) noexcept -> int {
	int32_t* red = Counter(game, RedWingKey);
	if (red == nullptr) {
		return 0;
	}
	if (*red == 0) {
		// shortcut: PD2 takes the game seed (game +0x7c) mod 3; here rand() picks it once per game
		*red = 1 + std::rand() % Wings;
		plog::Info("zhar: wing %d holds the red seal", FirstWing + *red - 1);
	}
	return *red - 1;
}

auto Coords(Unit* unit) noexcept -> std::pair<int32_t, int32_t> {
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::rva::UnitCoords)(unit, xy);
	return {xy[0], xy[1]};
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : -1;
}

// PD2 0x1038d3a0: the first object of `row` in the player's room or the rooms next to it, whatever its mode
auto FindNear(Unit* player, int32_t row) noexcept -> Unit* {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	void** rooms = nullptr;
	uint32_t count = 0;
	if (room != nullptr) {
		d2r::Engine<void (*)(void*, void***, uint32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	}
	for (uint32_t r = 0; rooms != nullptr && r < count; ++r) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[r]); unit != nullptr;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			if (At<int32_t>(unit, UnitType) == UnitObject && At<int32_t>(unit, UnitClassId) == row) {
				return unit;
			}
		}
	}
	return nullptr;
}

// The player goes to `level` and on to (x, y) there, as PD2's linked portals put a player by the partner portal or
// marker; (0, 0) leaves the player at the level's arrival. `hint` is a room by (x, y), for a spot far from the arrival
auto Warp(Game* game, Unit* player, int32_t level, int32_t x, int32_t y, void* hint = nullptr) noexcept -> bool {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(WarpToLevel)(game, player, level, 0);
	if (LevelOf(player) != level) {
		plog::Warn("zhar: the warp to level %d left the player in %d", level, LevelOf(player));
		return false;
	}
	void* near = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	void* room = x != 0 && near != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(near, x, y)
	                                       : nullptr;
	if (room == nullptr && hint != nullptr) {
		room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(hint, x, y);
	}
	if (room == nullptr) {
		int32_t sx = 0, sy = 0;
		room = x != 0 ? SpawnSpot(game, level, RedSpot, &sx, &sy) : nullptr;  // builds the level's rooms
		room = room != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(room, x, y) : nullptr;
	}
	if (room != nullptr) {
		d2r::Engine<void (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, int32_t) noexcept>(TeleportUnit)(
			game, player, room, x, y, 0, 0);
	} else if (x != 0) {
		plog::Warn("zhar: no room at (%d, %d) in level %d, left at its arrival", x, y, level);
	}
	const auto [px, py] = Coords(player);
	plog::Info("zhar: player to level %d at (%d, %d), aimed (%d, %d) (%u trips)", level, px, py, x, y, ++g_trips);
	return true;
}

// A hub portal's trip. PD2 makes the wing's partner portal when the hub's placeholder spawns; here it is made on the
// first trip, so no other level is built inside the preset spawn
auto HubTrip(Game* game, Unit* player, Unit* portal, int link) noexcept -> bool {
	const int32_t wing = FirstWing + (link - HubLink);
	const int32_t* dx = Field(game, link, 2);
	const int32_t* dy = Field(game, link, 3);
	if (dx != nullptr && dy != nullptr && *dx == 0) {
		int32_t x = 0, y = 0;
		void* room = SpawnSpot(game, wing, WingSpot, &x, &y);
		Unit* entry = room != nullptr ? Spawn(game, room, BluePortal, x, y, ModeOpen, Hub) : nullptr;
		if (entry == nullptr) {
			plog::Warn("zhar: no partner portal at spot %d of level %d", WingSpot, wing);
			return false;
		}
		const auto [hx, hy] = Coords(portal);
		SetLink(game, EntryLink + (link - HubLink), entry, Hub, hx, hy);
		SetLink(game, link, portal, wing, x, y);
		plog::Info("zhar: level %d's portal at (%d, %d), linked to the hub's at (%d, %d) (%u portals)", wing, x, y, hx,
		           hy, ++g_portals);
	}
	return Warp(game, player, wing, *Field(game, link, 2), *Field(game, link, 3));
}

// An exit's trip: the red one to the hub's spot 0, a blue one to its marker by the hub's spot 11
auto ExitTrip(Game* game, Unit* player, int link) noexcept -> bool {
	const int wing = link - ExitLink;
	const bool red = wing == RedWing(game);
	int32_t x = 0, y = 0;
	void* spot = SpawnSpot(game, Hub, red ? RedSpot : WingSpot, &x, &y);
	if (spot == nullptr) {
		plog::Warn("zhar: no spot %d in the hub", red ? RedSpot : WingSpot);
		return false;
	}
	if (!red) {
		x += ExitOffsets[wing][0];
		y += ExitOffsets[wing][1];
	}
	return Warp(game, player, Hub, x, y, spot);
}

auto Refuse(Unit* player) noexcept -> int32_t {
	// PD2 attaches the sound with no target unit; D2R's own refusals (map_open.cpp) pass the player
	d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(d2r::rva::ClientMessage)(player, Cant, player);
	++g_refused;
	return 0;
}

// PD2 0x103aa670, a tome's part: the portal in the tome's rooms opens, a blue one first
auto ReadTome(Game* game, Unit* player, Unit* tome) noexcept -> int32_t {
	using namespace d2r::rva;
	if (At<int32_t>(tome, UnitMode) == ModeOpen) {
		return 0;
	}
	Unit* portal = FindNear(player, BluePortal);
	portal = portal != nullptr ? portal : FindNear(player, RedPortal);
	if (portal == nullptr) {
		plog::Warn("zhar: tome read with no seal portal near");
		return Refuse(player);
	}
	int32_t* progress = Counter(game, ProgressKey);
	const int32_t before = progress != nullptr ? *progress : -1;
	const bool blue = At<int32_t>(portal, UnitClassId) == BluePortal;
	if (progress != nullptr && ((blue && *progress < 2) || (!blue && *progress < 3))) {
		*progress = blue ? *progress + 1 : 3;
		++g_seals;
		// shortcut: PD2 plays the taunt to every player in the level (packet 0x2c, 0x1037f240); D2R lacks the sound
		plog::Info("zhar: seal step %d -> %d (%s seal), sound %s", before, *progress, blue ? "blue" : "red",
		           Taunts[*progress - 1]);
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(tome, ModeOpen);
	// PD2: the portal opens over its mode 1 frames (objects FrameCnt1), then event 1 sets mode 2
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(portal, ModeOpening);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(
		d2r::DataFlag(game), At<int32_t>(portal, UnitClassId));
	if (record != nullptr) {
		const int32_t open = At<int32_t>(game, GameFrame) + 1 + static_cast<int32_t>(At<uint32_t>(record, ObjOpenFrames) >> 8);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
			game, portal, UnitEventOpen, open, 0, 0, 0);
	}
	const auto [x, y] = Coords(portal);
	plog::Info("zhar: tome read, %s portal at (%d, %d) opens, progress %d, sound %s (%u tomes)", blue ? "blue" : "red",
	           x, y, progress != nullptr ? *progress : -1, BookSound, ++g_tomes);
	return 1;
}

// The argument block D2R's operate dispatcher 0x592c40 passes an operate function (map_open.cpp's)
struct ObjectOperate {
	Game* game;
	Unit* object;
	Unit* player;
	uint64_t seed;
	int32_t classId;
};

// PD2 0x103aa670
auto OperateZhar(ObjectOperate* args) noexcept -> int32_t {
	using namespace d2r::rva;
	Game* game = args->game;
	Unit* player = args->player;
	Unit* object = args->object;
	if (game == nullptr || player == nullptr || object == nullptr) {
		return 0;
	}
	if (MapEvent(game) != ZharEvent) {
		return Refuse(player);
	}
	const int32_t row = At<int32_t>(object, UnitClassId);
	if (row == Tome) {
		return ReadTome(game, player, object);
	}
	if ((row != BluePortal && row != RedPortal) || At<int32_t>(object, UnitMode) != ModeOpen) {
		return Refuse(player);
	}
	const int link = LinkOf(game, object);
	bool went = false;
	if (link >= HubLink && link < EntryLink) {
		went = HubTrip(game, player, object, link);
	} else if (link >= EntryLink && link < ExitLink) {
		went = Warp(game, player, *Field(game, link, 1), *Field(game, link, 2), *Field(game, link, 3));
	} else if (link >= ExitLink) {
		went = ExitTrip(game, player, link);
	} else {
		plog::Warn("zhar: portal %d at level %d has no link", row, LevelOf(object));
	}
	return went ? 1 : Refuse(player);
}

}  // namespace

auto ZharPreset(Game* game, void* room, const uint8_t* preset) noexcept -> void {
	using namespace d2r::rva;
	if (At<int32_t>(preset, PresetType) != UnitObject) {
		return;
	}
	const int32_t level = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
	const int32_t id = At<int32_t>(preset, PresetId);
	if (level < Hub || level >= FirstWing + Wings || id < PlaceholderA || id > PlaceholderA + 2) {
		return;
	}
	const int32_t x = At<int32_t>(room, RoomX) + At<int32_t>(preset, PresetX);
	const int32_t y = At<int32_t>(room, RoomY) + At<int32_t>(preset, PresetY);
	if (level == Hub) {
		const int link = HubLink + (id - PlaceholderA);
		if (Unit* portal = Spawn(game, room, BluePortal, x, y, ModeOpen, FirstWing + (id - PlaceholderA)); portal) {
			SetLink(game, link, portal, FirstWing + (id - PlaceholderA), 0, 0);
			plog::Info("zhar: hub portal %d at (%d, %d) to level %d (%u portals)", link, x, y,
			           FirstWing + (id - PlaceholderA), ++g_portals);
		}
		return;
	}
	const int wing = level - FirstWing;
	if (id == PlaceholderA) {
		const bool red = wing == RedWing(game);
		if (Unit* portal = Spawn(game, room, red ? RedPortal : BluePortal, x, y, ModeClosed, Hub); portal) {
			SetLink(game, ExitLink + wing, portal, Hub, 0, 0);
			plog::Info("zhar: level %d's closed %s seal portal at (%d, %d) (%u portals)", level, red ? "red" : "blue", x,
			           y, ++g_portals);
		}
	} else if (id == PlaceholderB && Spawn(game, room, Tome, x, y, ModeClosed, 0) != nullptr) {
		plog::Info("zhar: level %d's tome at (%d, %d)", level, x, y);
	}
}

auto InstallZhar(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool slot = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperateZhar) * 8, Empty,
	                                         sizeof(Empty), reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperateZhar)));
	std::snprintf(report, reportSize, "seal operate %d%s", ObjectOperateZhar, slot ? "" : " FAILED (slot not empty)");
	return slot ? 1 : 0;
}

auto ZharCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "zhar: %u seal portals, %u tomes read, %u seal steps, %u trips, %u refused", g_portals.load(),
	              g_tomes.load(), g_seals.load(), g_trips.load(), g_refused.load());
}
