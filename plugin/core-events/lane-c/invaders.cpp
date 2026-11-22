// The Invaders map event (#224, MS10): PD2's invader portal think (ProjectDiablo.dll 0x1035dca0, the Overseer AI's portal
// branch) and the invaders' death drops (0x10378250). Decoded in the issue comment on #224.
//
// The portal (monstats 1199, monai row 120 `Overseer`): while the game's event is 20 it waits for a player within aidist
// and its cooldown (stat 500, aip1 ticks), then spawns one invader at its own spot and sends the players the invader's
// class notify. It closes (KillQuietly) after 1, then 3 in all, then 5 in all invaders over its three portals. Beyond 4
// invaders, 2 closed portals or 3 spawns it closes at once. The seven invaders (1200-1206) run D2R's table AIs
// (BloodRaven, Duriel, Summoner...): no think of theirs is registered.
//
// The gear (#1708): when the portal spawns an invader, its set of treasure classes (`Invader*` rows of TreasureClassEx,
// PD2's list and order, 0x10378250) is rolled at once by D2R's TC drop at the invader's level for the player the portal
// saw, and the items are held off the ground (invader id -> item ids). On its death exactly those items drop where D2R
// drops loot, then the class's ear. PD2 rolls the same TCs at death for the killer (docs/deviations.md). An invader with
// nothing held (spawned another way, or the gear entries not found) rolls at death as before.
// Why native: the SDK's ItemService makes items only for a player (createItem, transactions), rejects ground moves and
// has no TC roll. So the TC drop runs as usual and each pick is at once taken off the floor with the steps of D2R's own
// pickup (ItemMode 0x471950), and the death puts them down with the two steps of its drop-from-cursor handler (PlrMsg
// 0x4bd8f0): a free spot, then the floor. (ItemSpawn itself is hooked by another plugin, so it cannot be wrapped.)
//
// The portal's think is registered through the toolkit's AI registry (no SDK service runs monster AI); the death is the
// SDK's monster-death listener, which hands over ids only, so the game, unit and TC are engine calls as horazon.cpp's.
#include "invaders.h"

#include "ai_common.h"
#include "ai_hooks.h"
#include "corruption_roll.h"
#include "invader_look.h"
#include "log.h"
#include "map_events.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

using namespace ai_common;
using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t OverseerRow = 120;   // monai.txt `Overseer`: shared with Lucion's classes (lucion_ai.cpp)
constexpr int32_t PortalClass = 1199;  // monstats InvaderPortal
constexpr int32_t FirstInvader = 1200, InvaderCount = 7;  // Amazon, Assassin, Barbarian, Druid, Necromancer, Paladin, Sorceress
constexpr int32_t InvadersEvent = 20;
constexpr int32_t StatLevel = 12;
constexpr int32_t StatCooldown2 = 500;  // mon_cooldown2 (PD2 447): counts up, never reset
constexpr int32_t ModeNeutral = 1;
constexpr int32_t MaxInvaders = 4, MaxFinished = 2, MaxSpawns = 3;  // the portal closes at once beyond these
constexpr int32_t NoTargetIdle = 5;
constexpr uint32_t InvadersKey = 0x494e5643;  // 'INVC': invaders spawned this game
constexpr uint32_t FinishedKey = 0x494e5646;  // 'INVF': portals closed this game

const char* const ClassNames[InvaderCount] = {"Amazon", "Assassin", "Barbarian", "Druid", "Necromancer", "Paladin", "Sorceress"};
const char* const NotifyKeys[InvaderCount] = {
	"d2r:MapEventNotifyInvadersAmazon", "d2r:MapEventNotifyInvadersAssassin", "d2r:MapEventNotifyInvadersBarbarian",
	"d2r:MapEventNotifyInvadersDruid", "d2r:MapEventNotifyInvadersNecromancer", "d2r:MapEventNotifyInvadersPaladin",
	"d2r:MapEventNotifyInvadersSorceress"};
// The ears: ivez Amazon, ivea Assassin, iveb Barbarian, ived Druid, iven Necromancer, ivep Paladin, ives Sorceress
constexpr uint32_t Ears[InvaderCount] = {
	D2RL::Items::MakeItemCode("ivez"), D2RL::Items::MakeItemCode("ivea"), D2RL::Items::MakeItemCode("iveb"),
	D2RL::Items::MakeItemCode("ived"), D2RL::Items::MakeItemCode("iven"), D2RL::Items::MakeItemCode("ivep"),
	D2RL::Items::MakeItemCode("ives")};

