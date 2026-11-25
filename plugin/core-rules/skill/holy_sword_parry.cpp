// holy_sword_parry
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace {

// --- Holy Sword parry (PD2 avoid roll 0x103035f0, #1466) -------------------------------------------------------------
// PD2's avoid rolls a block for a unit whose composit weapon class is 5 (2hs, a two-handed swing: Holy Sword's sword)
// as well as 13 (ht2, claws), with the block chance (passive_weaponblock, which Holy Sword's aura sets) capped at 75, and
// answers 0x10, the result a block gives. D2R's avoid (0x44ef70, (attacker, defender, ranged flag)) only blocks for 13, so
// this wraps it: a player in class 5 rolls PD2's parry first (PD2 rolls the block before the evade, dodge and avoid
// rolls), and class 13 (claws) rolls it too in the modes D2R skips (walk, run: #1483); everything else is D2R's own code. PD2's 4-frame no-avoid window after any
// avoid is kept per player id (AvoidWindow, parry_roll.h). Not ported: its half chance in the PvP arenas (levels 157, 159, 166: cut), and sound 4713 (ignored by decision).
// Block chance (0x450070 (unit)), composit weapon class (0x3b6e00 (unit, unit data, int* out, -1, 1)) and the unit
// data (UnitInventory) are what 0x44ef70 itself calls. Found by signature: the prologue and the first calls, calls masked.
constexpr int32_t StatEvade = 0x154, StatDodge = 0x152, StatAvoid = 0x153;

auto StatOf(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

constexpr uint8_t AvoidD2REntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                     0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};

constexpr uint64_t BlockChanceD2R = 0x450070, CompositClassD2R = 0x3b6e00;

constexpr const char* AvoidPattern =
    "48 89 5C 24 08 48 89 6C 24 10 56 57 41 56 48 83 EC 30 48 8B F1 45 8B F0 48 8B CA 48 8B FA 33 ED E8 ?? ?? ?? ?? "
    "48 8B CF 85 C0 0F 85 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C0 FE 48 8B CF 83 F8 01 0F 86 ?? ?? ?? ?? E8 ?? ?? ?? ??";

using AvoidFn = uint32_t (*)(Unit* attacker, Unit* defender, int32_t ranged) noexcept;

AvoidFn g_avoidOriginal = nullptr;

auto AvoidRolled(Unit* attacker, Unit* defender, int32_t ranged) noexcept -> uint32_t;


pd2::parry::AvoidWindow g_avoidWindow;  // server game thread only, like the roll


auto AvoidWithParry(Unit* attacker, Unit* defender, int32_t ranged) noexcept -> uint32_t {
	// PD2 keeps the no-avoid window for players only (#1482); the frame is the defender's game's
	const bool player = defender != nullptr && At<int32_t>(defender, d2r::rva::UnitType) == 0;
	Game* game = player ? At<Game*>(defender, d2r::rva::UnitGame) : nullptr;
	const int32_t frame = game != nullptr ? At<int32_t>(game, d2r::rva::GameFrame) : 0;
	const uint32_t id = player ? At<uint32_t>(defender, d2r::rva::UnitIdField) : 0;
	if (game != nullptr && g_avoidWindow.Blocked(id, frame)) {
		return 0;
	}
	const uint32_t result = AvoidRolled(attacker, defender, ranged);
	if (game != nullptr && (result & pd2::parry::AvoidMask) != 0) {
		g_avoidWindow.Set(id, frame);
	}
	return result;
}


auto AvoidRolled(Unit* attacker, Unit* defender, int32_t ranged) noexcept -> uint32_t {
	void* data = defender == nullptr ? nullptr
	             : d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(d2r::rva::UnitInventory)(defender, __FILE__, __LINE__);
	if (data != nullptr) {  // PD2 rolls for any defender
		int32_t weaponClass = -1;
		d2r::Engine<int64_t (*)(Unit*, void*, int32_t*, int32_t, int32_t) noexcept>(CompositClassD2R)(defender, data,
		                                                                                              &weaponClass, -1, 1);
		const int32_t unitType = At<int32_t>(defender, d2r::rva::UnitType), mode = At<int32_t>(defender, d2r::rva::UnitMode);
		const int32_t raw = pd2::parry::RollsPd2Block(weaponClass, unitType, mode)
		                        ? d2r::Engine<int32_t (*)(Unit*) noexcept>(BlockChanceD2R)(defender)
		                        : 0;
		const bool lucion = attacker != nullptr && At<int32_t>(attacker, d2r::rva::UnitType) == 1 &&
		                    At<int32_t>(attacker, d2r::rva::UnitClassId) == pd2::parry::LucionMerged;
		const int32_t chance = pd2::parry::Chance(raw, lucion);
		if (chance > 0) {
			const int32_t roll = static_cast<int32_t>(
				d2r::Roll(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(defender)) % 100);
			const bool parried = pd2::parry::Parries(chance, static_cast<uint32_t>(roll));
			if (parried) {
				return pd2::parry::ResultBlock;
			}
		}
	}
	const uint32_t result = g_avoidOriginal(attacker, defender, ranged);
	// #1496: in walk and run D2R's avoid ends at the evade roll (stat 0x154: 8, or 0 on a miss); PD2 goes on to the dodge
	// (0x152, melee) or avoid (0x153, ranged) roll when evade is above 0 and its roll failed (evade 0: 0, as here).
	if (result == 0 && defender != nullptr &&
	    pd2::parry::D2RSkipsBlock(At<int32_t>(defender, d2r::rva::UnitType), At<int32_t>(defender, d2r::rva::UnitMode)) &&
	    StatOf(defender, StatEvade) > 0) {
		const int32_t chance = StatOf(defender, ranged == 0 ? StatDodge : StatAvoid);
		if (chance > 0) {
			const int32_t roll = static_cast<int32_t>(
				d2r::Roll(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(defender)) % 100);
			const bool hit = roll < chance;
			if (hit) {
				return ranged == 0 ? 4 : 2;
			}
		}
	}
	return result;
}

// Set while D2R's Smite do (srvdo 150) runs for the Smite row: that do queues its one hit (QueueHit) and applies it.

// Set while D2R's Smite do (srvdo 150) runs for the Smite row: that do queues its one hit (QueueHit) and applies it.
thread_local int32_t t_smiteLevel = 0;


// PD2's event 16 on Smite: one roll a hit with the crushingblow stat, its per-hand sum and calc3, on the damage block
// before D2R applies it, as a domeleedamage event runs. CrushingBlowApply's slot holds PD2's blow (item_rules.cpp,
// with Smite's calc4).
// shortcut: if D2R also rolls item crushing blow inline (outside the dump), a Smite hit rolls the item's chance twice;
// upgrade if the game check shows more blows than crushingblow + calc3 allows.
void SmiteCrushingBlow(Game* game, Unit* unit, Unit* target, uint8_t* damage, int32_t level) noexcept {
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), SkillSmite);
	const int32_t stat = std::max(d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
	                                  unit, StatCrushingBlow, 0),
	                              0);
	const int32_t calc3 = skill != nullptr ? d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc3, SkillSmite, level) : 0;
	const int32_t chance = stat + HandStatSum(unit, StatCrushingBlow) + calc3;
	const int32_t roll = chance > 0 ? static_cast<int32_t>(
	                                      d2r::Roll(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit)) % 100)
	                                : 100;
	int32_t result = 0;
	if (roll < chance) {
		d2r::CrushingBlowArgs args {damage, {4, 10, 10}, 0};  // domeleedamage, never 6
		result = d2r::Engine<int32_t (*)(Game*, Unit*, Unit*, d2r::CrushingBlowArgs*) noexcept>(
			d2r::rva::CrushingBlowApply)(game, unit, target, &args);
	}
	++g_smiteEvents;
}


