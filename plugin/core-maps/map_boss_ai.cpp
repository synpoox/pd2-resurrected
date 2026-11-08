// PD2's AIs for three map bosses (ProjectDiablo.dll; its AI table writer 0x10361150, #624). Each casts the skill whose
// missile spawns its adds through core-rules' summon-on-hit (shit 6):
//
// - leoricMapBoss (0x1035da60, monai 114 ReanimatedHorde): when stat 446 (mon_cooldown1) has run out, Skill1
//   (LeoricSummonNova) at the target, and the timer becomes 15-24 thinks. Else it counts down; then, on one roll r of
//   100: r < 20 and no Bone Armor state: Skill3 (Bone Armor) at himself; r < 60: past 15 subtiles Skill4 (Joust) at
//   the target, else r < 30 within 10 subtiles Skill2 (MonCharge) at the target's spot after a delay of 30, else in
//   melee range Skill5 (MonSmite). Otherwise a swing in melee range, or a walk at the target and a delay of the Hell
//   aidel.
// - siegebeastMapBoss (0x1035f820, monai 115 SiegeBeast): on r < 50 with stat 446 run out, Skill2
//   (SiegeBeastClusterbomb) at the target, 446 = 4 + r % 3 and 447 = 30 + 10 x (r % 3). Else 446 counts down; while
//   447 runs, r < 447 casts Skill4 (SiegeBeastBuffNova) and clears it, or it drops by 10 (to 0 below 15). Then r < 30
//   within 15 subtiles Skill3 (SiegeBeastFireNova); r < 60 in melee range a swing; r < 50 Skill1 (SiegeBeastOrb); else
//   a walk at the target and a delay of the Hell aidel.
// - SharpToothBoss (0x1035dca0's branch for 0x451, monai 120 Overseer, beside Lucion's): without the armageddon state,
//   Skill1 (SharptoothArmageddon) at the target. Else r < 30, or r < 60 past 15 subtiles: Skill2; r < 70: Skill3 (both
//   SharptoothChainPoison); else a walk to a random spot 5 subtiles off (1.13c D2Game 0xf29b0).
//
// With no target each runs its row's D2R think, as PD2 runs 1.13c's. Iskatu (monai 53 Summoner) needs no port: PD2's
// Summoner AI (0x10358510) has no branch for him and runs 1.13c's Summoner think (D2Game 0x85780), and D2R's
// (0x5b4d50) is the same think: both cast Skill5 (IskatuBlizzard) on the aip1 and aip2 rolls.
//
// Native table writes through ai_hooks: no SDK service runs monster AI (ai_hooks.cpp).
#include "map_boss_ai.h"

#include "ai_hooks.h"
#include "log.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// Merged monstats ids (PD2 998, 1000, 1105)
constexpr int32_t Leoric = 1015, SiegeBeast = 1017, Sharptooth = 1122;
constexpr int32_t ReanimatedHordeRow = 114, SiegeBeastRow = 115, OverseerRow = 120;  // monai.txt
// D2R's thinks of rows 114 and 115 (rva::AiTable + row x 0x20 in the 93847 dump)
constexpr uint64_t AiReanimatedHorde = 0x5c2f50, AiSiegeBeast = 0x5c31a0;
constexpr int32_t StatCooldown1 = 499, StatCooldown2 = 500;  // mon_cooldown1/2 (PD2 446/447)
constexpr int32_t StateBoneArmor = 14, StateArmageddon = 145;
constexpr int32_t ModeA1 = 4;
constexpr int32_t Hell = 2;
// 1.13c D2Game 0xf2540 / D2R 0x4a8a10 through its wrapper 0x4a8850: (game, unit, x, y), a walk to the point
constexpr uint64_t AiWalkTo = 0x4a8850;

std::atomic<uint32_t> g_thinks {0};
std::atomic<uint32_t> g_summons {0};
std::atomic<uint32_t> g_casts {0};
std::atomic<uint32_t> g_vanilla {0};

