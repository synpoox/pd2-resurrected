// The Corrupted Heroes map event (event 20; docs/research/pd2-map-system-dll.md, section 4): map_events.cpp opens up to
// three InvaderPortals as the map's rooms activate, and each portal sends out invaders, monsters that play a PD2
// class. The invaders run their monstats AIs (BloodRaven, Duriel, Summoner), not PD2's own brain (MS11, #225).
//
// - The portal (PD2 0x1035dca0's branch for 0x49e, monai 120 Overseer): while the event is 20 it waits until a player
//   is within its aidist, then charges for aip1 thinks (stat mon_cooldown2) and sends the next invader to a free spot
//   by it, with a sound to the level, the class's notify line and state dcloneset (its aura). The classes come in an
//   order shuffled once per game, so five invaders are five classes. The first portal closes after the first invader,
//   the second after the third, the third after the fifth; a portal past those counts closes at once.
// - An invader's death (PD2 0x10378250, through its death switch 0x10378d80): its class's ear (normal, one) and its
//   gear, TreasureClassEx rows `Invader*` by the class, some picked by a roll.
//
// The think goes through ai_hooks (no SDK service runs monster AI); the death through the SDK's lifecycle listener.
// The counts live in D2R's per-game counters (rva::GameCounter), as map_events.cpp's.
#include "invaders.h"

#include "ai_hooks.h"
#include "level_fx.h"
#include "log.h"
#include "lucion.h"
#include "map_events.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t InvadersEvent = 20;
constexpr int32_t OverseerRow = 120;  // monai.txt
// Merged monstats ids (PD2 1182 InvaderPortal, 1183-1189 the invaders in this order)
constexpr int32_t Portal = 1199;
constexpr int32_t FirstInvader = 1200;
constexpr int Classes = 7;
enum : int { Amazon, Assassin, Barbarian, Druid, Necromancer, Paladin, Sorceress };
constexpr const char* ClassNotify[Classes] = {
	"d2r:MapEventNotifyInvadersAmazon",      "d2r:MapEventNotifyInvadersAssassin", "d2r:MapEventNotifyInvadersBarbarian",
	"d2r:MapEventNotifyInvadersDruid",       "d2r:MapEventNotifyInvadersNecromancer",
	"d2r:MapEventNotifyInvadersPaladin",     "d2r:MapEventNotifyInvadersSorceress",
};
constexpr const char* EarNames[Classes] = {"ivez", "ivea", "iveb", "ived", "iven", "ivep", "ives"};
const uint32_t Ears[Classes] = {
	D2RL::Items::MakeItemCode("ivez"), D2RL::Items::MakeItemCode("ivea"), D2RL::Items::MakeItemCode("iveb"),
	D2RL::Items::MakeItemCode("ived"), D2RL::Items::MakeItemCode("iven"), D2RL::Items::MakeItemCode("ivep"),
	D2RL::Items::MakeItemCode("ives"),
};

constexpr uint32_t InvadersKey = 0x4d49564e;  // 'MIVN': invaders sent so far (PD2's game +0x262a in event 20)
constexpr uint32_t ClosedKey = 0x4d495643;    // 'MIVC': portals closed so far (PD2's game +0x2629)
constexpr uint32_t OrderKey = 0x4d495653;     // 'MIVS': the seed of this game's class order (PD2's game +0xec)
constexpr int32_t MaxInvaders = 5, LastPortal = 2, MaxPortals = 3;
constexpr int32_t StatCooldown2 = 500;  // mon_cooldown2 (PD2 447): the portal's charge
constexpr int32_t StatLevel = 12;
constexpr int32_t StateAura = 255;      // dcloneset (PD2 209), overlay dclone_aura
constexpr int32_t PortalSound = 0x10;   // sounds.txt row PD2 plays to the level with each invader
constexpr int32_t NearDelay = 5;        // PD2's delay while no player is within aidist
constexpr int32_t ModeNeutral = 1;

std::atomic<uint32_t> g_thinks {0};
std::atomic<uint32_t> g_sent {0};
std::atomic<uint32_t> g_closed {0};
std::atomic<uint32_t> g_noSpot {0};
std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_drops {0};

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

