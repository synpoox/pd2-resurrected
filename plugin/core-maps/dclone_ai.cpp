// PD2's Dclone AI (ProjectDiablo.dll 0x1035b3f0; docs/research/pd2-uber-bosses-dll.md, section 5) for uberdiablonew:
// the heal when a player leaves, the immunity while his adds live, the three cold-nova phases at the arena centre, the
// three skeleton waves (UberDiabSummon, srvdo 177) and his attack picks. Every other Trap-Melee monster keeps D2R's think.
// The trapped souls' AI (0x1035bc60, monai row 99, #472) is here too: they cast while Dclone lives and die with him.
// That PD2 function's nihlathakMap branch (map boss 0x39a, #538) is here too: stages of mob waves, then RathmaDeath.
//
// Dclone registers on monai row 87 through ai_hooks (no SDK service runs monster AI; see ai_hooks.cpp). PD2 keeps the
// player count in a per-unit map and the last pick in the AI control; Dclone is one per game, so the port keeps both in
// D2R's per-game counters (rva::GameCounter), as lucion_ai.cpp does. PD2 reads the Hell column of every aip, whatever the
// difficulty; so does the port.
#include "dclone_ai.h"

#include "ai_hooks.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t Dclone = 806;                         // uberdiablonew (PD2 789)
constexpr int32_t FirstAdd = 903, LastAdd = 909;        // dcloneskele .. dclonebloodlord (PD2 886-892)
constexpr int32_t StatCooldown1 = 499, StatPhase = 500;  // mon_cooldown1/2 (PD2 446/447): the nova count, the phase
constexpr int32_t StatColdMastery = 331, StatColdPierce = 305;
constexpr int32_t StateResistAll = 8;  // PD2 0x10380770 sets it with the resists
constexpr int32_t ImmuneStats[] = {39, 43, 41, 45, 37, 36};  // fire, cold, lightning, poison, magic, damage resist
constexpr int32_t CentreX = 15129, CentreY = 6548;          // the arena centre (PD2 0x3b19, 0x1994)
constexpr int32_t FarPlayer = 0x17c;                         // squared distance

constexpr uint32_t TierKey = 0x55425254;     // 'UBRT', as map_open.cpp
constexpr uint32_t PlayersKey = 0x55424450;  // 'UBDP': the players in his level last tick
constexpr uint32_t LastKey = 0x5542444c;     // 'UBDL': the last pick (0 cast or idle, 1 slot 4, 2 slot 7)
constexpr uint32_t ImmuneKey = 0x55424449;   // 'UBDI': 1 while immune (the six resists below are saved)
constexpr uint32_t ResistKeys[] = {0x55424430, 0x55424431, 0x55424432, 0x55424433, 0x55424434, 0x55424435};

constexpr int32_t TrapMeleeRow = 87, TrappedSoulRow = 99;  // monai.txt
constexpr int32_t FirstSoul = 807, LastSoul = 811;           // ubertrappedsoul1-5 (PD2 790-794)
constexpr int32_t ModeDead = 12;                             // monmode DD

// The souls' cast delay: aidel + 10 at tier 0, else aidel - 4 x (tier - 1), at least 20 (PD2 0x1035c186)
constexpr auto SoulDelay(int32_t aidel, int32_t tier) noexcept -> int32_t {
	return tier == 0 ? aidel + 10 : (aidel - 4 * tier + 4 > 20 ? aidel - 4 * tier + 4 : 20);
}
// The aimed souls' (aip8 2 or 3) spread around the target, 0 for none
constexpr auto SoulSpread(int32_t tier) noexcept -> int32_t {
	return tier < 3 ? 24 - 8 * tier : 0;
}
static_assert(SoulDelay(40, 0) == 50 && SoulDelay(40, 1) == 40 && SoulDelay(40, 2) == 36 && SoulDelay(22, 2) == 20);
static_assert(SoulSpread(0) == 24 && SoulSpread(1) == 16 && SoulSpread(2) == 8);

