// The kicks' base damage is PD2's (#2051): the unit's damage stats, not the skill's.
//
// PD2 patches 1.13c's kick fill (D2Game +0x92360) at its three callers (rel32 at +0x92c83 Dragon Talon's hit builder,
// +0x92a23 srvst 27 Dragon Tail, +0x93808 srvdo 52) to ProjectDiablo.dll 0x103a25a0 (PD2.gpr 0x103a2530), a thunk to
// 0x103022c0 (game, block, target, skill, level, attacker). Its base, in 1/256:
//   min = (mindamage 21 - the weapon's own 21 + item_normaldamage 111) + the boots' min + dexterity / 4
//   max = (maxdamage 22 - the weapon's own 22 + item_normaldamage 111) + the boots' max + dexterity / 3
// The boots come from 1.13c's boots helper (D2R 0x33af30: item_kickdamage 137, the boots' damage, and their % into the
// damage%), the weapon is the wielded one when it is of type weapon (45; none: nothing subtracted). Both are scaled by
// 0x1037c530: trunc(v x pct / 100), capped at INT_MAX, pct = 100 + the block's damage% + the boots' + damage vs undead
// (122) / demon (121) on such a target. Physical = min + rand(max - min), set into the block (1.13c +0x08). Then
// 1.13c's attack fill (1, 0x80), as D2R. The skill's own physical damage (D2R 0x3385b0 / 0x338160) is not used.
// PD2's own srvst 27 and srvdo 50 (Dragon Tail) call 0x103022c0 too; its srvdo 178 (Dragon Flight) calls 1.13c's fill.
//
// D2R 93847's fill 0x573af0 (game, block, attacker, target, skill, level) is 1.13c's: skill physical + damage%, boots,
// roll, `add [block+0x18]`, then ElementalDamage, the weapon unlink and 0x44c030 (1, 0x80). Its callers: Dragon Talon's
// builder 0x572eb0 (0x573035, #1862 already wraps it with calc2), srvst 27 0x571f40 (0x5720f3) and srvdo 52 0x56fc80
// (0x56fec8). core-rules' srvdo 178 calls the fill directly and keeps D2R's base, as PD2's 178 keeps 1.13c's. The
// three calls are redirected by signature (they name the target), and the fill's own call to ElementalDamage right
// after the physical add is redirected to overwrite the physical with PD2's base. No SDK service builds a kick's hit.
#include "common.h"
#include "../item_rules.h"
#include "registry.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

// PD2's base before the roll, in 1/256 (0x103022c0's sums and 0x1037c530's scaling)
struct KickStats {
	int32_t minDam, maxDam;    // the unit's 21 / 22
	int32_t weapMin, weapMax;  // the weapon's own 21 / 22
	int32_t normal;            // item_normaldamage (111)
	int32_t dex;
	int32_t bootsMin, bootsMax;  // the boots helper's sums
	int32_t pct;                 // 100 + damage% + the boots' + undead / demon
};
struct KickRange {
	int32_t min, max;
};

constexpr auto Scale(int32_t v, int32_t pct) noexcept -> int32_t {
	const double r = static_cast<double>(v) * pct / 100.0;
	return r > 2147483647.0 ? INT_MAX : static_cast<int32_t>(r);
}

constexpr auto Pd2Base(const KickStats& s) noexcept -> KickRange {
	const int32_t min = (s.minDam - s.weapMin + s.normal) * 256 + s.bootsMin * 256 + s.dex * 256 / 4;
	const int32_t max = (s.maxDam - s.weapMax + s.normal) * 256 + s.bootsMax * 256 + s.dex * 256 / 3;
	return {Scale(min, s.pct), Scale(max, s.pct)};
}

// 10-20 on the unit with a 5-10 weapon, 3 normal, 90 dex, 20-40 boots, pct 100 + 60 + 0
static_assert(Pd2Base({10, 20, 5, 10, 3, 90, 20, 40, 160}).min == ((8 * 256 + 20 * 256 + 5760) * 16) / 10);
static_assert(Pd2Base({10, 20, 5, 10, 3, 90, 20, 40, 160}).max == (13 * 256 + 40 * 256 + 7680) * 16 / 10);
static_assert(Pd2Base({0, 0, 0, 0, 0, 0, 0, 0, 100}).max == 0);
static_assert(Pd2Base({0, 0, 0, 0, 0, 1, 0, 0, 100}).min == 64);  // dex x 256 / 4, then / 3 for the max
static_assert(Pd2Base({INT_MAX / 512, INT_MAX / 512, 0, 0, 0, 0, 0, 0, 100000}).max == INT_MAX);