auto Seed(Unit* unit) noexcept -> uint32_t* {
	return d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit);
}
auto Delay(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiSetDelay)(game, unit, frames);
}

// PD2 shuffles the seven classes and their notify lines alike (0x10361290 seeded with game +0xec, then 0x10361430)
// on each think, so the n-th invader is always the same class in a game. The port keeps one seed per game instead.
auto ClassOf(Game* game, Unit* portal, int32_t n) noexcept -> int {
	int32_t seed = Get(game, OrderKey);
	if (seed == 0) {
		seed = static_cast<int32_t>(d2r::RandomBelow(Seed(portal), 0x7fffffff)) | 1;
		Set(game, OrderKey, seed);
	}
	int order[Classes] = {Amazon, Assassin, Barbarian, Druid, Necromancer, Paladin, Sorceress};
	std::mt19937 gen(static_cast<uint32_t>(seed));
	std::shuffle(std::begin(order), std::end(order), gen);
	return order[n % Classes];
}

// A bare state, as unique_maps.cpp sets one: the bit UnitHasState reads, then its gfx bit (#623)
auto SetAura(Unit* unit) noexcept -> void {
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetStateBit)(unit, StateAura, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetStateGfxFlag)(unit, StateAura, 1);
}

// PD2's 0x103130a0 after its spot search 0x10389ad0 (size 3) by the portal: D2R's free-spot search, as
// event_portals.cpp's OpenPortal does, then D2R's monster spawn
auto SendInvader(Game* game, Unit* portal, int32_t classId) noexcept -> Unit* {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(portal);
	if (room == nullptr) {
		return nullptr;
	}
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(portal, xy);
	void* found = nullptr;
	d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(FindFreeSpot)(
		game, room, xy, 3, 0x400, &found, 5, 100);
	if (found == nullptr) {
		return nullptr;
	}
	return d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster)(game, found, xy[0], xy[1], classId, ModeNeutral, -1, 0x42);
}

// shortcut: with no target the portal waits as when no player is near; PD2 reads the tick's distance either way
void ThinkPortal(Game* game, Unit* portal, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	const auto* rec = tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr;
	if (game == nullptr || portal == nullptr || rec == nullptr) {
		VanillaAiThink(OverseerRow)(game, portal, tick);
		return;
	}
	++g_thinks;
	const int difficulty = std::min<int>(At<uint8_t>(tick, AiTickDifficulty), 2);
	const int32_t aidel = At<uint8_t>(rec, MonAiDel + difficulty);
	if (MapEvent(game) != InvadersEvent) {
		Delay(game, portal, aidel);
		return;
	}
	int32_t sent = Get(game, InvadersKey);
	const int32_t closed = Get(game, ClosedKey);
	if (sent >= MaxInvaders || closed > LastPortal || MapEventSpawns(game) > MaxPortals) {
		KillQuietly(game, portal);
		return;
	}
	const Unit* target = At<Unit*>(tick, AiTickTarget);
	if (target == nullptr || At<int32_t>(tick, AiTickDistance) > At<uint8_t>(rec, MonAiDist + difficulty)) {
		Delay(game, portal, NearDelay);
		return;
	}
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t charge = stat(portal, StatCooldown2, 0);
	if (charge < At<int16_t>(rec, MonAiParams)) {  // aip1, PD2 reads the Normal column whatever the difficulty
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(portal, StatCooldown2, charge + 1, 0);
		Delay(game, portal, aidel);
		return;
	}
	const int cls = ClassOf(game, portal, sent);
	if (Unit* invader = SendInvader(game, portal, FirstInvader + cls); invader != nullptr) {
		Set(game, InvadersKey, ++sent);
		void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(portal);
		PlayLevelSound(game, room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : -1,
		               PortalSound);
		MapEventNotify(game, ClassNotify[cls]);
		SetAura(invader);
		plog::Info("invaders: portal %d sent invader %d (class %d), %d so far (%u)", closed + 1, sent,
		           FirstInvader + cls, sent, ++g_sent);
	} else {
		plog::Warn("invaders: portal %d found no spot for class %d (%u)", closed + 1, FirstInvader + cls, ++g_noSpot);
	}
	// The first portal closes after 1 invader, the second after 3, the third after 5
	const bool done = closed == 0 ? sent >= 1 : closed == 1 ? sent >= 3 : sent >= MaxInvaders;
	if (!done) {
		Delay(game, portal, aidel);
		return;
	}
	Set(game, ClosedKey, closed + 1);
	plog::Info("invaders: portal %d closes after %d invaders (%u)", closed + 1, sent, ++g_closed);
	KillQuietly(game, portal);
}