std::atomic<uint32_t> g_spawned {0};
std::atomic<uint32_t> g_closed {0};
std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_drops {0};
std::atomic<uint32_t> g_noTc {0};
std::atomic<uint32_t> g_held {0};      // items rolled at spawn and held
std::atomic<uint32_t> g_released {0};  // held items put on the floor at death
std::atomic<uint32_t> g_lost {0};      // held items with no unit or no floor spot at death

// D2R build 93847, found by entry bytes, never an RVA (the SDK places no existing item on the ground):
// FUN_14043de10 (game, room, xy*, out xy*, 1) -> room or null: the free floor spot by a unit, as the cursor drop asks
sigscan::Entry DropSpot {"ItemDropSpot", "invaders", sigscan::Kind::Code, 0, 0,
                         "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 48 83 ec 40 83 b9 24 01 00 00 00"};
// FUN_14043dd00 (game, dropper or null, item, room, x, y): puts a stored item on that floor spot and tells the clients
sigscan::Entry DropOnFloor {"ItemDropOnFloor", "invaders", sigscan::Kind::Code, 0, 0,
                            "4d 85 c9 0f 84 ?? ?? ?? ?? 48 89 6c 24 10 48 89 74 24 18 4c 89 74 24 20 41 57 48 83 ec 20 49 8b e9"};
// The pickup's steps that take an item off the floor (ItemMode FUN_140471950, before it goes to the inventory):
// FUN_140535d50 (game, item): tells every client that sees the item's room that it is gone
sigscan::Entry FloorGoneClients {"ItemFloorGoneClients", "invaders", sigscan::Kind::Code, 0, 0,
                                 "41 56 41 57 48 83 ec 38 4c 8b f2 e8 ?? ?? ?? ?? 4c 8b f8 48 85 c0 0f 84"};
// FUN_140348b00 (item): takes the item off its floor path
sigscan::Entry FloorPathOff {"ItemFloorPathOff", "invaders", sigscan::Kind::Code, 0, 0,
                             "48 89 5c 24 10 48 89 6c 24 18 56 57 41 54 41 56 41 57 48 83 ec 30 48 8b d9 e8 ?? ?? ?? ?? 44 8b e0 48 85 db 75 13"};
// FUN_14038efe0 (item): takes the item out of its room's unit list
sigscan::Entry FloorRoomOff {"ItemFloorRoomOff", "invaders", sigscan::Kind::Code, 0, 0,
                             "48 89 5c 24 10 48 89 74 24 18 57 48 83 ec 20 33 f6 48 8b d9 48 85 c9 75 1e"};
sigscan::Entry* const GearEntries[] = {&DropSpot, &DropOnFloor, &FloorGoneClients, &FloorPathOff, &FloorRoomOff};
// unit flags (+0x124) the pickup clears and the floor drop sets (FUN_14034ee20 and FUN_14034ed80)
constexpr uint32_t FloorUnitFlags = 0x2 | 0x2000000;
constexpr int32_t ItemModeStored = 0;  // item modes: 0 stored, 3 on the floor
bool g_gearOn = false;