std::atomic<uint32_t> g_soulCasts {0};
std::atomic<uint32_t> g_soulCastsBy[LastSoul - FirstSoul + 1][3] {};
std::atomic<uint32_t> g_soulDeaths {0};
std::atomic<uint32_t> g_thinks {0};
std::atomic<uint32_t> g_phases {0};
std::atomic<uint32_t> g_novas {0};
std::atomic<uint32_t> g_waves {0};
std::atomic<uint32_t> g_heals {0};
std::atomic<uint32_t> g_casts {0};

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

auto Rand100(Unit* unit) noexcept -> int32_t {
	return static_cast<int32_t>(
		d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit), 100));
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room) : -1;
}

auto Dead(Unit* unit) noexcept -> bool {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitDead)(unit) != 0;
}

auto Dist2(Unit* unit, int32_t x, int32_t y) noexcept -> int32_t {
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);
	return (ux - x) * (ux - x) + (uy - y) * (uy - y);
}

auto Idle(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiIdle)(game, unit, frames);
}

// PD2 0x103a1880 then 0x103a1910: cast, then think again after `frames`
auto Cast(Game* game, Unit* unit, int32_t skill, int32_t mode, Unit* target, int32_t x, int32_t y,
          int32_t frames) noexcept -> int32_t {
	const int32_t ok = d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(
		d2r::rva::AiUseSkill)(game, unit, static_cast<uint8_t>(mode), skill, target, x, y, 0);
	if (plog::Sample(g_casts)) {
		plog::Info("dclone ai: cast skill %d mode %d at (%d, %d)%s -> %d (%u so far)", skill, mode, x, y,
		           target != nullptr ? " on target" : "", ok, g_casts.load());
	}
	if (frames > 0) {  // 0: PD2 casts without 0x103a1910
		d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiSetDelay)(game, unit, frames);
	}
	return ok;
}

// PD2 0x10380770: on sets the six resists to 200 and state 8; off puts back what they were (PD2 reads monstats)
auto SetImmune(Game* game, Unit* unit, bool on) noexcept -> void {
	int32_t& immune = Counter(game, ImmuneKey);
	if (on == (immune != 0)) {
		return;
	}
	if (on) {
		for (size_t i = 0; i < std::size(ImmuneStats); ++i) {
			Counter(game, ResistKeys[i]) = Stat(unit, ImmuneStats[i]);
			SetStat(unit, ImmuneStats[i], 200);
		}
	} else {
		for (size_t i = 0; i < std::size(ImmuneStats); ++i) {
			SetStat(unit, ImmuneStats[i], Counter(game, ResistKeys[i]));
		}
	}
	// A bare state, set as D2R's 0x56bfe0: SetStateBit is the bit UnitHasState reads (#623)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetStateBit)(unit, StateResistAll, on ? 1 : 0);
	if (on) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetStateGfxFlag)(unit, StateResistAll, 1);
	}
	immune = on ? 1 : 0;
	plog::Info("dclone: immune %s", on ? "on" : "off");
}

// PD2 0x10356e00, as lucion.cpp's KillQuietly: no drop, then the death mode
auto KillQuietly(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(monster, UnitNoDrop, 1);
	alignas(8) uint8_t block[0x30] = {};
	d2r::Engine<void (*)(Unit*, int32_t, void*) noexcept>(MonsterModeBlock)(monster, ModeDeath, block);
	*reinterpret_cast<void**>(block + 0x10) = d2r::Engine<void* (*)(Game*, Unit*) noexcept>(UnitTarget)(game, monster);
	d2r::Engine<uint64_t (*)(Game*, void*, int32_t) noexcept>(SetMonsterMode)(game, block, 1);
}

