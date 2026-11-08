// PD2's Overseer AI (ProjectDiablo.dll 0x1035dca0; docs/research/pd2-uber-bosses-dll.md, section 5) for Lucion (PD2
// 1112), LucionControl (1113) and LucionSpawner (1141): Lucion's stages and state machine, the portal phase, the
// controller's spawner waves and Royal Fire, the spawner's adds. Every other Overseer monster keeps D2R's think.
//
// The three register on monai row 120 through ai_hooks (no SDK service runs monster AI; see ai_hooks.cpp).
//
// PD2 keeps the fight in game fields (stage +0x262a, sub-step +0x262b) and Lucion's AI params; the port keeps them in
// D2R's per-game counters (rva::GameCounter), like map_open's tier. Lucion is one per game, so his params are too.
#include "lucion_ai.h"

#include "ai_hooks.h"
#include "level_fx.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// Merged monstats ids (PD2 1112, 1113, 1114, 1115, 1140, 1141)
constexpr int32_t Lucion = 1129, LucionControl = 1130, LucionSpawn = 1131, LucionSpawnRanged = 1132,
                  LucionSpawnTank = 1157, LucionSpawner = 1158;
constexpr int32_t StatCooldown1 = 499, StatCooldown2 = 500;  // mon_cooldown1/2 (PD2 446/447)
constexpr int32_t StateInvis = 146;                          // PD2's 0x92
constexpr int32_t ModeS3 = 10, ModeS4 = 11, ModeNeutral = 1, ModeA1 = 4, ModeA2 = 5;

constexpr uint32_t TierKey = 0x55425254;  // 'UBRT', as map_open.cpp
constexpr uint32_t StageKey = 0x55424c47;     // 'UBLG': PD2 game +0x262a
constexpr uint32_t SubStepKey = 0x55424c53;   // 'UBLS': PD2 game +0x262b
constexpr uint32_t StateKey = 0x55424c41;     // 'UBLA': Lucion's AI param 0, the next state
constexpr uint32_t ParamKey = 0x55424c42;     // 'UBLB': param 1
constexpr uint32_t LastKey = 0x55424c43;      // 'UBLC': param 2, the state just run
constexpr uint32_t ResistSavedKey = 0x55424c52;  // 'UBLR': the six resists below are saved
constexpr uint32_t ResistKeys[] = {0x55424c30, 0x55424c31, 0x55424c32, 0x55424c33, 0x55424c34, 0x55424c35};
// PD2 0x10380770 sets these to 200 (fire, cold, lightning, poison, magic, damage resist)
constexpr int32_t ImmuneStats[] = {39, 43, 41, 45, 37, 36};

// The arena (PD2's subtile coordinates, as Royal Fire's ring centre in skill_functions.cpp)
constexpr int32_t Corners[][2] = {{0x622d, 0x17eb}, {0x622d, 0x17d5}, {0x6217, 0x17d5}, {0x6217, 0x17eb}};
constexpr int32_t CentreX = 0x6223, CentreY = 0x17e1;

constexpr int32_t OverseerRow = 120;  // monai.txt

std::atomic<uint32_t> g_thinks {0};
std::atomic<uint32_t> g_stages {0};
std::atomic<uint32_t> g_casts {0};
std::atomic<uint32_t> g_spawns {0};

auto Counter(Game* game, uint32_t key) noexcept -> int32_t& {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto Stat(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

auto SetStat(Unit* unit, int32_t stat, int32_t value) noexcept -> void {
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::rva::SetUnitStat)(unit, stat, value, 0);
}

auto Rand(Unit* unit, uint32_t n) noexcept -> int32_t {
	return static_cast<int32_t>(
		d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit), n));
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room) : -1;
}

// The live monsters of a class in a level (PD2 walks the level's rooms; the port walks the game's monster lists)
auto CountInLevel(Game* game, int32_t classId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	int32_t n = 0;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* u = At<Unit*>(game, GameMonsters + bucket * 8); u != nullptr; u = At<Unit*>(u, UnitListNext)) {
			if (At<int32_t>(u, UnitClassId) == classId && d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(u) == 0 &&
			    LevelOf(u) == level) {
				++n;
			}
		}
	}
	return n;
}

