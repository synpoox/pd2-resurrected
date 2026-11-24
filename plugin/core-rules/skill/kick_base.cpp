// The kicks' base damage is PD2's (#2051): the unit's damage stats, not the skill's.
//
// PD2 patches 1.13c's kick fill (D2Game +0x92360) at its three callers (rel32 at +0x92c83 Dragon Talon's hit builder,
// +0x92a23 srvst 27 Dragon Tail, +0x93808 srvdo 52) to ProjectDiablo.dll 0x103a25a0 (PD2.gpr 0x103a2530), a thunk to
// 0x103022c0 (game, block, target, skill, level, attacker). Its base, in 1/256:
//   min = (mindamage 21 - the weapon's own 21 + item_normaldamage 111) + the boots' min + dexterity / 4
//   max = (maxdamage 22 - the weapon's own 22 + item_normaldamage 111) + the boots' max + dexterity / 3
// The boots come from 1.13c's boots helper (D2R 0x33af30: item_kickdamage 137, the boots' damage, and their % into the
// damage%), the weapon is the wielded one (inventory) when it is of type weapon (45) and usable (not broken; else nothing subtracted). Both are scaled by
// 0x1037c530: trunc(v x pct / 100), capped at INT_MAX, pct = 100 + the block's damage% + the boots' + damage vs undead
// (122) / demon (121) on such a target. Physical = min + rand(max - min), set into the block (1.13c +0x08). Then
// 1.13c's attack fill (1, 0x80), as D2R. The skill's own physical damage (D2R 0x3385b0 / 0x338160) is not used.
// PD2's own srvst 27 and srvdo 50 (Dragon Tail) call 0x103022c0 too; its srvdo 178 (Dragon Flight) calls 1.13c's fill.
//
// D2R 93847's fill 0x573af0 (game, block, attacker, target, skill, level) is 1.13c's: skill physical + damage%, boots,
// roll, `add [block+0x18]`, then ElementalDamage, the weapon unlink and 0x44c030 (1, 0x80). Its callers: Dragon Talon's
// builder 0x572eb0 (0x573035, #1862 already wraps it with calc2), srvst 27 0x571f40 (0x5720f3) and srvdo 52 0x56fc80
// (0x56fec8). core-rules' srvdo 178 calls the fill directly and keeps D2R's base, as PD2's 178 keeps 1.13c's. The
// fill's entry is hooked by signature (it names the target; Dragon Talon's call is #1862's patch already), and the
// fill's own call to ElementalDamage right after the physical add is redirected to overwrite the physical with PD2's
// base. No SDK service builds a kick's hit.
#include "common.h"
#include "../item_rules.h"
#include "kick_base.h"
#include "registry.h"

#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;
using namespace kick_base;

// The fill's entry, (game, block, attacker, target, skill, level): mov [rsp+0x18],rbx / mov [rsp+0x20],rbp / push rsi /
// push rdi / push r13 / push r14 / push r15 / sub rsp,0x40, then its argument moves up to the first call
constexpr const char* FillPattern =
    "48 89 5c 24 18 48 89 6c 24 20 56 57 41 55 41 56 41 57 48 83 ec 40 49 8b f0 4c 8b f2 4c 8b c2 4d 8b e9 48 8b d6 "
    "4c 8b f9 e8";
constexpr uint8_t FillEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x56,
                                 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40};
// Inside the fill: the boots helper call (rcx attacker, rdx &min, r8 &max, r9 &pct) at 0x573d00, and the
// ElementalDamage call (attacker, block, skill, level) right after `add [r14+0x18],ecx` at 0x573f39
constexpr const char* BootsCall = "89 44 24 30 48 8d 54 24 78 48 8b ce 4c 8d 4c 24 70 e8";
constexpr const char* ElementalCall = "03 ca 41 01 4e 18 49 8b d6 48 8b ce e8";
constexpr uint32_t BootsCallAt = 17, ElementalCallAt = 12;
constexpr uint16_t DragonFlightDo = 178;  // core-rules' srvdo 178: PD2's calls 1.13c's fill