// One class's drop: a TC name, and how many times it drops
struct Pick {
	const char* tc;
	int times;
};

auto DropTc(Game* game, Unit* monster, Unit* killer, int32_t ilvl, const Pick& pick) noexcept -> int {
	using namespace d2r::rva;
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
	const int32_t index = tables != nullptr ? d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
												  At<void*>(tables, TablesTcNames), pick.tc, 0)
	                                        : 0;
	const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
										dataFlag, static_cast<uint16_t>(index), 0)
	                              : nullptr;
	if (tc == nullptr) {
		plog::Warn("invaders: no TC \"%s\"", pick.tc);
		return 0;
	}
	for (int i = 0; i < pick.times; ++i) {
		d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, void*, int32_t*,
		                     int32_t) noexcept>(TcDrop)(game, monster, killer, tc, 0, ilvl, 0, nullptr, nullptr, 1);
	}
	return pick.times;
}

// PD2 0x10378250: every invader drops gloves, a belt, boots, two rings and an amulet, then its class's ear and gear.
// A roll (the CRT's rand() in PD2, the monster's seed here) picks between weapon sets.
auto Gear(int cls, uint32_t* seed, Pick (&out)[4]) noexcept -> int {
	const auto roll = [seed](uint32_t n) noexcept { return d2r::RandomBelow(seed, n); };
	switch (cls) {
	case Assassin:
		out[0] = {"InvaderGenericHelm", 1}, out[1] = {"InvaderBodyAll", 1}, out[2] = {"InvaderAssassinWeapon", 2};
		return 3;
	case Barbarian: {
		out[0] = {"InvaderBarbarianHelm", 1}, out[1] = {"InvaderBodyMedHeavy", 1};
		const uint32_t r = roll(3);
		out[2] = r == 0 ? Pick {"InvaderBarbarian2h", 1}
			: r == 1    ? Pick {"InvaderBarbarianThrow", 2}
			            : Pick {"InvaderBarbarian1h", 2};
		return 3;
	}
	case Druid:
		out[0] = {"InvaderDruidHelm", 1}, out[1] = {"InvaderBodyAll", 1}, out[2] = {"InvaderDruidWeapon", 1};
		out[3] = {"InvaderGenericShield", 1};
		return 4;
	case Necromancer:
		out[0] = {"InvaderGenericHelm", 1}, out[1] = {"InvaderBodyLightMed", 1}, out[2] = {"InvaderNecroWeapon", 1};
		out[3] = {"InvaderNecroShield", 1};
		return 4;
	case Paladin:
		out[0] = {"InvaderGenericHelm", 1}, out[1] = {"InvaderBodyMedHeavy", 1}, out[2] = {"InvaderPaladinWeapon", 1};
		out[3] = {"InvaderPaladinShield", 1};
		return 4;
	case Sorceress:
		out[0] = {"InvaderCirclets", 1}, out[1] = {"InvaderBodyLightMed", 1};
		if (roll(2) == 0) {
			out[2] = {"InvaderSorc2hWeapon", 1};
			return 3;
		}
		out[2] = {"InvaderSorc1hWeapon", 1}, out[3] = {"InvaderGenericShield", 1};
		return 4;
	default: {  // Amazon, PD2's switch default too
		out[0] = {"InvaderCirclets", 1}, out[1] = {"InvaderBodyAll", 1};
		switch (roll(4)) {
		case 0: out[2] = {"InvaderAmazonBow", 1}, out[3] = {"InvaderAmazonQuiver", 1}; return 4;
		case 1: out[2] = {"InvaderAmazonCrossbow", 1}, out[3] = {"InvaderAmazonBolt", 1}; return 4;
		case 2: out[2] = {"InvaderAmazon1hJavelin", 1}, out[3] = {"InvaderGenericShield", 1}; return 4;
		default: out[2] = {"InvaderAmazon2hSpear", 1}; return 3;
		}
	}
	}
}