// The engine's AI moves, as D2R's own thinks call them
auto Idle(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiIdle)(game, unit, frames);
}
auto Delay(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiSetDelay)(game, unit, frames);
}
auto Attack(Game* game, Unit* unit, int32_t mode, Unit* target) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t, Unit*) noexcept>(d2r::rva::AiAttack)(game, unit, mode, target);
}
auto Cast(Game* game, Unit* unit, int32_t skill, int32_t mode, Unit* target, int32_t x, int32_t y) noexcept -> void {
	if (skill < 0) {
		Idle(game, unit, 10);
		return;
	}
	const int32_t ok = d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(
		d2r::rva::AiUseSkill)(game, unit, static_cast<uint8_t>(mode), skill, target, x, y, 0);
	if (plog::Sample(g_casts)) {
		plog::Info("lucion ai: class %d cast skill %d mode %d at (%d, %d)%s -> %d (%u so far)",
		           At<int32_t>(unit, d2r::rva::UnitClassId), skill, mode, x, y, target != nullptr ? " on target" : "", ok,
		           g_casts.load());
	}
}
// PD2 sets the mode itself (D2Common); the port sends it the way AiIdle sends neutral
auto SetMode(Game* game, Unit* unit, int32_t mode) noexcept -> void {
	alignas(8) uint8_t cmd[0x40] {};
	d2r::Engine<void (*)(Unit*, int32_t, uint8_t*) noexcept>(d2r::rva::ModeCmdInit)(unit, mode, cmd);
	std::memcpy(cmd + d2r::rva::ModeCmdUnit, &unit, sizeof(unit));
	d2r::Engine<int32_t (*)(Game*, uint8_t*, int32_t) noexcept>(d2r::rva::ModeCmdRun)(game, cmd, 1);
}

// PD2 0x10380770: immune sets the six resists to 200 and strips poison and open wounds; off puts back what they
// were (PD2 reads monstats). setState is PD2's third argument: state 8 only then
auto SetImmune(Game* game, Unit* unit, bool on, bool setState) noexcept -> void {
	int32_t& saved = Counter(game, ResistSavedKey);
	if (on) {
		const bool first = saved == 0;
		if (first) {
			for (size_t i = 0; i < std::size(ImmuneStats); ++i) {
				Counter(game, ResistKeys[i]) = Stat(unit, ImmuneStats[i]);
			}
			saved = 1;
		}
		for (const int32_t s : ImmuneStats) {
			SetStat(unit, s, 200);
		}
		const int stripped = ImmunityStates(game, unit, true, setState);
		if (first || stripped > 0) {
			plog::Info("lucion ai: immune, state 8 %s, %d poison/open wounds lists stripped", setState ? "set" : "not set",
			           stripped);
		}
	} else if (saved != 0) {
		for (size_t i = 0; i < std::size(ImmuneStats); ++i) {
			SetStat(unit, ImmuneStats[i], Counter(game, ResistKeys[i]));
		}
		ImmunityStates(game, unit, false, setState);
		saved = 0;
	}
}

auto Spawn(Game* game, Unit* near, int32_t classId, int32_t x, int32_t y) noexcept -> Unit* {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(
		d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(near), x, y);
	if (room == nullptr) {
		return nullptr;
	}
	d2r::Engine<void (*)(void*, int32_t, int32_t, int32_t, int32_t) noexcept>(ClearSpawnSpot)(room, x, y, 1, 0x100);
	Unit* spawn = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster)(game, room, x, y, classId, ModeNeutral, 1, 0);
	if (spawn != nullptr) {
		*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(spawn) + UnitMainFlags) |= UnitNoTreasure | UnitNoExperience;
		++g_spawns;
	}
	return spawn;
}

// PD2 0x10380950: on gives the unit a list with state 146 and light radius -100 (list flags 8, as PD2's); off frees it.
// PD2 also sets the list's free callback (0x10312350) and calls a D2Common thunk (0x1030bd80) not identified.
auto SetShroud(Game* game, Unit* unit, bool on) noexcept -> void {
	using namespace d2r::rva;
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, StateInvis);
	if (on && list == nullptr) {
		const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(unit, __FILE__, __LINE__);
		list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(
			8, 0, At<int32_t>(unit, UnitType), id);
		if (list == nullptr) {
			return;
		}
		d2r::Engine<void (*)(void*, int32_t) noexcept>(SetListState)(list, StateInvis);
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(
			d2r::DataFlag(game), list, StatLightRadius, -100, 0);
		d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(unit, list, 1);
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, StateInvis, 1);
	} else if (!on && list != nullptr) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(ClearUnitState)(unit, StateInvis, 0);
		d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(unit, list);
		d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(d2r::DataFlag(game), list);
	} else {
		return;
	}
	plog::Info("lucion ai: shroud %s, light radius %d", on ? "on" : "off", Stat(unit, StatLightRadius));
}