using FillFn = void (*)(Game*, uint8_t*, Unit*, Unit*, int32_t, int32_t) noexcept;
using ElementalFn = void (*)(Unit*, uint8_t*, int32_t, int32_t) noexcept;
using BootsFn = void (*)(Unit*, int32_t*, int32_t*, int32_t*) noexcept;
using WeaponFn = Unit* (*)(void* inventory) noexcept;
using UsableFn = int32_t (*)(Unit* item) noexcept;
FillFn g_fill = nullptr;
ElementalFn g_elemental = nullptr;
BootsFn g_boots = nullptr;
WeaponFn g_weapon = nullptr;
UsableFn g_usable = nullptr;

// The PD2 fill in progress on this thread (its block and target); srvdo 178's fill has none
thread_local uint8_t* t_block = nullptr;
thread_local Unit* t_target = nullptr;

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

// PD2 routes the fill's three 1.13c callers (Dragon Talon's builder, srvst 27, srvdo 52) to its own fill and keeps
// 1.13c's for its srvdo 178. Here every fill but srvdo 178's takes PD2's base: Dragon Talon's call is #1862's relay
// already (a second MutationService patch on its E8 is refused), and core-rules' 178 calls this entry directly.
auto Fill(Game* game, uint8_t* block, Unit* unit, Unit* target, int32_t skill, int32_t level) noexcept -> void {
	const uint8_t* record = game != nullptr ? d2r::api.getSkillRecord(d2r::DataFlag(game), skill) : nullptr;
	const bool pd2 = record != nullptr && d2r::At<uint16_t>(record, d2r::rva::SkSrvDoFunc) != DragonFlightDo;
	uint8_t* const outerBlock = t_block;
	Unit* const outerTarget = t_target;
	t_block = pd2 ? block : nullptr;
	t_target = target;
	g_fill(game, block, unit, target, skill, level);
	t_block = outerBlock;
	t_target = outerTarget;
}

// D2R's physical is in; PD2's replaces it before the elemental damage, as 0x103022c0 sets it before its own steps
auto Elemental(Unit* unit, uint8_t* block, int32_t skill, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	if (block != nullptr && block == t_block && unit != nullptr) {
		KickStats s {};
		s.pct = d2r::At<int32_t>(block, DmgPercent);
		g_boots(unit, &s.bootsMin, &s.bootsMax, &s.pct);
		s.pct += 100;
		void* inventory =
			d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(unit, __FILE__, __LINE__);
		Unit* weapon = inventory != nullptr ? g_weapon(inventory) : nullptr;
		if (weapon != nullptr && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(weapon, TypeWeapon) != 0 &&
		    g_usable(weapon) != 0) {
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
		const uint32_t span = r.max > r.min ? static_cast<uint32_t>(r.max - r.min) : 1;  // PD2 rolls % 1 too
		int32_t& phys = *reinterpret_cast<int32_t*>(block + DmgPhys);
		const int32_t d2rPhys = phys;
		phys = r.min + static_cast<int32_t>(d2r::RandomBelow(d2r::api.unitSeed(unit), span));
	}
	g_elemental(unit, block, skill, level);
}

}  // namespace

auto InstallKickBase(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	// The fill's own calls first, then its entry
	g_boots = reinterpret_cast<BootsFn>(CallTarget(context->exeBase, BootsCall, BootsCallAt));
	g_weapon = reinterpret_cast<WeaponFn>(CallTarget(context->exeBase, WeaponBlock, WeaponCallAt));
	g_usable = reinterpret_cast<UsableFn>(CallTarget(context->exeBase, WeaponBlock, UsableCallAt));
	const bool inner = g_boots != nullptr && g_weapon != nullptr && g_usable != nullptr &&
	                   RedirectCall(context, ElementalCall, ElementalCallAt, reinterpret_cast<uint64_t>(&Elemental),
	                                reinterpret_cast<uint64_t*>(&g_elemental));
	const bool on = inner && sigscan::HookBySignature(context, "kick fill", FillPattern, 0, FillEntry, sizeof(FillEntry),
	                                                  reinterpret_cast<void*>(&Fill), reinterpret_cast<void**>(&g_fill));
	plog::Info("kick-base: PD2 kick base %s", on ? "on" : "FAILED (kick fill signature or bytes)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; kick-base%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallKickBase, 260)