using QueueHitFn = void (*)(Game*, Unit*, Unit*, uint8_t*, uint8_t) noexcept;

}  // namespace

namespace pd2r::skills {

QueueHitFn g_queueHitOriginal = nullptr;

// D2R's hit queue (inline hook): Smite's hit gets its crushing-blow roll when it landed (D2R's own test there).

// D2R's hit queue (inline hook): Smite's hit gets its crushing-blow roll when it landed (D2R's own test there).
void QueueHitHook(Game* game, Unit* unit, Unit* target, uint8_t* damage, uint8_t flag) noexcept {
	if (t_smiteLevel > 0 && game != nullptr && unit != nullptr && target != nullptr && damage != nullptr) {
		const uint16_t result = At<uint16_t>(damage, d2r::rva::DamageResult);
		if ((result & d2r::rva::ResultHit) != 0 && (result & d2r::rva::ResultAvoided) == 0) {
			SmiteCrushingBlow(game, unit, target, damage, t_smiteLevel);
		}
	}
	g_queueHitOriginal(game, unit, target, damage, flag);
}

// srvdo 150 (D2R's Smite do, also MonSmite's, UberTalicSmite's, MonSmiteLucion's): only the Smite row rolls, as only
// its PD2 start adds the event.

// srvdo 150 (D2R's Smite do, also MonSmite's, UberTalicSmite's, MonSmiteLucion's): only the Smite row rolls, as only
// its PD2 start adds the event.
auto SrvDoSmite(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	t_smiteLevel = skillId == SkillSmite ? std::max(level, 1) : 0;
	const int64_t result = d2r::Engine<SrvDoFn>(d2r::rva::SrvDoSmiteD2R)(game, unit, skillId, level);
	t_smiteLevel = 0;
	return result;
}

// --- Monster kits (W36; docs/research/skills/w36-monster-kits.md) ---------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 173 at 0x103b3520, cltdo 109 at 0x103abba0, srvdo 181 at 0x103b4cf0, srvst 67 at
// 0x103b6520 with its per-unit callback 0x1036d5a0.

}  // namespace pd2r::skills

auto InstallHolySwordParry(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	const bool parry = sigscan::HookBySignature(context, "AvoidRoll", AvoidPattern, 0, AvoidD2REntry, sizeof(AvoidD2REntry),
	                                               reinterpret_cast<void*>(&AvoidWithParry),
	                                               reinterpret_cast<void**>(&g_avoidOriginal));
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; holy sword parry%s", parry ? "" : " FAILED (signature)");
	}
	return parry;
}

PD2R_RULE(InstallHolySwordParry, 340)