constexpr int32_t SoundFrenzyStart = 0xa78, SoundFrenzyEnd = 0x26d;  // sounds.txt shrine_storm, bloodraven_mist_end

struct Mon {
	const uint8_t* rec;
	uint8_t diff;
	auto Skill(int i) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonSkills + 2 * i); }
	auto Mode(int i) const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonSkillModes + i); }
	// PD2 reads aip1-8 as one int16 list (aip1, aip1(N), aip1(H), aip2, ...): its p0 at 1.13c +0x56
	auto P(int i) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonAiParams + 2 * i); }
	auto AiDel() const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonAiDel + diff); }
};

auto LifePercent(Unit* unit) noexcept -> int32_t {
	const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
	return max > 0 ? static_cast<int32_t>(static_cast<int64_t>(Stat(unit, d2r::rva::StatLife)) * 100 / max) : 100;
}

// PD2's stage thresholds: stage n+1 once life is below these, each tier adding the second number
constexpr int32_t StageLife[][2] = {{85, 5}, {80, 5}, {75, 4}, {70, 4}, {55, 3}, {35, 3}};
constexpr auto NextStage(int32_t stage, int32_t life, int32_t tier) noexcept -> bool {
	for (int i = 0; i < 6; ++i) {
		if (stage < i + 2 && life < StageLife[i][0] + StageLife[i][1] * tier) {
			return true;
		}
	}
	return false;
}
static_assert(NextStage(1, 84, 0) && !NextStage(1, 85, 0) && NextStage(6, 36, 1) && !NextStage(7, 1, 0));