// The live monsters in a level: any add alive, and the ones (not Dclone) standing on the centre spot
auto ScanMonsters(Game* game, Unit* self, int32_t level, Unit** onSpot, int max, int* spotCount) noexcept -> bool {
	using namespace d2r::rva;
	bool adds = false;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* u = At<Unit*>(game, GameMonsters + bucket * 8); u != nullptr; u = At<Unit*>(u, UnitListNext)) {
			if (Dead(u) || LevelOf(u) != level) {
				continue;
			}
			const int32_t classId = At<int32_t>(u, UnitClassId);
			adds |= classId >= FirstAdd && classId <= LastAdd;
			if (onSpot != nullptr && u != self && *spotCount < max && Dist2(u, CentreX, CentreY) <= 10) {
				onSpot[(*spotCount)++] = u;
			}
		}
	}
	return adds;
}

struct Mon {
	const uint8_t* rec;
	auto Skill(int slot) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonSkills + 2 * (slot - 1)); }
	auto Mode(int slot) const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonSkillModes + slot - 1); }
	// aip1-8 are int16[8][3]; PD2 reads the Hell column
	auto Aip(int n) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonAiParams + 2 * (3 * (n - 1) + 2)); }
	// One difficulty column (0 Normal, 1 Nightmare, 2 Hell), unsigned as PD2 reads it
	auto Aip(int n, int col) const noexcept -> int32_t {
		return At<uint16_t>(rec, d2r::rva::MonAiParams + 2 * (3 * (n - 1) + col));
	}
	auto AiDel() const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonAiDel + 2); }
};

auto LifePercent(Unit* unit) noexcept -> int32_t {
	const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
	return max > 0 ? static_cast<int32_t>(static_cast<int64_t>(Stat(unit, d2r::rva::StatLife)) * 100 / max) : 100;
}

// The nova phases start at stat 447 = 2, 4, 6 below these life percents (PD2: 100 - phase / 2 * 25)
constexpr auto NovaPhase(int32_t phase, int32_t life) noexcept -> bool {
	return (phase == 2 || phase == 4 || phase == 6) && life < 100 - phase / 2 * 25;
}
static_assert(NovaPhase(2, 74) && !NovaPhase(2, 75) && NovaPhase(6, 24) && !NovaPhase(3, 1) && !NovaPhase(4, 50));