// The fill's callers: (rcx game, rdx &block, r8 attacker, r9 target, [rsp+0x20] skill, [rsp+0x28] level), E8 last.
// Dragon Talon's builder 0x57301a (#1862's pattern, which skips this tail, holds the knockback byte it patches)
constexpr const char* TalonCall =
    "89 74 24 28 48 8d 54 24 40 4d 8b cf 44 89 64 24 20 4c 8b c7 89 5c 24 5c 49 8b ce e8";
// srvst 27 (Dragon Tail) 0x5720dc
constexpr const char* TailCall = "44 89 6c 24 28 48 8d 54 24 30 4d 8b cf 89 74 24 20 4c 8b c3 48 8b cf e8";
// srvdo 52 0x56fead (no skill of the mod's data uses it; PD2 patches its 1.13c call too)
constexpr const char* Srvdo52Call =
    "4d 8b ce 89 5c 24 28 4c 8b c7 89 44 24 6c 48 8d 54 24 50 44 89 64 24 20 48 8b ce e8";
constexpr uint32_t TalonCallAt = 27, TailCallAt = 23, Srvdo52CallAt = 27;
// Inside the fill: the boots helper call (rcx attacker, rdx &min, r8 &max, r9 &pct) at 0x573d00, and the
// ElementalDamage call (attacker, block, skill, level) right after `add [r14+0x18],ecx` at 0x573f39
constexpr const char* BootsCall = "89 44 24 30 48 8d 54 24 78 48 8b ce 4c 8d 4c 24 70 e8";
constexpr const char* ElementalCall = "03 ca 41 01 4e 18 49 8b d6 48 8b ce e8";
constexpr uint32_t BootsCallAt = 17, ElementalCallAt = 12;

constexpr int32_t StatDex = 2, StatMin = 21, StatMax = 22, StatNormal = 111, StatDemon = 121, StatUndead = 122;
constexpr int32_t TypeWeapon = 45;

using FillFn = void (*)(Game*, uint8_t*, Unit*, Unit*, int32_t, int32_t) noexcept;
using ElementalFn = void (*)(Unit*, uint8_t*, int32_t, int32_t) noexcept;
using BootsFn = void (*)(Unit*, int32_t*, int32_t*, int32_t*) noexcept;
FillFn g_talonFill = nullptr, g_tailFill = nullptr, g_srvdo52Fill = nullptr;
ElementalFn g_elemental = nullptr;
BootsFn g_boots = nullptr;

// The PD2 fill in progress on this thread (its block and target); a fill no PD2 caller reached (srvdo 178) has none
thread_local uint8_t* t_block = nullptr;
thread_local Unit* t_target = nullptr;
std::atomic<uint32_t> g_kicks {0};

auto Stat(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

auto TargetFlags(Unit* unit) noexcept -> uint32_t {
	using namespace d2r::rva;
	if (unit == nullptr || d2r::At<int32_t>(unit, UnitType) != UnitMonster) {
		return 0;
	}
	const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		d2r::api.unitDataFlag(unit), d2r::At<int32_t>(unit, UnitClassId));
	return mon != nullptr ? d2r::At<uint32_t>(mon, MonFlags) : 0;
}

auto Fill(FillFn original, Game* game, uint8_t* block, Unit* unit, Unit* target, int32_t skill, int32_t level) noexcept
	-> void {
	uint8_t* const outerBlock = t_block;
	Unit* const outerTarget = t_target;
	t_block = block;
	t_target = target;
	original(game, block, unit, target, skill, level);
	t_block = outerBlock;
	t_target = outerTarget;
}

auto TalonFill(Game* g, uint8_t* b, Unit* u, Unit* t, int32_t s, int32_t l) noexcept -> void {
	Fill(g_talonFill, g, b, u, t, s, l);
}
auto TailFill(Game* g, uint8_t* b, Unit* u, Unit* t, int32_t s, int32_t l) noexcept -> void {
	Fill(g_tailFill, g, b, u, t, s, l);
}
auto Srvdo52Fill(Game* g, uint8_t* b, Unit* u, Unit* t, int32_t s, int32_t l) noexcept -> void {
	Fill(g_srvdo52Fill, g, b, u, t, s, l);
}