auto ThinkLucion(Game* game, Unit* unit, uint8_t* tick, const Mon& m) noexcept -> void {
	Unit* target = At<Unit*>(tick, d2r::rva::AiTickTarget);
	const int32_t dist = At<int32_t>(tick, d2r::rva::AiTickDistance);
	const int32_t dist2 = target != nullptr ? dist * dist : 0x7fffffff;  // PD2: squared distance to the nearest player
	// The idle window for a boss left alone (PD2 game+0x26fe): idle and immune, counting down while a player is near
	if (IdleWindow(game, unit, target != nullptr)) {
		SetImmune(game, unit, true, true);  // PD2 0x35de6d pushes 1
		Idle(game, unit, 10);
		return;
	}
	const int32_t tier = Counter(game, TierKey);
	const int32_t aidel = m.AiDel() - tier;
	int32_t& stage = Counter(game, StageKey);
	int32_t& sub = Counter(game, SubStepKey);
	int32_t& stateRef = Counter(game, StateKey);
	int32_t& paramRef = Counter(game, ParamKey);
	const int32_t state = stateRef;
	const int32_t param = paramRef;
	const int32_t last = Counter(game, LastKey);
	if (plog::Sample(g_thinks)) {
		plog::Info("lucion: unit %u thinks (%u so far), state %d, stage %d, life %d%%", At<uint32_t>(unit, d2r::rva::UnitIdField),
		           g_thinks.load(), state, stage, LifePercent(unit));
	}
	const auto store = [&](int32_t next, int32_t p) {
		if (next != state && (next == 4 || next == 5 || next == 6 || next == 7 || next == 9 || next == 12)) {
			plog::Info("lucion: state %d -> %d (stage %d)", state, next, stage);
		}
		stateRef = next;
		paramRef = p;
		Counter(game, LastKey) = state;
	};

	const int32_t before = stage;
	if (NextStage(stage, LifePercent(unit), tier)) {
		++stage;
		++g_stages;
		plog::Info("lucion: stage %d -> %d at %d%% life, tier %d", before, stage, LifePercent(unit), tier);
	}
	if (sub != 0 && state != 5) {
		--sub;
	}
	const int32_t cd1 = Stat(unit, StatCooldown1);
	if (cd1 > 0 && state != 7 && state != 8) {
		SetStat(unit, StatCooldown1, cd1 - 1);
	}
	int32_t tx = 0, ty = 0;
	if (target != nullptr) {
		d2r::UnitXY(target, &tx, &ty);
	}
	// The portal phase: stage 3 reached this tick
	const auto portalPhase = [&]() {
		SetImmune(game, unit, true, false);  // PD2 0x35e30e pushes 0: no state 8
		SetMode(game, unit, ModeS3);
		Delay(game, unit, 25);
		if (stage == 3 && before < 3) {
			for (const auto& c : Corners) {
				Spawn(game, unit, LucionSpawner, c[0], c[1]);
			}
		}
	};
	if (stage == 3 && before < 3) {
		portalPhase();
		store(4, 1);
		return;
	}
	const auto blinkAtTarget = [&]() {
		Cast(game, unit, m.Skill(0), m.Mode(0), nullptr, tx - 1 + Rand(unit, 2), ty - 1 + Rand(unit, 2));
		store(1, 0);
		Counter(game, LastKey) = 10;
	};
	if (state == 1 && target != nullptr && (dist2 > 700 || (dist2 > 500 && Rand(unit, 100) < 20))) {
		blinkAtTarget();
		return;
	}
	switch (state) {
	case 0:
		Idle(game, unit, 40);
		if (stage < 1) {
			stage = 1;
		}
		store(6, 0);
		return;
	case 1: {
		SetImmune(game, unit, false, true);
		const int32_t r = Rand(unit, 100);
		int32_t next = 1;
		if (r < m.P(0)) {
			const int32_t r2 = Rand(unit, 100);
			if (At<int32_t>(tick, d2r::rva::AiTickInMelee) != 0 && target != nullptr) {
				Attack(game, unit, ModeA1, target);  // PD2 0x10356ea0: a melee swing; the state stays 1
				return;
			}
			if (dist2 < 100 && r2 < 30 && target != nullptr) {
				d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t) noexcept>(d2r::rva::AiApproach)(game, unit, target, 0);
				Delay(game, unit, 15);
				store(1, 0);
				return;
			}
			next = 9;
		} else if (r < m.P(1)) {
			next = 12;
		} else {
			// After Nova (8) no Nova again; after the frenzy's end (6) no frenzy again
			if (last != 8 && r < m.P(2) && stage >= 2 && cd1 <= 0) {
				next = 7;
			} else if (last != 6 && r < m.P(3) && stage >= 3) {
				next = sub != 0 ? 3 : 4;
			} else if (stage < 2) {
				next = r < m.P(2) ? 9 : 3;
			} else {
				next = 3;
			}
		}
		Idle(game, unit, aidel);
		store(next, 0);
		return;
	}
	case 2:
	case 11:
		Idle(game, unit, aidel);
		store(1, 0);
		return;
	case 3:
		if (target != nullptr) {
			Attack(game, unit, ModeA2, target);
		} else {
			Idle(game, unit, aidel);
		}
		store(1, 0);
		return;
	case 4:
		if (param == 0) {
			portalPhase();
			store(4, 1);
		} else if (param == 1) {
			// PD2: the shroud (0x10380950), the hide (0x103a4c70), then the darkness and its sound for the level
			SetShroud(game, unit, true);
			d2r::Engine<void (*)(Game*, Unit*) noexcept>(d2r::rva::AiHide)(game, unit);
			sub = 0;
			Delay(game, unit, aidel);
			SetDarkness(game, unit, true);
			PlayLevelSound(game, LevelOf(unit), SoundFrenzyStart);
			store(5, Rand(unit, 6) + 6);
		} else {
			store(1, 0);
		}
		return;
	case 5:
		if (param > 0) {
			int32_t p = param;
			if (sub == 0) {
				--p;
				if (Rand(unit, 100) < 60 || target == nullptr) {
					target != nullptr ? Attack(game, unit, ModeA1, target) : Idle(game, unit, aidel);
				} else {
					sub = 1;
					Cast(game, unit, m.Skill(3), m.Mode(3), target, tx, ty);
				}
			} else if (sub < 3) {
				++sub;
				Cast(game, unit, m.Skill(3), m.Mode(3), target, tx, ty);
			} else {
				--p;
				sub = 0;
				target != nullptr ? Attack(game, unit, ModeA1, target) : Idle(game, unit, aidel);
			}
			store(5, p);
		} else {
			sub = At<uint8_t>(m.rec, d2r::rva::MonAiParams + 2 * 6);  // PD2 reads aip3's low byte
			target != nullptr ? Attack(game, unit, ModeA1, target) : Idle(game, unit, aidel);
			store(6, 0);
		}
		return;
	case 6:
		if (param == 0) {
			SetDarkness(game, unit, false);
			PlayLevelSound(game, LevelOf(unit), SoundFrenzyEnd);
			Idle(game, unit, aidel);
			store(6, 1);
		} else if (param == 1) {
			// PD2: the shroud off, the unhide (0x103a4c90; its result unread), mode 11
			SetShroud(game, unit, false);
			const int32_t shown = d2r::Engine<int32_t (*)(Game*, Unit*) noexcept>(d2r::rva::AiUnhide)(game, unit);
			plog::Info("lucion ai: unhide -> %d", shown);
			SetMode(game, unit, ModeS4);
			Delay(game, unit, 10);
			store(6, 2);
		} else {
			SetImmune(game, unit, false, true);
			Idle(game, unit, aidel);
			store(1, 0);
		}
		return;
	case 7:
		if (param == 0) {
			Cast(game, unit, m.Skill(0), m.Mode(0), nullptr, CentreX, CentreY);
			Delay(game, unit, 35 - 4 * tier);
			store(7, 1);
		} else if (param == 1) {
			Cast(game, unit, m.Skill(1), m.Mode(1), nullptr, 0, 0);
			Delay(game, unit, 65 - 8 * tier);
			store(7, Rand(unit, 10000) + 1);
		} else {
			Cast(game, unit, m.Skill(1), 9, nullptr, 0, 0);
			store(8, param);
		}
		return;
	case 8:
		Idle(game, unit, aidel);
		SetStat(unit, StatCooldown1, Rand(unit, static_cast<uint32_t>(2 * (3 - tier))) - stage / 2 + m.P(7));
		store(1, 0);
		return;
	case 9:
		Cast(game, unit, m.Skill(2), m.Mode(2), nullptr, tx, ty);
		store(1, 0);
		return;
	case 10:
		if (target != nullptr) {
			blinkAtTarget();
		} else {
			Idle(game, unit, aidel);
			store(1, 0);
		}
		return;
	case 12: {
		int32_t ux = 0, uy = 0;
		d2r::UnitXY(unit, &ux, &uy);
		if (target == nullptr) {
			Idle(game, unit, aidel);
		} else {
			const int32_t dx = tx - ux, dy = ty - uy;
			Cast(game, unit, m.Skill(4), m.Mode(4), nullptr, ux + (dx > 0 ? dx + 3 : dx - 3),
			     uy + (dy > 0 ? dy + 3 : dy - 3));
		}
		store(1, 0);
		return;
	}
	default:
		Idle(game, unit, aidel);
		store(1, 0);
		return;
	}
}