auto ThinkDclone(Game* game, Unit* unit, uint8_t* tick, const Mon& m) noexcept -> void {
	const int32_t tier = Counter(game, TierKey);
	const int32_t level = LevelOf(unit);
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);

	// The players in his level: how many, the nearest, and the farthest one's squared distance
	int32_t players = 0, farthest = 0, nearestD = 0x7fffffff;
	Unit* nearest = nullptr;
	for (auto* client = At<uint8_t*>(game, d2r::rva::GameClients); client != nullptr;
	     client = At<uint8_t*>(client, d2r::rva::ClientNext)) {
		Unit* p = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(d2r::rva::ClientUnit)(client, 0);
		if (p == nullptr || Dead(p) || LevelOf(p) != level) {
			continue;
		}
		++players;
		const int32_t d = Dist2(p, ux, uy);
		farthest = d > farthest ? d : farthest;
		if (d < nearestD) {
			nearestD = d;
			nearest = p;
		}
	}
	// The idle window for a boss left alone (PD2 game+0x26fe): idle and immune, counting down while a player is here
	if (IdleWindow(game, unit, nearest != nullptr)) {
		Idle(game, unit, 10);
		SetImmune(game, unit, true);
		return;
	}
	const bool adds = ScanMonsters(game, unit, level, nullptr, 0, nullptr);
	const int32_t delay = m.AiDel() - 8 * tier + 8 > 25 ? m.AiDel() - 8 * tier + 8 : 25;

	// A player left or died: heal 20% of max life
	int32_t& lastPlayers = Counter(game, PlayersKey);
	if (players > 0) {
		if (lastPlayers > players) {
			const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
			const int32_t life = Stat(unit, d2r::rva::StatLife) + max / 100 * 20;
			SetStat(unit, d2r::rva::StatLife, life < max ? life : max);
			++g_heals;
			plog::Info("dclone: players %d -> %d, healed to %d%%", lastPlayers, players, LifePercent(unit));
		}
		lastPlayers = players;
	}

	Unit* target = At<Unit*>(tick, d2r::rva::AiTickTarget);
	if (target == nullptr) {
		if (nearest == nullptr) {
			VanillaAiThink(TrapMeleeRow)(game, unit, tick);  // PD2 0x103187c0, as for a row with no Hell aip3
			SetImmune(game, unit, true);
			return;
		}
		target = nearest;
	}

	int32_t phase = Stat(unit, StatPhase);
	const int32_t life = LifePercent(unit);
	if (plog::Sample(g_thinks)) {
		plog::Info("dclone: thinks (%u so far), phase %d, life %d%%, %d players, adds %d", g_thinks.load(), phase, life,
		           players, adds ? 1 : 0);
	}
	if (NovaPhase(phase, life)) {
		const int32_t pct = 100 - phase / 2 * 25;
		const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
		SetStat(unit, d2r::rva::StatLife, static_cast<int32_t>(static_cast<int64_t>(max) * pct / 100));
		SetStat(unit, StatCooldown1, m.Aip(5));
		SetStat(unit, StatPhase, ++phase);
		++g_phases;
		plog::Info("dclone: phase %d -> %d at %d%% life: life set to %d%%, %d novas", phase - 1, phase, life, pct,
		           m.Aip(5) - 1);
	}

	const int32_t novas = Stat(unit, StatCooldown1);
	// PD2 aims the path at the centre (0x103331e0) before the run: UberDiabRun's start (srvst 71) keeps that point
	const auto runToCentre = [&]() {
		plog::Info("dclone: run to the centre, %d away (squared)", Dist2(unit, CentreX, CentreY));
		d2r::Engine<void (*)(void*, int32_t, int32_t) noexcept>(d2r::rva::PathSetTargetXY)(
			At<void*>(unit, d2r::rva::UnitPath), CentreX, CentreY);
		Cast(game, unit, m.Skill(7), m.Mode(7), nullptr, CentreX, CentreY, 50);
		SetImmune(game, unit, true);
	};
	if (novas == m.Aip(5)) {
		SetStat(unit, StatCooldown1, novas - 1);
		runToCentre();
		return;
	}
	if (novas > 0) {
		const int32_t d = Dist2(unit, CentreX, CentreY);
		if (d > 30) {
			runToCentre();
			return;
		}
		if (d > 2) {
			// Kill what stands on the centre spot, then teleport onto it
			Unit* onSpot[16] = {};
			int count = 0;
			ScanMonsters(game, unit, level, onSpot, 16, &count);
			for (int i = 0; i < count; ++i) {
				KillQuietly(game, onSpot[i]);
			}
			plog::Info("dclone: teleport onto the centre, %d away (squared), %d on the spot killed", d, count);
			Cast(game, unit, m.Skill(8), m.Mode(8), nullptr, CentreX, CentreY, 50);
			return;
		}
		const int32_t cold = (m.Aip(5) - novas + 1) * m.Aip(6);
		SetStat(unit, StatColdMastery, cold);
		SetStat(unit, StatColdPierce, cold);
		SetStat(unit, StatCooldown1, novas - 1);
		++g_novas;
		plog::Info("dclone: nova %d of %d (phase %d), cold mastery and pierce %d", m.Aip(5) - novas, m.Aip(5) - 1,
		           phase, cold);
		Cast(game, unit, m.Skill(5), m.Mode(5), target, 0, 0, delay);
		SetImmune(game, unit, true);
		return;
	}

	SetStat(unit, StatColdMastery, 0);
	SetStat(unit, StatColdPierce, 0);
	int32_t& last = Counter(game, LastKey);
	int32_t runBonus = 8 * tier, idleBonus = -8 * tier, pickBonus = last == 1 ? 10 : 0;
	bool immune = phase == 0 || adds;
	if (farthest > FarPlayer) {
		runBonus += 25;
		immune = true;
		pickBonus = -20;
	} else if (farthest < 16) {
		runBonus = -15;
	}
	SetImmune(game, unit, immune);
	if (phase == 0) {
		SetStat(unit, StatCooldown1, 0);
		SetStat(unit, StatPhase, 1);
		plog::Info("dclone: phase 0 -> 1");
		Idle(game, unit, 50);
		return;
	}

	// The skeleton waves at phases 1, 3 and 5, below Param4-6 of slot 6's skill
	const auto* summon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::GetSkillRecord)(
		d2r::DataFlag(game), m.Skill(6));
	if (summon != nullptr && ((phase == 1 && life < At<int32_t>(summon, d2r::rva::SkParam4)) ||
	                          (phase == 3 && life < At<int32_t>(summon, d2r::rva::SkParam5)) ||
	                          (phase == 5 && life < At<int32_t>(summon, d2r::rva::SkParam6)))) {
		SetStat(unit, StatPhase, phase + 1);
		++g_waves;
		plog::Info("dclone: phase %d -> %d at %d%% life: skeleton wave %d", phase, phase + 1, life, phase / 2 + 1);
		Cast(game, unit, m.Skill(6), m.Mode(6), target, 0, 0, delay);
		last = 0;
		return;
	}

	if (last != 2 && Rand100(unit) < m.Aip(8) + runBonus) {
		Cast(game, unit, m.Skill(7), m.Mode(7), target, 0, 0, delay);
		last = 2;
		return;
	}
	if (Rand100(unit) + idleBonus < m.Aip(7)) {
		Idle(game, unit, delay);
		last = 0;
		return;
	}
	const int32_t r = Rand100(unit) + pickBonus;
	const int32_t a1 = m.Aip(1), a2 = a1 + m.Aip(2), a3 = a2 + m.Aip(3);
	int slot = 4;
	if (r < a1) {
		slot = 1;
	} else if (r < a2) {
		slot = 3;
	} else if (r < a3 && At<int32_t>(tick, d2r::rva::AiTickInMelee) != 0) {
		slot = 2;
	}
	Cast(game, unit, m.Skill(slot), m.Mode(slot), target, 0, 0, slot == 4 ? delay + 10 : delay);
	last = slot == 4 ? 1 : 0;
}