struct Tick {
	Game* game;
	Unit* unit;
	const uint8_t* rec;
	Unit* target;
	int32_t distance;
	bool melee;
};

// PD2 0x1036f320: a roll on the unit's seed
auto Rand(Unit* unit, uint32_t n) noexcept -> int32_t {
	return static_cast<int32_t>(
		d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit), n));
}
auto Stat(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}
auto SetStat(Unit* unit, int32_t stat, int32_t value) noexcept -> void {
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::rva::SetUnitStat)(unit, stat, value, 0);
}
auto HasState(Unit* unit, int32_t state) noexcept -> bool {
	return d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::UnitHasState)(unit, state) != 0;
}

// PD2 0x103a1880: (skill, target, x, y, mode) of monstats Skill<slot>. `summon` marks the cast that spawns adds.
auto Cast(const Tick& t, int slot, Unit* target, int32_t x = 0, int32_t y = 0, bool summon = false) noexcept -> void {
	using namespace d2r::rva;
	const int32_t skill = At<int16_t>(t.rec, MonSkills + 2 * (slot - 1));
	const int32_t mode = At<uint8_t>(t.rec, MonSkillModes + slot - 1);
	const int32_t ok = d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(
		AiUseSkill)(t.game, t.unit, static_cast<uint8_t>(mode), skill, target, x, y, 0);
	const int32_t classId = At<int32_t>(t.unit, UnitClassId);
	if (summon) {
		plog::Info("map boss ai: class %d casts skill %d (Skill%d) -> %d (%u so far)", classId, skill, slot, ok,
		           ++g_summons);
	} else if (plog::Sample(g_casts)) {
		plog::Info("map boss ai: class %d casts Skill%d -> %d (%u so far)", classId, slot, ok, g_casts.load());
	}
}
// PD2 0x103a1910
auto Delay(const Tick& t, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiSetDelay)(t.game, t.unit, frames);
}
// PD2 0x10356ea0
auto Swing(const Tick& t) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t, Unit*) noexcept>(d2r::rva::AiAttack)(t.game, t.unit, ModeA1, t.target);
}
// PD2 0x103186a0 (1.13c D2Game 0xf2540 with mode 2 at the target), then 0x103a1910 with the Hell aidel (PD2 reads
// monstats +0x51 whatever the difficulty)
auto ApproachAndWait(const Tick& t) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t) noexcept>(d2r::rva::AiApproach)(t.game, t.unit, t.target, 0);
	Delay(t, At<uint8_t>(t.rec, d2r::rva::MonAiDel + Hell));
}

void ThinkLeoric(const Tick& t) noexcept {
	const int32_t r = Rand(t.unit, 100);
	const int32_t cd = Stat(t.unit, StatCooldown1);
	if (cd < 1) {
		SetStat(t.unit, StatCooldown1, r % 10 + 15);
		Cast(t, 1, t.target, 0, 0, true);
		return;
	}
	SetStat(t.unit, StatCooldown1, cd - 1);
	if (r < 60) {
		if (r < 20 && !HasState(t.unit, StateBoneArmor)) {
			Cast(t, 3, t.unit);
			return;
		}
		if (t.distance > 15) {
			Cast(t, 4, t.target);
			return;
		}
		if (r < 30 && t.distance < 10) {
			int32_t x = 0, y = 0;
			d2r::UnitXY(t.target, &x, &y);
			Delay(t, 30);
			Cast(t, 2, nullptr, x, y);
			return;
		}
		if (t.melee) {
			Cast(t, 5, t.target);
			return;
		}
	}
	t.melee ? Swing(t) : ApproachAndWait(t);
}