// LucionControl: from stage 3 it spawns LucionSpawners in the arena while Lucion is in state 5; from stage 4 it casts
// Royal Fire on a timer
auto ThinkControl(Game* game, Unit* unit, uint8_t* tick, const Mon& m) noexcept -> void {
	if (IdleWindow(game, nullptr, false)) {
		Idle(game, unit, 10);
		return;
	}
	const int32_t stage = Counter(game, StageKey);
	const int32_t tier = Counter(game, TierKey);
	const int32_t level = LevelOf(unit);
	const bool frenzy = Counter(game, StateKey) == 5;
	int32_t aidel = m.AiDel();
	const int32_t r = Rand(unit, 100);
	if (stage >= 3) {
		int32_t base = m.P(1);
		if (frenzy) {
			aidel >>= 1;
			base -= 3;
		}
		const int32_t cd = Stat(unit, StatCooldown1);
		if (cd < 1 && frenzy) {
			const int32_t n = CountInLevel(game, LucionSpawner, level);
			if (n < m.P(2) && r < (tier - m.P(0)) * n + 100) {
				const int32_t x = Rand(unit, 0x2c) + 0x620d, y = Rand(unit, 0x2c) + 0x17cb;
				if (Spawn(game, unit, LucionSpawner, x, y) != nullptr) {
					SetStat(unit, StatCooldown1, r % (3 - tier) - stage / 2 + base);
				}
			}
		} else {
			SetStat(unit, StatCooldown1, cd - 1);
		}
	}
	if (stage > 3) {
		const int32_t base = m.P(3) - tier + (frenzy ? 2 : 0);
		const int32_t cd = Stat(unit, StatCooldown2);
		if (cd < 1) {
			Cast(game, unit, m.Skill(0), m.Mode(0), At<Unit*>(tick, d2r::rva::AiTickTarget), 0, 0);
			SetStat(unit, StatCooldown2, r % (3 - tier) - stage / 2 + base);
			return;
		}
		SetStat(unit, StatCooldown2, cd - 1);
	}
	Idle(game, unit, aidel);
}