constexpr Pick Jewellery[] = {
	{"InvaderGloves", 1}, {"InvaderBelt", 1}, {"InvaderBoots", 1}, {"InvaderRing", 2}, {"InvaderAmulet", 1},
};

// shortcut: the drops' item level is the invader's level; PD2 reads it through an engine pointer (0x103a1b00) not read
// shortcut: the ear drops by the invader through D2R's drop (rva::DropItemAt), where PD2 first finds a free spot by it
// (0x1036ebc0, size 2)
auto DropAll(Game* game, Unit* monster, Unit* killer, int cls) noexcept -> int {
	using namespace d2r::rva;
	const int32_t ilvl = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatLevel, 0);
	int dropped = 0;
	for (const Pick& p : Jewellery) {
		dropped += DropTc(game, monster, killer, ilvl, p);
	}
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const int32_t ear = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(
		dataFlag, Ears[cls]);
	if (ear >= 0 && d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
						game, monster, ear, QualityNormal, 0, 0) != nullptr) {
		++dropped;
	} else {
		plog::Warn("invaders: no %s ear (item row %d)", EarNames[cls], ear);
	}
	Pick gear[4] {};
	const int n = Gear(cls, Seed(monster), gear);
	for (int i = 0; i < n; ++i) {
		dropped += DropTc(game, monster, killer, ilvl, gear[i]);
	}
	return dropped;
}

void OnMonsterDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                    void* userData) noexcept {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != UnitType::Monster || event->monster.classId < FirstInvader ||
	    event->monster.classId >= FirstInvader + Classes || event->gameId > 0xffff) {
		return;
	}
	const auto id = static_cast<uint16_t>(event->gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
	if (game == nullptr || At<uint16_t>(game, GameIdField) != id || MapEvent(game) != InvadersEvent) {
		return;
	}
	Unit* monster = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, event->monster.id);
	// shortcut: a kill by a missile, a pet or a console cheat rolls for the game's first player, as event_portals.cpp's
	Unit* killer = event->killer.type == UnitType::Player
		? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 0, event->killer.id)
		: nullptr;
	auto* client = At<uint8_t*>(game, GameClients);
	killer = killer != nullptr || client == nullptr ? killer
		: d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
	if (monster == nullptr || killer == nullptr) {
		return;
	}
	const int cls = static_cast<int>(event->monster.classId) - FirstInvader;
	const int dropped = DropAll(game, monster, killer, cls);
	g_drops += static_cast<uint32_t>(dropped);
	plog::Info("invaders: class %d died in level %d, %d drops (%u deaths)", event->monster.classId, event->levelId,
	           dropped, ++g_deaths);
}

}  // namespace

auto InstallInvaders(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const bool portal = RegisterAiThink(context, OverseerRow, d2r::rva::AiOverseer, Portal, &ThinkPortal);
	bool deaths = false;
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success &&
	    D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		const D2RL::Lifecycle::MonsterDeathListener listener {
			.structSize = D2RL::Lifecycle::MonsterDeathListenerSize,
			.flags = 0,
			.callback = OnMonsterDeath,
			.userData = nullptr,
		};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		deaths = lifecycle->registerMonsterDeathListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success;
	}
	std::snprintf(report, reportSize, "portal think (monai %d) %s, invader drops %s", OverseerRow,
	              portal ? "on" : "FAILED", deaths ? "on" : "FAILED (no lifecycle listener)");
	return (portal ? 1 : 0) + (deaths ? 1 : 0);
}

auto InvadersCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "invaders: %u portal thinks, %u sent, %u portals closed, %u without a spot, %u deaths, %u drops",
	              g_thinks.load(), g_sent.load(), g_closed.load(), g_noSpot.load(), g_deaths.load(), g_drops.load());
}
