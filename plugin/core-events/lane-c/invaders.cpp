// The Invaders map event (#224, MS10): PD2's invader portal think (ProjectDiablo.dll 0x1035dca0, the Overseer AI's portal
// branch) and the invaders' death drops (0x10378250). Decoded in the issue comment on #224.
//
// The portal (monstats 1199, monai row 120 `Overseer`): while the game's event is 20 it waits for a player within aidist
// and its cooldown (stat 500, aip1 ticks), then spawns one invader at its own spot and sends the players the invader's
// class notify. It closes (KillQuietly) after 1, then 3 in all, then 5 in all invaders over its three portals. Beyond 4
// invaders, 2 closed portals or 3 spawns it closes at once. The seven invaders (1200-1206) run D2R's table AIs
// (BloodRaven, Duriel, Summoner...): no think of theirs is registered.
//
// The drops: a dead invader of the event drops its class's ear and a set of treasure classes (`Invader*` rows of
// TreasureClassEx) at its own level, all through D2R's TC drop for the killer. PD2's logic, in the port's TC names.
//
// The portal's think is registered through the toolkit's AI registry (no SDK service runs monster AI); the death is the
// SDK's monster-death listener, which hands over ids only, so the game, unit and TC are engine calls as horazon.cpp's.
#include "invaders.h"

#include "ai_common.h"
#include "ai_hooks.h"
#include "corruption_roll.h"
#include "log.h"
#include "map_events.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>

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

// One treasure-class drop at the monster's level; false when the table has no such TC
auto DropTc(Game* game, Unit* monster, Unit* killer, const char* name) noexcept -> bool {
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
	d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, void*, int32_t*, int32_t) noexcept>(
		TcDrop)(game, monster, killer, tc, 0, ilvl, 0, nullptr, nullptr, 1);
	return true;
}

// The class's TC list, in PD2's order (its branch for each class, ProjectDiablo.dll 0x10378250). `drop` is one TC; the
// Barbarian's, Sorceress's and Amazon's pick a weapon set by a roll.
template <class Drop>
void ClassDrops(int32_t which, Drop&& drop) noexcept {
	switch (which) {
	case 0:  // Amazon
		drop("InvaderCirclets");
		drop("InvaderBodyAll");
		switch (RollRand() & 3) {
		case 0: drop("InvaderAmazonBow"); drop("InvaderAmazonQuiver"); break;
		case 1: drop("InvaderAmazonCrossbow"); drop("InvaderAmazonBolt"); break;
		case 2: drop("InvaderAmazon1hJavelin"); drop("InvaderGenericShield"); break;
		default: drop("InvaderAmazon2hSpear"); break;
		}
		break;
	case 1:  // Assassin
		drop("InvaderGenericHelm");
		drop("InvaderBodyAll");
		drop("InvaderAssassinWeapon");
		drop("InvaderAssassinWeapon");
		break;
	case 2:  // Barbarian: one 2-hander, two throwing weapons or two 1-handers
		drop("InvaderBarbarianHelm");
		drop("InvaderBodyMedHeavy");
		switch (RollRand() % 3) {
		case 0: drop("InvaderBarbarian2h"); break;
		case 1: drop("InvaderBarbarianThrow"); drop("InvaderBarbarianThrow"); break;
		default: drop("InvaderBarbarian1h"); drop("InvaderBarbarian1h"); break;
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
	default:  // Sorceress: a 2-hander, or a 1-hander and a shield
		drop("InvaderCirclets");
		drop("InvaderBodyLightMed");
		if ((RollRand() & 1) == 0) {
			drop("InvaderSorc2hWeapon");
		} else {
			drop("InvaderSorc1hWeapon");
			drop("InvaderGenericShield");
		}
		break;
	}
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
	if (game == nullptr || MapEvent(game) != InvadersEvent) {
		return;
	}
	Unit* monster = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, event->monster.id);
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
	const int32_t which = event->monster.classId - FirstInvader;
	++g_deaths;
	int32_t tcs = 0, missing = 0;
	const auto drop = [&](const char* name) noexcept {
		(DropTc(game, monster, killer, name) ? tcs : missing) += 1;
	};
	// the common set: gloves, belt, boots, two rings, an amulet
	drop("InvaderGloves");
	drop("InvaderBelt");
	drop("InvaderBoots");
	drop("InvaderRing");
	drop("InvaderRing");
	drop("InvaderAmulet");
	ClassDrops(which, drop);
	// the ear: a normal item of the class's code
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(dataFlag, Ears[which]);
	bool ear = false;
	if (row >= 0) {
		ear = d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
			game, monster, row, QualityNormal, 0, 0) != nullptr;
	}
	g_drops.fetch_add(static_cast<uint32_t>(tcs));
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
	std::snprintf(report, reportSize, "the invader portal (monai %d think, class %d); invaders keep their table AIs%s",
	              OverseerRow, PortalClass, drops ? ", ears and drops on death" : ", no death drops");
	return 1;
}

auto InvadersCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "invaders: %u spawned, %u portals closed, %u deaths, %u TC drops, %u TCs missing", g_spawned.load(),
	              g_closed.load(), g_deaths.load(), g_drops.load(), g_noTc.load());
}