// PD2 0x1035bc60, the ubertrappedsoul1-5 branch. PD2 finds Dclone through its boss slot (game+0x2600); the port looks
// for him in the soul's level, and, as PD2 with an empty slot, leaves a soul without him to D2R's TrappedSoul think.
auto FindDclone(Game* game, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* u = At<Unit*>(game, GameMonsters + bucket * 8); u != nullptr; u = At<Unit*>(u, UnitListNext)) {
			if (At<int32_t>(u, UnitClassId) == Dclone && LevelOf(u) == level) {
				return u;
			}
		}
	}
	return nullptr;
}

auto NearestPlayer(Game* game, Unit* unit, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	int32_t sx = 0, sy = 0;
	d2r::UnitXY(unit, &sx, &sy);
	Unit* nearest = nullptr;
	int32_t best = 0x7fffffff;
	for (auto* client = At<uint8_t*>(game, GameClients); client != nullptr; client = At<uint8_t*>(client, ClientNext)) {
		Unit* p = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
		if (p != nullptr && !Dead(p) && LevelOf(p) == level && Dist2(p, sx, sy) < best) {
			best = Dist2(p, sx, sy);
			nearest = p;
		}
	}
	return nearest;
}

auto ThinkSoul(Game* game, Unit* unit, uint8_t* tick) noexcept -> void {
	using namespace d2r::rva;
	Mon m {tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr};
	const int32_t level = LevelOf(unit);
	Unit* dclone = m.rec != nullptr ? FindDclone(game, level) : nullptr;
	if (dclone == nullptr) {
		VanillaAiThink(TrappedSoulRow)(game, unit, tick);
		return;
	}
	const int32_t dMode = At<int32_t>(dclone, UnitMode);
	const int32_t dLife = LifePercent(dclone);
	if (dMode == ModeDeath || dMode == ModeDead || dLife <= 0) {
		++g_soulDeaths;
		plog::Info("trapped soul %d: Dclone is dead or dying (mode %d, life %d%%), the soul dies (%u so far)",
		           At<int32_t>(unit, UnitClassId), dMode, dLife, g_soulDeaths.load());
		KillQuietly(game, unit);
		return;
	}

	// The nearest live player in the level is the target when the tick has none
	if (IdleWindow(game, nullptr, false)) {
		Idle(game, unit, 10);
		return;
	}
	Unit*& target = *reinterpret_cast<Unit**>(tick + AiTickTarget);
	if (Unit* nearest = NearestPlayer(game, unit, level); nearest != nullptr) {
		target = nearest;
	}

	const int32_t tier = Counter(game, TierKey);
	const int32_t delay = SoulDelay(m.AiDel(), tier);
	const int32_t skip = m.Aip(6) - 5 * tier + 5 > 5 ? m.Aip(6) - 5 * tier + 5 : 5;
	// Each soul waits for Dclone's life to drop below its aip4 (+12 per tier above 1), and for his novas to end
	if (dLife > m.Aip(4) + (tier - 1) * 12 || Stat(dclone, StatCooldown1) != 0) {
		Idle(game, unit, m.AiDel());
		return;
	}
	const int32_t burst = Stat(unit, StatCooldown1);
	if (burst <= 0) {
		// The boulder on the target, then aip5 meteors
		Cast(game, unit, m.Skill(2), m.Mode(2), target, 0, 0, delay);
		SetStat(unit, StatCooldown1, m.Aip(5));
		return;
	}
	SetStat(unit, StatCooldown1, burst - 1);
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	if (static_cast<int32_t>(d2r::RandomBelow(seed, 100)) < skip) {
		Idle(game, unit, m.AiDel());
		return;
	}
	int32_t x = 0, y = 0, tx = 0, ty = 0, spread = -1;
	if (m.Aip(8) != 2 && m.Aip(8) != 3) {
		// A random spot in the 50x50 square around the arena centre
		x = 0x3b00 + static_cast<int32_t>(d2r::RandomBelow(seed, 50));
		y = 0x197b + static_cast<int32_t>(d2r::RandomBelow(seed, 50));
	} else {
		// At the target, spread over 24 - 8 x tier subtiles
		if (target == nullptr || !d2r::UnitXY(target, &x, &y)) {
			return;  // PD2 returns with no delay too
		}
		spread = SoulSpread(tier);
		tx = x;
		ty = y;
		if (spread > 0) {
			x += static_cast<int32_t>(d2r::RandomBelow(seed, spread)) - spread / 2;
			y += static_cast<int32_t>(d2r::RandomBelow(seed, spread)) - spread / 2;
		}
	}
	target = nullptr;
	if (auto* path = At<uint8_t*>(unit, UnitPath); path != nullptr) {
		*reinterpret_cast<Unit**>(path + PathTargetUnit) = nullptr;
	}
	const int32_t wait = static_cast<int32_t>(d2r::RandomBelow(seed, 8)) + delay - 4;
	++g_soulCasts;
	// Sampled per soul and tier, so each soul's first cast at each tier is logged
	if (const int32_t soul = At<int32_t>(unit, UnitClassId) - FirstSoul; tier >= 0 && tier < 3 &&
	                                                                       plog::Sample(g_soulCastsBy[soul][tier])) {
		plog::Info("trapped soul %d: tier %d, meteor at (%d, %d), target (%d, %d) spread %d, delay %d (aidel %d), skip "
		           "%d%%, Dclone %d%% (%u so far)", At<int32_t>(unit, UnitClassId), tier, x, y, tx, ty, spread, wait,
		           m.AiDel(), skip, dLife, g_soulCasts.load());
	}
	Cast(game, unit, m.Skill(1), m.Mode(1), nullptr, x, y, wait);
}