void ThinkSiegeBeast(const Tick& t) noexcept {
	const int32_t r = Rand(t.unit, 100);
	const int32_t cd1 = Stat(t.unit, StatCooldown1);
	int32_t cd2 = Stat(t.unit, StatCooldown2);
	if (cd1 < 1 && r < 50) {
		SetStat(t.unit, StatCooldown1, r % 3 + 4);
		SetStat(t.unit, StatCooldown2, (r % 3 * 5 + 15) * 2);
		Cast(t, 2, t.target, 0, 0, true);
		return;
	}
	if (cd1 > 0) {
		SetStat(t.unit, StatCooldown1, cd1 - 1);
	}
	if (cd2 > 0) {
		if (r < cd2) {
			SetStat(t.unit, StatCooldown2, 0);
			Cast(t, 4, t.target);
			return;
		}
		SetStat(t.unit, StatCooldown2, cd2 < 15 ? 0 : cd2 - 10);
	}
	if (r < 60) {
		if (r < 30 && t.distance < 15) {
			Cast(t, 3, t.target);
			return;
		}
		if (t.melee) {
			Swing(t);
			return;
		}
	}
	if (r < 50) {
		Cast(t, 1, t.target);
		return;
	}
	ApproachAndWait(t);
}

// 1.13c D2Game 0xf29b0 with range 5 (PD2 0x103a18b0(5, ...)): one axis `range`, the other a roll below it, each with
// a random sign, from the unit's spot; then a walk there
auto Wander(const Tick& t, int32_t range) noexcept -> void {
	int32_t x = 0, y = 0;
	if (!d2r::UnitXY(t.unit, &x, &y)) {
		return;
	}
	int32_t dx = range, dy = range;
	(Rand(t.unit, 2) != 0 ? dy : dx) = Rand(t.unit, static_cast<uint32_t>(range));
	dx = Rand(t.unit, 2) != 0 ? -dx : dx;
	dy = Rand(t.unit, 2) != 0 ? -dy : dy;
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(AiWalkTo)(t.game, t.unit, x + dx, y + dy);
}

void ThinkSharptooth(const Tick& t) noexcept {
	if (!HasState(t.unit, StateArmageddon)) {
		Cast(t, 1, t.target, 0, 0, true);
		return;
	}
	const int32_t r = Rand(t.unit, 100);
	if ((r < 60 && t.distance > 15) || r < 30) {
		Cast(t, 2, t.target);
		return;
	}
	if (r < 70) {
		Cast(t, 3, t.target);
		return;
	}
	Wander(t, 5);
}

template <int32_t Row, void (*Port)(const Tick&) noexcept>
void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	const auto* rec = tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr;
	Unit* target = tick != nullptr ? At<Unit*>(tick, AiTickTarget) : nullptr;
	if (rec == nullptr || target == nullptr) {
		++g_vanilla;
		VanillaAiThink(Row)(game, unit, tick);
		return;
	}
	++g_thinks;
	Port(Tick {game, unit, rec, target, At<int32_t>(tick, AiTickDistance), At<int32_t>(tick, AiTickInMelee) != 0});
}

}  // namespace

auto InstallMapBossAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const bool leoric =
		RegisterAiThink(context, ReanimatedHordeRow, AiReanimatedHorde, Leoric, &Think<ReanimatedHordeRow, ThinkLeoric>);
	const bool siege =
		RegisterAiThink(context, SiegeBeastRow, AiSiegeBeast, SiegeBeast, &Think<SiegeBeastRow, ThinkSiegeBeast>);
	const bool sharptooth = RegisterAiThink(context, OverseerRow, d2r::rva::AiOverseer, Sharptooth,
	                                        &Think<OverseerRow, ThinkSharptooth>);
	std::snprintf(report, reportSize, "Leoric (monai %d) %s, Siege Beast (%d) %s, Sharptooth (%d) %s",
	              ReanimatedHordeRow, leoric ? "on" : "FAILED", SiegeBeastRow, siege ? "on" : "FAILED", OverseerRow,
	              sharptooth ? "on" : "FAILED");
	return (leoric ? 1 : 0) + (siege ? 1 : 0) + (sharptooth ? 1 : 0);
}

auto MapBossAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "map boss ai: %u thinks, %u summon casts, %u other casts, %u without a target",
	              g_thinks.load(), g_summons.load(), g_casts.load(), g_vanilla.load());
}