// Takes a just-dropped item off the floor, as the pickup does: it stays a unit of the game in no room, which the game
// frees with itself unless the death puts it back down
void TakeOffFloor(Game* game, Unit* item) noexcept {
	d2r::Engine<void (*)(Game*, Unit*) noexcept>(FloorGoneClients)(game, item);
	d2r::Engine<void (*)(Unit*) noexcept>(FloorPathOff)(item);
	d2r::Engine<void (*)(Unit*) noexcept>(FloorRoomOff)(item);
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(d2r::sig::SetUnitFlags)(item, FloorUnitFlags, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(d2r::sig::SetObjectMode)(item, ItemModeStored);  // the floor drop sets 3 again
}

// (game id << 32 | invader id) -> the item ids it holds. GameLeft clears it; the game frees unclaimed items itself.
std::mutex g_heldLock;
std::unordered_map<uint64_t, std::vector<uint32_t>> g_heldItems;

auto HeldKey(Game* game, uint32_t unitId) noexcept -> uint64_t {
	return (static_cast<uint64_t>(At<uint16_t>(game, d2r::rva::GameIdField)) << 32) | unitId;
}

void ForgetHeld(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	const std::lock_guard lock(g_heldLock);
	g_heldItems.clear();
}

void RollGear(Game* game, Unit* invader, Unit* player, int32_t which) noexcept;

auto GameOf(uint32_t gameId) noexcept -> Game* {
	using namespace d2r::rva;
	const auto id = static_cast<uint16_t>(gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
	return game != nullptr && At<uint16_t>(game, GameIdField) == id ? game : nullptr;
}

// --- The portal's think ---------------------------------------------------------------------------------------------

void ThinkPortal(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	const uint8_t* rec = tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr;
	if (rec == nullptr) {
		ai_registry::Vanilla(OverseerRow)(game, unit, tick);
		return;
	}
	const int32_t diff = At<uint8_t>(tick, AiTickDifficulty) % 3;
	const int32_t aidel = At<uint8_t>(rec, MonAiDel + diff);
	const int32_t aidist = At<uint8_t>(rec, MonAiDist + diff);
	const int32_t aip1 = At<int16_t>(rec, MonAiParams);  // PD2 reads the Normal column (the Hell one is blank in the table)
	if (MapEvent(game) != InvadersEvent) {
		Idle(game, unit, aidel);
		return;
	}
	const int32_t spawns = MapEventSpawns(game);
	if (Counter(game, InvadersKey) > MaxInvaders || Counter(game, FinishedKey) > MaxFinished || spawns > MaxSpawns) {
		plog::Info("invaders: portal %u closes at once (%d invaders, %d closed, %d spawns)", At<uint32_t>(unit, UnitIdField),
		           Counter(game, InvadersKey), Counter(game, FinishedKey), spawns);
		KillQuietly(game, unit);
		return;
	}
	const Unit* target = At<Unit*>(tick, AiTickTarget);
	if (target == nullptr || At<int32_t>(tick, AiTickDistance) > aidist) {
		Idle(game, unit, NoTargetIdle);
		return;
	}
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t cooldown = stat(unit, StatCooldown2, 0);
	if (cooldown < aip1) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(unit, StatCooldown2, cooldown + 1, 0);
		Idle(game, unit, aidel);
		return;
	}
	const int32_t which = RollRand() % InvaderCount;
	int32_t x = 0, y = 0;
	d2r::UnitXY(unit, &x, &y);
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	// D2R's spawn refuses a blocked subtile (the portal's own), so it tries the portal's spot, then a ring around it.
	constexpr int32_t Ring[][2] = {{0, 0}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}, {3, 3}, {-3, -3}, {3, -3}, {-3, 3}};
	Unit* invader = nullptr;
	int32_t usedX = x, usedY = y;
	for (const auto& step : Ring) {
		if (room == nullptr || invader != nullptr) {
			break;
		}
		invader = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
			SpawnMonster)(game, room, x + step[0], y + step[1], FirstInvader + which, ModeNeutral, -1, 0x42);
		usedX = x + step[0], usedY = y + step[1];
	}
	if (invader == nullptr) {
		plog::Warn("invaders: no %s at the portal (%d, %d)", ClassNames[which], x, y);
		Idle(game, unit, aidel);
		return;
	}
	++g_spawned;
	if (g_gearOn) {
		RollGear(game, invader, const_cast<Unit*>(target), which);  // the player the portal saw stands in for PD2's killer
	}
	const int32_t n = ++Counter(game, InvadersKey);
	MapEventNotify(game, InvadersEvent, NotifyKeys[which]);
	const int32_t finished = Counter(game, FinishedKey);
	plog::Info("invaders: %s (class %d) at (%d, %d), invader %d, portals closed %d, %u spawned", ClassNames[which],
	           FirstInvader + which, usedX, usedY, n, finished, g_spawned.load());
	const bool close = finished == 0 ? n != 0 : finished == 1 ? n > 2 : n >= 5;
	if (close) {
		++Counter(game, FinishedKey);
		++g_closed;
		plog::Info("invaders: portal %u closes after invader %d (%d portals closed)", At<uint32_t>(unit, UnitIdField), n,
		           Counter(game, FinishedKey));
		KillQuietly(game, unit);
		return;
	}
	Idle(game, unit, aidel);
}

// --- The invaders' drops --------------------------------------------------------------------------------------------