// --- nihlathakMap (PD2 0x1035bc60, its branch for monster 0x39a) ---------------------------------------------------
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

// shortcut: champion and unique picks (PD2 0x1037cbf0 / 0x1037caf0: mods and 3-6 minions) spawn as normal monsters,
// and the spot search is D2R's portal one, not PD2's 0x10389c20; port D2R's unique maker if a wave must match PD2's
auto SpawnWaveMonster(Game* game, Unit* unit, int32_t ux, int32_t uy, uint32_t* seed) noexcept -> bool {
	using namespace d2r::rva;
	int32_t xy[2] = {ux - 10 + static_cast<int32_t>(d2r::RandomBelow(seed, 20)),
	                 uy - 10 + static_cast<int32_t>(d2r::RandomBelow(seed, 20))};
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	room = room != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(room, xy[0], xy[1]) : nullptr;
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
	return d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster)(game, found, xy[0], xy[1], monster, 1, -1, 0x42) != nullptr;
}

auto ThinkNihlathak(Game* game, Unit* unit, uint8_t* tick) noexcept -> void {
	using namespace d2r::rva;
	Mon m {tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr};
	if (m.rec == nullptr) {
		VanillaAiThink(TrappedSoulRow)(game, unit, tick);
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
	if (plog::Sample(g_nihlStages)) {
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
		spawned += SpawnWaveMonster(game, unit, ux, uy, seed) ? 1 : 0;
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

void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	Mon m {tick != nullptr ? At<const uint8_t*>(tick, d2r::rva::AiTickMonStats) : nullptr};
	// PD2 hands a monstats row with no Hell aip3 to 0x103187c0 (a D2Game thunk); the port to D2R's Trap-Melee think
	if (m.rec == nullptr || m.Aip(3) == 0) {
		VanillaAiThink(TrapMeleeRow)(game, unit, tick);
		return;
	}
	ThinkDclone(game, unit, tick, m);
}

}  // namespace

auto InstallDcloneAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const bool ok = RegisterAiThink(context, TrapMeleeRow, d2r::rva::AiTrapMelee, Dclone, &Think);
	int souls = 0;
	for (int32_t id = FirstSoul; id <= LastSoul; ++id) {
		souls += RegisterAiThink(context, TrappedSoulRow, d2r::rva::AiTrappedSoul, id, &ThinkSoul) ? 1 : 0;
	}
	const bool nihl = RegisterAiThink(context, TrappedSoulRow, d2r::rva::AiTrappedSoul, NihlathakMap, &ThinkNihlathak);
	std::snprintf(report, reportSize,
	              "monai %d (Trap-Melee) think: uberdiablonew %s; monai %d (TrappedSoul): %d/5 souls, nihlathakMap %s",
	              TrapMeleeRow, ok ? "on" : "off", TrappedSoulRow, souls, nihl ? "on" : "off");
	return (ok ? 1 : 0) + (souls == 5 && nihl ? 1 : 0);
}

auto DcloneAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "dclone ai: %u thinks, %u nova phases, %u novas, %u waves, %u heals, %u casts; souls: %u meteors, %u "
	              "deaths; nihlathakMap: %u thinks, %u spawned, %u novas, %u deaths",
	              g_thinks.load(), g_phases.load(), g_novas.load(), g_waves.load(), g_heals.load(), g_casts.load(),
	              g_soulCasts.load(), g_soulDeaths.load(), g_nihlStages.load(), g_nihlSpawned.load(), g_nihlNovas.load(),
	              g_nihlDeaths.load());
}
