// The event Nihlathak AI: PD2's 0x1035bc60 nihlathakMap branch (map boss 0x39a, #538), split out of dclone_ai.cpp
// (#943): stages of mob waves, then RathmaDeath. Registers on monai row 99 through ai_hooks (one AI registry).
#include "nihlathak_ai.h"

#include "ai_common.h"
#include "ai_hooks.h"
#include "log.h"
#include "hot_log.h"
#include "uber_drops.h"

#include <D2RLPlugin/item.h>

#include <atomic>
#include <iterator>

namespace {

using namespace ai_common;
using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t TrappedSoulRow = 99;  // monai.txt
// The stage is PD2's game+0x2628, map_events.cpp's spawn count: the Shadow of Mendeln spawn sets it to 1.
constexpr uint32_t StageKey = 0x4d45564e;  // 'MEVN', as map_events.cpp
constexpr int32_t NihlathakMap = 939;      // PD2 922
// PD2's two sets (0x105ddfe0 {-1, 923, 924, 926, 927}, 0x105ddf74 {928-931}): a wave monster is one of five picks,
// zombie, charger, skeleton, archer, or (the -1) one of the four mages
constexpr int32_t NihlMobs[] = {940, 941, 943, 944};   // zombie, charger, skele, archerNihlMinion
constexpr int32_t NihlMages[] = {945, 946, 947, 948};  // mFire, mLtng, mCold, mPoisNihlMinion

// The aip row that gives stage `stage`'s wave counts (2, 3 or 4), 0 past the waves; a0-a2 are aip1's three columns
constexpr auto NihlWaveRow(int32_t stage, int32_t a0, int32_t a1, int32_t a2) noexcept -> int32_t {
	const int32_t i = stage - 1;
	return i < a0 + 1 ? 2 : i < a0 + a1 + 1 ? 3 : i < a0 + a1 + a2 + 1 ? 4 : 0;
}
// nihlathakMap's aip1 is 20, 15, 10: waves at stages 2-46, the pause at 47, the nova at 48, death at 49
static_assert(NihlWaveRow(2, 20, 15, 10) == 2 && NihlWaveRow(21, 20, 15, 10) == 2 && NihlWaveRow(22, 20, 15, 10) == 3 &&
              NihlWaveRow(37, 20, 15, 10) == 4 && NihlWaveRow(46, 20, 15, 10) == 4 && NihlWaveRow(47, 20, 15, 10) == 0);

std::atomic<uint32_t> g_nihlStages {0};
std::atomic<uint32_t> g_nihlSpawned {0};
std::atomic<uint32_t> g_nihlNovas {0};
std::atomic<uint32_t> g_nihlDeaths {0};
std::atomic<uint32_t> g_nihlDrops {0};

// PD2 0x10375fe0 (docs/research/map-events-audit.md section 2): the death step drops one rtmv (ilvl 131), then 20-24
// items of the ShadowOfMendeln TC at the boss's level, three per roll, the boss itself as the killer.
constexpr const char* MendelnTc = "ShadowOfMendeln";
constexpr int32_t SoulItemLevel = 131, LootMin = 20, LootSpread = 5, LootPerRoll = 3, MaxRolls = 40, StatLevel = 12;
static_assert(LootMin + LootSpread - 1 == 24);

// shortcut: all items drop on one frame, where PD2 spreads them 5 frames apart (0x10372780); stagger them if the pile
// must land like PD2's shower
auto DropMendelnLoot(Game* game, Unit* unit) noexcept -> void {
	using namespace d2r::rva;
	const bool soul = DropUniqueAt(game, unit, D2RL::Items::MakeItemCode("rtmv"), -1, SoulItemLevel, 2);
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTables)(dataFlag);
	const int32_t index = tables != nullptr ? d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
												  At<void*>(tables, TablesTcNames), MendelnTc, 0)
	                                        : 0;
	const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
										dataFlag, static_cast<uint16_t>(index), 0)
	                              : nullptr;
	if (tc == nullptr) {
		plog::Warn("nihlathakMap: no TC \"%s\", rtmv %s", MendelnTc, soul ? "dropped" : "not dropped");
		return;
	}
	const int32_t ilvl = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(unit, StatLevel, 0);
	const int32_t target = LootMin + static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit), LootSpread));
	int32_t dropped = 0;
	for (int32_t roll = 0; roll < MaxRolls && dropped < target; ++roll) {
		Unit* items[6] = {};
		int32_t n = 0;
		d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, Unit**, int32_t*, int32_t) noexcept>(
			TcDrop)(game, unit, unit, tc, 0, ilvl, 0, items, &n, LootPerRoll);
		dropped += n;
	}
	++g_nihlDrops;
	plog::Info("nihlathakMap: rtmv %s and %d of %d items from %s at ilvl %d", soul ? "dropped" : "not dropped", dropped,
	           target, MendelnTc, ilvl);
}