// One treasure-class drop at the monster's level; false when the table has no such TC. With `items` (room for 6) the
// picks are also written there and counted in *count.
auto DropTc(Game* game, Unit* monster, Unit* killer, const char* name, Unit** items = nullptr, int32_t* count = nullptr) noexcept
	-> bool {
	using namespace d2r::rva;
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTables)(dataFlag);
	const int32_t index = tables == nullptr ? 0 : d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
		At<void*>(tables, TablesTcNames), name, 0);
	const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
										dataFlag, static_cast<uint16_t>(index), 0)
	                              : nullptr;
	if (tc == nullptr) {
		++g_noTc;
		plog::Warn("invaders: no TC \"%s\"", name);
		return false;
	}
	const int32_t ilvl = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatLevel, 0);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, Unit**, int32_t*, int32_t) noexcept>(
		TcDrop)(game, monster, killer, tc, 0, ilvl, 0, items, count, 1);
	return true;
}

// The class's TC list, in PD2's order (its branch for each class, ProjectDiablo.dll 0x10378250). `drop` is one TC; the
// Barbarian's 1h / 2h set follows the weapon it shows, the Amazon's and Sorceress's sets are the bow and the staff they show (`parts`: its part bytes, invader_look.h has the mapping for every class), `set` gets the chosen set's name.
template <class Drop>
void ClassDrops(int32_t which, const uint8_t* parts, const char** set, Drop&& drop) noexcept {
	// the common set first: gloves, belt, boots, two rings, an amulet
	drop("InvaderGloves");
	drop("InvaderBelt");
	drop("InvaderBoots");
	drop("InvaderRing");
	drop("InvaderRing");
	drop("InvaderAmulet");
	switch (which) {
	case 0:  // Amazon
		drop("InvaderCirclets");
		drop("InvaderBodyAll");
		// every Amazon shows a bow
		*set = "bow (look)";
		drop("InvaderAmazonBow");
		drop("InvaderAmazonQuiver");
		break;
	case 1:  // Assassin
		drop("InvaderGenericHelm");
		drop("InvaderBodyAll");
		drop("InvaderAssassinWeapon");
		drop("InvaderAssassinWeapon");
		break;
	case 2:  // Barbarian: the weapon shown, one 2-hander or two 1-handers
		drop("InvaderBarbarianHelm");
		drop("InvaderBodyMedHeavy");
		if (pd2::invader_look::Barbarian(parts) == pd2::invader_look::Weapon::TwoHand) {
			*set = "2h (look)";
			drop("InvaderBarbarian2h");
		} else {
			*set = "1h x2 (look)";
			drop("InvaderBarbarian1h");
			drop("InvaderBarbarian1h");
		}
		break;
	case 3:  // Druid
		drop("InvaderDruidHelm");
		drop("InvaderBodyAll");
		drop("InvaderDruidWeapon");
		drop("InvaderGenericShield");
		break;
	case 4:  // Necromancer
		drop("InvaderGenericHelm");
		drop("InvaderBodyLightMed");
		drop("InvaderNecroWeapon");
		drop("InvaderNecroShield");
		break;
	case 5:  // Paladin
		drop("InvaderGenericHelm");
		drop("InvaderBodyMedHeavy");
		drop("InvaderPaladinWeapon");
		drop("InvaderPaladinShield");
		break;
	default:  // Sorceress
		drop("InvaderCirclets");
		drop("InvaderBodyLightMed");
		*set = "staff (look)";
		drop("InvaderSorc2hWeapon");  // every Sorceress shows a staff
		break;
	}
}

// " code q<quality> #id" of an item, appended to `line`
void AppendItem(char* line, size_t size, Unit* item) noexcept {
	const size_t used = std::strlen(line);
	const uint32_t code = d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::sig::ItemCode)(item);
	const int32_t quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::ItemQuality)(item);
	std::snprintf(line + used, size - used, " %.4s q%d #%u", reinterpret_cast<const char*>(&code), quality,
	              At<uint32_t>(item, d2r::rva::UnitIdField));
}