// LucionSpawner: from stage 3 it spawns an add at itself on a timer, by the aip odds and caps
auto ThinkSpawner(Game* game, Unit* unit, const Mon& m) noexcept -> void {
	if (IdleWindow(game, nullptr, false)) {
		Idle(game, unit, 10);
		return;
	}
	const int32_t stage = Counter(game, StageKey);
	const int32_t tier = Counter(game, TierKey);
	const bool frenzy = Counter(game, StateKey) == 5;
	int32_t aidel = m.AiDel();
	int32_t base = m.P(6);
	if (frenzy) {
		base >>= 1;
		aidel -= 1;
	}
	const int32_t r0 = Rand(unit, 100);
	if (stage > 2) {
		const int32_t cd = Stat(unit, StatCooldown1);
		if (cd < 1) {
			const int32_t level = LevelOf(unit);
			const int32_t melee = CountInLevel(game, LucionSpawn, level);
			const int32_t ranged = CountInLevel(game, LucionSpawnRanged, level);
			const int32_t tanks = CountInLevel(game, LucionSpawnTank, level);
			const int32_t r = Rand(unit, 100);
			const int32_t rangedCap = m.P(4) - stage / 3;
			int32_t pick = melee < m.P(3) - stage / 2 ? LucionSpawn : -1;
			if (stage == 5 && r < m.P(0) && ranged < rangedCap) {
				pick = LucionSpawnRanged;
			} else if (stage >= 6) {
				if (r < m.P(0) && ranged < rangedCap) {
					pick = LucionSpawnRanged;
				} else if (r < m.P(1) && tanks < m.P(5)) {
					pick = LucionSpawnTank;
				}
			}
			int32_t x = 0, y = 0;
			if (pick >= 0 && d2r::UnitXY(unit, &x, &y) && Spawn(game, unit, pick, x, y) != nullptr) {
				SetStat(unit, StatCooldown1, r0 % (3 - tier) - stage / 2 + base);
			}
		} else {
			SetStat(unit, StatCooldown1, cd - 1);
		}
	}
	Idle(game, unit, aidel);
}

// The monstats record and difficulty from the tick; null when there is none (then D2R's think runs)
auto MonOf(uint8_t* tick, Mon* m) noexcept -> bool {
	m->rec = tick != nullptr ? At<const uint8_t*>(tick, d2r::rva::AiTickMonStats) : nullptr;
	m->diff = m->rec != nullptr ? static_cast<uint8_t>(At<uint8_t>(tick, d2r::rva::AiTickDifficulty) % 3) : 0;
	return m->rec != nullptr;
}

template <auto Port>
void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	Mon m {};
	if (!MonOf(tick, &m)) {
		VanillaAiThink(OverseerRow)(game, unit, tick);
		return;
	}
	Port(game, unit, tick, m);
}

void SpawnerThink(Game* game, Unit* unit, uint8_t* tick, const Mon& m) noexcept {
	(void)tick;
	ThinkSpawner(game, unit, m);
}

}  // namespace

auto InstallLucionAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using d2r::rva::AiOverseer;
	const bool ok = RegisterAiThink(context, OverseerRow, AiOverseer, Lucion, &Think<ThinkLucion>) &&
	                RegisterAiThink(context, OverseerRow, AiOverseer, LucionControl, &Think<ThinkControl>) &&
	                RegisterAiThink(context, OverseerRow, AiOverseer, LucionSpawner, &Think<SpawnerThink>);
	std::snprintf(report, reportSize, ok ? "monai %d (Overseer) think: Lucion, LucionControl, LucionSpawner"
	                                     : "off: monai %d think not swapped",
	              OverseerRow);
	return ok ? 1 : 0;
}

auto LucionAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "lucion ai: %u thinks, %u stages, %u casts, %u spawns", g_thinks.load(), g_stages.load(),
	              g_casts.load(), g_spawns.load());
}