// D2R's champion and unique makers (docs/research/map-events-audit.md section 6), our own signatures; 93847 RVAs are
// for reference only. The waves fall back to plain spawns when one does not resolve.
// 93847: 0x499880, Unit* (game, room, area, classId, mayChampion, x, y, 1, flags): spawn + unique mods + 3-6 minions
sigscan::Entry SpawnUnique {"MonsterSpawnUnique", "waves", sigscan::Kind::Code, 0, 0,
                            "48 89 54 24 10 55 41 54 41 55 41 57 48 8d 6c 24 e9 48 81 ec a8 00 00 00 0f b7 45 7f 45 8b d9 44 0f b7 55 6f"};
// 93847: 0x49ab40, void (game, unit, umod): makes the unit a champion with that monumod row
sigscan::Entry MakeChampion {"MonsterMakeChampion", "waves", sigscan::Kind::Code, 0, 0,
                             "48 89 5c 24 18 48 89 6c 24 20 56 48 83 ec 20 41 8b e8 48 8b da 48 8b f1 48 85 d2 74 0d 48 8b ca e8 ?? ?? ?? ?? 83 f8 01"};
// 93847: 0x492b00, Unit* (game, near, classId, mode, range, flags): a monster next to a unit
sigscan::Entry SpawnNear {"SpawnMonsterNear", "waves", sigscan::Kind::Code, 0, 0,
                          "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 60 48 8b d9 44 89 44 24 38 33 f6 44 89"};
sigscan::Entry* const WaveMakers[] = {&SpawnUnique, &MakeChampion, &SpawnNear};
bool g_makers = false;

enum class WaveKind { Normal, Champion, Unique };
constexpr const char* WaveKindName[] = {"normal", "champion", "unique"};
constexpr uint8_t ChampionMods[] = {16, 36, 37, 38, 39};  // PD2's cpick monumod rows, weight 1 each

// PD2 0x1037cbf0 / 0x1037caf0. A unique is D2R's maker (mods, 3-6 minions); a champion is a plain spawn made a champion
// with one mod, plus 1-3 pack mates with mod 16 (D2R's 0x50b1b0, which needs a populator area we do not have).
// The spot search is D2R's portal one, not PD2's 0x10389c20.
auto SpawnWaveMonster(Game* game, Unit* unit, int32_t ux, int32_t uy, uint32_t* seed, WaveKind kind) noexcept -> bool {
	using namespace d2r::rva;
	int32_t xy[2] = {ux - 10 + static_cast<int32_t>(d2r::RandomBelow(seed, 20)),
	                 uy - 10 + static_cast<int32_t>(d2r::RandomBelow(seed, 20))};
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	room = room != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(room, xy[0], xy[1]) : nullptr;
	void* found = nullptr;
	if (room != nullptr) {
		d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(
			FindFreeSpot)(game, room, xy, 3, 0x400, &found, 5, 100);
	}
	if (found == nullptr) {
		return false;
	}
	const uint32_t pick = d2r::RandomBelow(seed, 5);
	const int32_t monster = pick < 4 ? NihlMobs[pick] : NihlMages[d2r::RandomBelow(seed, 4)];
	if (!g_makers) {
		kind = WaveKind::Normal;
	}
	const auto spawn = [&] {
		return d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
			SpawnMonster)(game, found, xy[0], xy[1], monster, 1, -1, 0x42);
	};
	Unit* leader = nullptr;
	int32_t mates = 0;
	if (kind == WaveKind::Unique) {
		leader = d2r::Engine<Unit* (*)(Game*, void*, void*, int32_t, int32_t, uint16_t, uint16_t, int32_t, uint16_t) noexcept>(
			SpawnUnique)(game, found, nullptr, monster, 0, static_cast<uint16_t>(xy[0]), static_cast<uint16_t>(xy[1]), 1, 0x10);
	} else {
		leader = spawn();
		if (leader != nullptr && kind == WaveKind::Champion) {
			const auto make = d2r::Engine<void (*)(Game*, Unit*, uint8_t) noexcept>(MakeChampion);
			make(game, leader, ChampionMods[d2r::RandomBelow(seed, 5)]);
			const int32_t want = static_cast<int32_t>(d2r::RandomBelow(seed, 3)) + 1;
			for (int32_t k = 0; k < want; ++k) {
				Unit* mate = d2r::Engine<Unit* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint16_t) noexcept>(
					SpawnNear)(game, leader, monster, 1, 4, 0x110);
				if (mate != nullptr) {
					make(game, mate, 16);
					++mates;
				}
			}
		}
	}
	if (leader == nullptr) {
		return false;
	}
	plog::Info("nihlathakMap: wave %s %d at (%d, %d), %d pack mates", WaveKindName[static_cast<int>(kind)], monster, xy[0],
	           xy[1], mates);
	return true;
}