// D2R's physical is in; PD2's replaces it before the elemental damage, as 0x103022c0 sets it before its own steps
auto Elemental(Unit* unit, uint8_t* block, int32_t skill, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	if (block != nullptr && block == t_block && unit != nullptr) {
		KickStats s {};
		s.pct = d2r::At<int32_t>(block, DmgPercent);
		g_boots(unit, &s.bootsMin, &s.bootsMax, &s.pct);
		s.pct += 100;
		Unit* weapon = d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(unit);
		if (weapon != nullptr && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(weapon, TypeWeapon) != 0) {
			s.weapMin = Stat(weapon, StatMin);
			s.weapMax = Stat(weapon, StatMax);
		}
		s.minDam = Stat(unit, StatMin);
		s.maxDam = Stat(unit, StatMax);
		s.normal = Stat(unit, StatNormal);
		s.dex = Stat(unit, StatDex);
		const uint32_t flags = TargetFlags(t_target);
		s.pct += (flags & MonUndead) != 0 ? Stat(unit, StatUndead) : 0;
		s.pct += (flags & MonDemon) != 0 ? Stat(unit, StatDemon) : 0;
		const KickRange r = Pd2Base(s);
		const uint32_t span = r.max > r.min ? static_cast<uint32_t>(r.max - r.min) : 0;
		int32_t& phys = *reinterpret_cast<int32_t*>(block + DmgPhys);
		const int32_t d2rPhys = phys;
		phys = r.min + static_cast<int32_t>(d2r::RandomBelow(d2r::api.unitSeed(unit), span));
		if (pd2::HotSample(g_kicks)) {
			plog::Info("kick-base: skill %d lvl %d phys %d -> %d (/256), min %d max %d: dam %d-%d weapon %d-%d normal %d "
			           "dex %d boots %d-%d pct %d",
			           skill, level, d2rPhys, phys, r.min, r.max, s.minDam, s.maxDam, s.weapMin, s.weapMax, s.normal,
			           s.dex, s.bootsMin, s.bootsMax, s.pct);
		}
	}
	g_elemental(unit, block, skill, level);
}

auto CallTarget(uint64_t exeBase, const char* pattern, uint32_t callAt) noexcept -> uint64_t {
	uint64_t rva = 0;
	if (sigscan::ScanUnique(exeBase, pattern, callAt, &rva) != 1) {
		return 0;
	}
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(exeBase + rva + 1), sizeof(rel));
	return exeBase + rva + 5 + static_cast<int64_t>(rel);
}

}  // namespace

auto InstallKickBase(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	// The fill's own calls first: until a caller is redirected, the elemental hook passes every fill through
	g_boots = reinterpret_cast<BootsFn>(CallTarget(context->exeBase, BootsCall, BootsCallAt));
	const bool fill = g_boots != nullptr &&
	                  RedirectCall(context, ElementalCall, ElementalCallAt, reinterpret_cast<uint64_t>(&Elemental),
	                               reinterpret_cast<uint64_t*>(&g_elemental));
	const bool talon = fill && RedirectCall(context, TalonCall, TalonCallAt, reinterpret_cast<uint64_t>(&TalonFill),
	                                        reinterpret_cast<uint64_t*>(&g_talonFill));
	const bool tail = fill && RedirectCall(context, TailCall, TailCallAt, reinterpret_cast<uint64_t>(&TailFill),
	                                       reinterpret_cast<uint64_t*>(&g_tailFill));
	const bool srvdo52 = fill && RedirectCall(context, Srvdo52Call, Srvdo52CallAt, reinterpret_cast<uint64_t>(&Srvdo52Fill),
	                                          reinterpret_cast<uint64_t*>(&g_srvdo52Fill));
	const bool on = fill && talon && tail && srvdo52;
	plog::Info("kick-base: PD2 kick base (Dragon Talon %d, Dragon Tail %d, srvdo 52 %d) %s", talon, tail, srvdo52,
	           on ? "on" : "FAILED (fill or caller signature)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; kick-base%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallKickBase, 260)