// At the spawn: the class's TCs rolled now, in PD2's order, the picks held off the floor for the death
void RollGear(Game* game, Unit* invader, Unit* player, int32_t which) noexcept {
	using namespace d2r::rva;
	std::vector<uint32_t> held;
	char line[1024] = "";
	int32_t missing = 0, onFloor = 0;
	const auto unitRoom = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom);
	const auto roll = [&](const char* name) noexcept {
		Unit* items[6] = {};
		int32_t n = 0;
		missing += DropTc(game, invader, player, name, items, &n) ? 0 : 1;
		for (int32_t i = 0; i < n && i < 6; ++i) {
			if (items[i] != nullptr && unitRoom(items[i]) != nullptr) {
				TakeOffFloor(game, items[i]);
				onFloor += unitRoom(items[i]) != nullptr ? 1 : 0;  // must stay 0: logged as proof
				held.push_back(At<uint32_t>(items[i], UnitIdField));
				AppendItem(line, sizeof(line), items[i]);
			}
		}
	};
	const uint8_t* data = At<const uint8_t*>(invader, UnitData);
	const char* set = "no look to choose";
	ClassDrops(which, data != nullptr ? data + 0x08 : nullptr, &set, roll);
	const uint32_t id = At<uint32_t>(invader, UnitIdField);
	g_held.fetch_add(static_cast<uint32_t>(held.size()));
	plog::Info("invaders: %s #%u holds %zu items, %d still in a room (weapon set %s%s):%s", ClassNames[which], id, held.size(),
	           onFloor, set, missing != 0 ? ", some TCs missing" : "", line);
	const std::lock_guard lock(g_heldLock);
	g_heldItems[HeldKey(game, id)] = std::move(held);
}

// At the death: the held items on the floor by the invader, as the cursor drop puts one down. False when none was held.
auto DropHeld(Game* game, Unit* monster, int32_t which) noexcept -> bool {
	using namespace d2r::rva;
	std::vector<uint32_t> held;
	{
		const std::lock_guard lock(g_heldLock);
		const auto it = g_heldItems.find(HeldKey(game, At<uint32_t>(monster, UnitIdField)));
		if (it == g_heldItems.end()) {
			return false;
		}
		held = std::move(it->second);
		g_heldItems.erase(it);
	}
	int32_t xy[2] = {};
	d2r::UnitXY(monster, &xy[0], &xy[1]);
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(monster);
	char line[1024] = "";
	uint32_t lost = 0;
	for (const uint32_t itemId : held) {
		Unit* item = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(game, 4, itemId);
		// only a held item: still on no floor (a reused id or a moved item is never touched)
		int32_t at[2] = {};
		void* spot = item != nullptr && d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(item) == nullptr &&
				At<int32_t>(item, UnitMode) == ItemModeStored && room != nullptr
			? d2r::Engine<void* (*)(Game*, void*, int32_t*, int32_t*, uint8_t) noexcept>(DropSpot)(game, room, xy, at, 1)
			: nullptr;
		if (spot == nullptr) {
			++lost;
			continue;
		}
		d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, int32_t, int32_t) noexcept>(DropOnFloor)(game, nullptr, item, spot, at[0],
		                                                                                           at[1]);
		AppendItem(line, sizeof(line), item);
	}
	g_released.fetch_add(static_cast<uint32_t>(held.size()) - lost);
	g_lost.fetch_add(lost);
	plog::Info("invaders: %s #%u dropped %zu held items%s:%s", ClassNames[which], At<uint32_t>(monster, UnitIdField),
	           held.size() - lost, lost != 0 ? " (some with no unit or spot, left to the game)" : "", line);
	return true;
}

void OnInvaderDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                    void* userData) noexcept {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != UnitType::Monster || event->monster.classId < FirstInvader ||
	    event->monster.classId >= FirstInvader + InvaderCount || event->gameId > 0xffff) {
		return;
	}
	Game* game = GameOf(event->gameId);
	Unit* monster = game != nullptr ? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, event->monster.id)
	                                : nullptr;
	const int32_t which = event->monster.classId - FirstInvader;
	// held gear drops whatever the event is now; with none held, the event's invaders roll at death as before
	const bool held = monster != nullptr && g_gearOn && DropHeld(game, monster, which);
	if (game == nullptr || (!held && MapEvent(game) != InvadersEvent)) {
		return;
	}
	// shortcut: a kill by a missile, a pet or a console cheat rolls for the game's first player (as horazon.cpp);
	// find the owner of a missile or pet if multiplayer ever needs it
	Unit* killer = event->killer.type == UnitType::Player
		? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 0, event->killer.id)
		: nullptr;
	auto* client = At<uint8_t*>(game, GameClients);
	killer = killer != nullptr || client == nullptr ? killer
		: d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
	if (monster == nullptr || killer == nullptr) {
		return;
	}
	++g_deaths;
	int32_t tcs = 0, missing = 0;
	const auto drop = [&](const char* name) noexcept {
		(DropTc(game, monster, killer, name) ? tcs : missing) += 1;
	};
	// the part bytes (UnitData +0x08, 16 of them): logged whole so one kill shows the offset is right
	const uint8_t* data = At<const uint8_t*>(monster, UnitData);
	const uint8_t* parts = data != nullptr ? data + 0x08 : nullptr;
	const char* set = held ? "held since the spawn" : "no look to choose (the class's roll or single set)";
	if (!held) {
		ClassDrops(which, parts, &set, drop);
	}
	// the ear: a normal item of the class's code
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(dataFlag, Ears[which]);
	bool ear = false;
	if (row >= 0) {
		ear = d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
			game, monster, row, QualityNormal, 0, 0) != nullptr;
	}
	g_drops.fetch_add(static_cast<uint32_t>(tcs));
	char hex[3 * pd2::invader_look::PartBytes + 1] = "";
	for (int i = 0; parts != nullptr && i < pd2::invader_look::PartBytes; ++i) {
		std::snprintf(hex + 3 * i, sizeof(hex) - 3 * i, "%02x ", parts[i]);
	}
	plog::Info("invaders: %s (class %d) parts HD TR LG RA LA RH LH SH S1-S8 = %s; weapon drop set: %s", ClassNames[which],
	           event->monster.classId, hex, set);
	plog::Info("invaders: %s (class %d) died, ear %.4s %s, %d TC drops%s", ClassNames[which], event->monster.classId,
	           reinterpret_cast<const char*>(&Ears[which]), ear ? "dropped" : "NOT dropped", tcs,
	           missing != 0 ? ", some TCs missing" : "");
}

}  // namespace

auto InstallInvaders(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const bool ok = ai_registry::RegisterAt(context, OverseerRow, d2r::sig::AiOverseer.address, PortalClass, &ThinkPortal);
	if (!ok) {
		std::snprintf(report, reportSize, "off: monai %d think not swapped", OverseerRow);
		return 0;
	}
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::Lifecycle::MonsterDeathListener listener {
		.structSize = D2RL::Lifecycle::MonsterDeathListenerSize, .flags = 0, .callback = OnInvaderDeath, .userData = nullptr};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	const bool drops = context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success &&
	                   D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) &&
	                   lifecycle->registerMonsterDeathListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success;
	// the gear held from the spawn: every floor step found, the store cleared when the game is left
	sigscan::Resolve(GearEntries, std::size(GearEntries), context->exeBase, nullptr, 0);
	const D2RL::Lifecycle::GameplayEventListener left {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                   .kind = D2RL::Lifecycle::GameplayEventKind::GameLeft,
	                                                   .callback = ForgetHeld};
	D2RL::Lifecycle::ListenerHandle leftHandle = D2RL::Lifecycle::InvalidHandle;
	using sigscan::State;
	bool found = d2r::sig::ItemCode.state == State::Ok && d2r::sig::ItemQuality.state == State::Ok &&
	             d2r::sig::SetUnitFlags.state == State::Ok && d2r::sig::SetObjectMode.state == State::Ok &&
	             d2r::sig::UnitRoom.state == State::Ok && d2r::sig::FindUnit.state == State::Ok;
	for (const sigscan::Entry* e : GearEntries) {
		if (e->state != State::Ok) {
			found = false;
			plog::Warn("invaders: %s %s", e->name, sigscan::StateText(e->state));
		}
	}
	g_gearOn = drops && found &&
	           lifecycle->registerGameplayEventListener(context, &left, &leftHandle) == D2RL::Lifecycle::Result::Success;
	if (drops && !g_gearOn) {
		plog::Warn("invaders: gear not held from the spawn (an entry not found): invaders roll at death");
	}
	std::snprintf(report, reportSize, "the invader portal (monai %d think, class %d); invaders keep their table AIs%s",
	              OverseerRow, PortalClass,
	              g_gearOn ? ", gear rolled at the spawn, dropped on death" : drops ? ", ears and drops on death" : ", no death drops");
	return 1;
}

auto InvadersCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "invaders: %u spawned, %u portals closed, %u deaths, %u TC drops, %u TCs missing, %u items held, %u dropped, %u lost",
	              g_spawned.load(), g_closed.load(), g_deaths.load(), g_drops.load(), g_noTc.load(), g_held.load(),
	              g_released.load(), g_lost.load());
}