auto ThinkNihlathak(Game* game, Unit* unit, uint8_t* tick) noexcept -> void {
	using namespace d2r::rva;
	Mon m {tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr};
	if (m.rec == nullptr) {
		ai_registry::Vanilla(TrappedSoulRow)(game, unit, tick);
		return;
	}
	const int32_t stage = Counter(game, StageKey);
	if (stage == 0) {  // not spawned by its map event
		Idle(game, unit, 25);
		return;
	}
	Unit*& target = *reinterpret_cast<Unit**>(tick + AiTickTarget);
	if (target == nullptr) {
		target = NearestPlayer(game, unit, LevelOf(unit));
	}
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);
	const int32_t a0 = m.Aip(1, 0), a1 = m.Aip(1, 1), a2 = m.Aip(1, 2);
	const int32_t last = a0 + a1 + a2 + 1, i = stage - 1;
	if (pd2::HotSample(g_nihlStages)) {
		plog::Info("nihlathakMap: stage %d of %d (%u thinks)", stage, last + 3, g_nihlStages.load());
	}
	// PD2 casts these with no target at (0, 0); the port casts at the caster, where srvdo 169 puts the nova anyway
	if (stage == 1) {
		// Wake when a player comes within 14
		if (target == nullptr || Dist2(target, ux, uy) >= 15 * 15) {
			Idle(game, unit, 25);
			return;
		}
		Counter(game, StageKey) = 2;
		plog::Info("nihlathakMap: a player is near, stage 1 -> 2, skill %d", m.Skill(1));
		Cast(game, unit, m.Skill(1), m.Mode(1), nullptr, ux, uy, 0);
		return;
	}
	if (i == last) {
		Counter(game, StageKey) = stage + 1;
		plog::Info("nihlathakMap: waves done, waiting 100");
		d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(AiSetDelay)(game, unit, 100);
		return;
	}
	if (i == last + 1) {
		++g_nihlNovas;
		plog::Info("nihlathakMap: casts skill %d (RathmaDeath) at (%d, %d) (%u novas)", m.Skill(2), ux, uy,
		           g_nihlNovas.load());
		Cast(game, unit, m.Skill(2), m.Mode(2), nullptr, ux, uy, 0);
		Counter(game, StageKey) = stage + 1;
		d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(AiSetDelay)(game, unit, 25);
		return;
	}
	if (i > last + 1) {
		++g_nihlDeaths;
		plog::Info("nihlathakMap: after the nova, it dies (%u deaths)", g_nihlDeaths.load());
		DropMendelnLoot(game, unit);
		KillQuietly(game, unit);
		return;
	}

	// A wave: counts of normal, champion and unique picks from the stage's aip row
	const int32_t row = NihlWaveRow(stage, a0, a1, a2);
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	const auto roll = [&](int col, int32_t floor) -> int32_t {
		const int32_t n = m.Aip(row, col);
		return n > floor ? static_cast<int32_t>(d2r::RandomBelow(seed, static_cast<uint32_t>(n))) : floor;
	};
	int32_t normal = row == 2 ? roll(0, 2) : roll(0, 1);
	const int32_t champions = row == 2 ? roll(1, 0) : roll(1, 1);
	const int32_t uniques = row == 4 ? roll(2, 1) : roll(2, 0);
	normal += normal + champions + uniques < 1 ? 1 : 0;
	int spawned = 0;
	for (int32_t k = 0; k < normal + champions + uniques; ++k) {
		const WaveKind kind = k < normal ? WaveKind::Normal : k < normal + champions ? WaveKind::Champion : WaveKind::Unique;
		spawned += SpawnWaveMonster(game, unit, ux, uy, seed, kind) ? 1 : 0;
	}
	g_nihlSpawned += spawned;
	// The pause: aip5's Normal column, Nightmare's past aip1's first column, Hell's past the first two
	const int32_t delay = m.Aip(5, i > a0 + a1 + 1 ? 2 : i > a0 + 1 ? 1 : 0);
	plog::Info("nihlathakMap: stage %d, wave of aip%d: %d normal, %d champion, %d unique, %d spawned (%u so far), "
	           "delay %d", stage, row, normal, champions, uniques, spawned, g_nihlSpawned.load(), delay);
	Counter(game, StageKey) = stage + 1;
	Idle(game, unit, delay);
	const uint32_t r = d2r::RandomBelow(seed, 3);
	if (r == 0) {
		Cast(game, unit, m.Skill(3), m.Mode(3), target, 0, 0, 0);
	} else if (r == 1) {
		Cast(game, unit, m.Skill(1), m.Mode(1), nullptr, ux + 2, uy + 2, 0);
	}
}
}  // namespace

auto RegisterNihlathakAi(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Resolve(WaveMakers, std::size(WaveMakers), context->exeBase, nullptr, 0);
	g_makers = true;
	for (sigscan::Entry* e : WaveMakers) {
		if (e->state != sigscan::State::Ok) {
			g_makers = false;
			plog::Warn("nihlathakMap: signature %s %s, wave champions and uniques spawn as normal monsters", e->name,
			           sigscan::StateText(e->state));
		}
	}
	plog::Info("nihlathakMap: champion and unique makers %s", g_makers ? "resolved" : "off");
	return ai_registry::RegisterAt(context, TrappedSoulRow, d2r::sig::AiTrappedSoul.address, NihlathakMap, &ThinkNihlathak);
}

auto NihlathakAiCounts(uint32_t out[5]) noexcept -> void {
	out[0] = g_nihlStages.load();
	out[1] = g_nihlSpawned.load();
	out[2] = g_nihlNovas.load();
	out[3] = g_nihlDeaths.load();
	out[4] = g_nihlDrops.load();
}
