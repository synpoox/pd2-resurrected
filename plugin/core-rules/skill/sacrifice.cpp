// sacrifice
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

// --- Sacrifice's corpse explosion (#1994, docs/features/core-rules.md "Sacrifice corpse explosion") ----------------
// PD2 wraps 1.13c's damage deal (+0xdcf20) in ProjectDiablo.dll 0x103025c0 (PD2.gpr 0x103025b0). After the deal, a hit
// with result flag 2 (the killing hit) whose attacker's current skill is Sacrifice (96) puts state sacrifice_explosion
// on the victim and queues an event at frame + clamp(death animation ticks - 6, 3, 30) carrying the overkill: the damage
// total (block +0x4c) minus stat previous_hitpoints, the life PD2's hit apply copy (0x103059a0) stores just before the
// life loss, at least 0. The event's callback (0x10303aa0, PD2.gpr 0x10303a90) hits every unit within calc3 of the
// corpse for physical damage = Param5 % of the monster's average monlvl life + Param6 % of the overkill.
// D2R: the deal 0x44a9b0 calls the kill 0x44db20 (game, victim, attacker, 1) only for result flag 2, as its last step.
// That call is redirected and its 1 becomes the damage block (rbx there), so the explosion is queued after the kill, as
// PD2's is after the deal. The life before the loss is read after the hit apply's (0x44ce80) life add call (absorbed
// life), the step just before the loss. No PluginSDK service gives a kill with its damage block, and the deal's entry
// stays free for other plugins (d2r-tools' dps-counter hooks it).
constexpr const char* SacrificeKillCall =
    "0f b7 43 04 a8 02 74 ?? 41 b9 01 00 00 00 4d 8b c6 48 8b d7 48 8b cd e8 ?? ?? ?? ?? e9";

constexpr uint32_t SacrificeKillArgAt = 8, SacrificeKillCallAt = 23;
// mov r9d,1 -> mov r9,rbx (the deal keeps its block in rbx from its entry) / nop3

// mov r9d,1 -> mov r9,rbx (the deal keeps its block in rbx from its entry) / nop3
constexpr uint8_t KillArgOne[] = {0x41, 0xb9, 0x01, 0x00, 0x00, 0x00};

constexpr uint8_t KillArgBlock[] = {0x4c, 0x8b, 0xcb, 0x0f, 0x1f, 0x00};

constexpr const char* SacrificeLifeCall = "8b 97 30 01 00 00 48 8b ce e8 ?? ?? ?? ?? 8b 9f 34 01 00 00 85 db 7e";

constexpr uint32_t SacrificeLifeCallAt = 9;
// (game, unit, type, frame, callback, a6, a7, a8): AddUnitEvent (0x48b720) is this with callback 0. The event loop
// (0x48c545) calls callback(game, unit, type, a6, a7) at the frame in place of the type's handler. D2R's own user is
// the object event of 0x590b00 (type 14, callback 0x595000); no RemoveUnitEvent call names 14. PD2 uses 1.13c's type 7,
// which D2R's 0x4a1820 removes from dead monsters.

// (game, unit, type, frame, callback, a6, a7, a8): AddUnitEvent (0x48b720) is this with callback 0. The event loop
// (0x48c545) calls callback(game, unit, type, a6, a7) at the frame in place of the type's handler. D2R's own user is
// the object event of 0x590b00 (type 14, callback 0x595000); no RemoveUnitEvent call names 14. PD2 uses 1.13c's type 7,
// which D2R's 0x4a1820 removes from dead monsters.
constexpr uint64_t AddUnitEventCallback = 0x48be80;

constexpr int32_t UnitEventCallback = 14;
// (dataFlag, monstats id, expansion, difficulty, level, flags, int32* out, 0): 1.13c D2Common #11089. Flags 1 fills
// out[0] / out[1] with the row's min / max life at that level (monlvl HP x monstats minHP / maxHP / 100).

// (dataFlag, monstats id, expansion, difficulty, level, flags, int32* out, 0): 1.13c D2Common #11089. Flags 1 fills
// out[0] / out[1] with the row's min / max life at that level (monlvl HP x monstats minHP / maxHP / 100).
constexpr uint64_t MonLevelStats = 0x3a26c0;
// (out, game, unit): byte +0 difficulty, int32 +4 and byte +8 non-zero for an expansion game, as the monster stat
// set-up 0x588d50 reads them before MonLevelStats

// (out, game, unit): byte +0 difficulty, int32 +4 and byte +8 non-zero for an expansion game, as the monster stat
// set-up 0x588d50 reads them before MonLevelStats
constexpr uint64_t GameMonsterInfo = 0x48ff40;
// Unit animation (D2Common Units.cpp 0x34c880): +0x64 frames << 8, +0x68 uint16 speed (256 = a frame a tick), +0x70 the
// animation data: 1.13c's +0x48, +0x4c, +0x50

// Unit animation (D2Common Units.cpp 0x34c880): +0x64 frames << 8, +0x68 uint16 speed (256 = a frame a tick), +0x70 the
// animation data: 1.13c's +0x48, +0x4c, +0x50
constexpr int UnitAnimFrames = 0x64, UnitAnimSpeed = 0x68, UnitAnimData = 0x70;

constexpr int32_t SkillSacrifice = 96, StatLevel = 12, UnitMissile = 3;

constexpr uint32_t OverkillMask = 0x0fffffff;  // PD2 packs the owner's unit type in the event value's top 4 bits


using UnitKillFn = void (*)(Game*, Unit*, Unit*, uint64_t) noexcept;

using AddLifeFn = void (*)(Unit*, int32_t) noexcept;

UnitKillFn g_unitKill = nullptr;

AddLifeFn g_addLife = nullptr;

bool g_killGetsBlock = false;   // set once the kill's argument patch is in: before that its 4th argument is the 1

int32_t g_sacrificeState = -1;  // states.txt sacrifice_explosion (ours 269, PD2 223)

thread_local Unit* t_lifeUnit = nullptr;

thread_local int32_t t_lifeBefore = 0;

// PD2's 0x1037c530: value x percent / 100 in doubles, truncated, at most INT32_MAX

// PD2's 0x1037c530: value x percent / 100 in doubles, truncated, at most INT32_MAX
constexpr auto Pd2Percent(int32_t value, int32_t percent) noexcept -> int32_t {
	const double v = static_cast<double>(value) * static_cast<double>(percent) / 100.0;
	return v > 2147483647.0 ? INT32_MAX : static_cast<int32_t>(v);
}

static_assert(Pd2Percent(256 * 1000, 5) == 256 * 50 && Pd2Percent(1000, 125) == 1250 && Pd2Percent(-1000, 5) == -50);

// PD2's delay: the death animation's ticks - 6, from 3 to 30; 3 without animation data

// PD2's delay: the death animation's ticks - 6, from 3 to 30; 3 without animation data
constexpr auto SacrificeDelay(bool anim, uint32_t frames, uint32_t speed) noexcept -> int32_t {
	if (!anim || speed == 0) {
		return 3;
	}
	const uint32_t ticks = frames / speed - 6;  // unsigned as PD2's: under 6 ticks wraps, so it takes 30
	return ticks < 3 ? 3 : static_cast<int32_t>(std::min(ticks, 30u));
}

static_assert(SacrificeDelay(true, 20 << 8, 256) == 14 && SacrificeDelay(true, 7 << 8, 256) == 3 &&
              SacrificeDelay(true, 5 << 8, 256) == 30 &&
              SacrificeDelay(true, 99 << 8, 256) == 30 && SacrificeDelay(false, 0, 0) == 3);


auto SacrificeLife(Unit* victim, int32_t absorbed) noexcept -> void {
	g_addLife(victim, absorbed);
	t_lifeUnit = victim;  // the life the loss starts from: PD2's previous_hitpoints
	t_lifeBefore = victim != nullptr ? d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
	                                       victim, StatHitpoints, 0)
	                                 : 0;
}


struct SacrificeHit {
	d2r::LeapKnockback* block;
	int64_t victims;
};

// One unit in range (PD2's 0x10304aa0): a copy of the block through the area apply

// One unit in range (PD2's 0x10304aa0): a copy of the block through the area apply
auto SacrificeBlastOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* hit = static_cast<SacrificeHit*>(area->arg);
	d2r::LeapKnockback damage;
	d2r::CopyRolled(*hit->block, damage);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(d2r::rva::AreaApply)(static_cast<Game*>(area->game),
	                                                                               area->unit, victim, &damage);
	hit->victims += (damage.resultFlags & d2r::rva::ResultHit) != 0 ? 1 : 0;
	d2r::ReleaseDamage(damage);
	return 1;
}

// The event (PD2's 0x10303aa0): the corpse explosion

// The event (PD2's 0x10303aa0): the corpse explosion
auto SacrificeBlast(Game* game, Unit* corpse, int32_t /*type*/, int32_t ownerId, int32_t value) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || corpse == nullptr) {
		return 0;
	}
	const auto packed = static_cast<uint32_t>(value);
	Unit* owner = d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(
		game, static_cast<int32_t>(packed >> 28), ownerId);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, SkillSacrifice);
	int32_t x = 0, y = 0;
	if (owner == nullptr || At<uint32_t>(owner, UnitType) >= 6 || At<int32_t>(corpse, UnitType) != UnitMonster ||
	    skill == nullptr || !d2r::UnitXY(corpse, &x, &y)) {
		return 0;
	}
	int32_t radius = At<int32_t>(skill, SkParam4);  // an owner without the skill: Param4, as PD2's
	void* unitSkill = d2r::Engine<void* (*)(Unit*, int32_t, int32_t) noexcept>(GetUnitSkill)(owner, SkillSacrifice, 0);
	int32_t level = 0;
	if (unitSkill != nullptr) {
		level = d2r::Engine<int32_t (*)(Unit*, void*, int32_t) noexcept>(UnitSkillLevel)(owner, unitSkill, 1);
		radius = d2r::EvalCalc(game, owner, skill, SkCalc3, SkillSacrifice, level);
	}
	uint8_t info[16] = {};
	d2r::Engine<void* (*)(void*, Game*, Unit*) noexcept>(GameMonsterInfo)(info, game, corpse);
	const int32_t expansion = (info[8] != 0 || At<int32_t>(info, 4) != 0) ? 1 : 0;
	const int32_t monLevel = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(corpse, StatLevel, 0);
	int32_t life[32] = {};  // the stats record; flags 1 writes [0] and [1]
	d2r::Engine<int64_t (*)(uint32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t*, int32_t) noexcept>(
		MonLevelStats)(dataFlag, At<int32_t>(corpse, UnitClassId), expansion, info[0], monLevel, 1, life, 0);
	const int32_t overkill = static_cast<int32_t>(packed & OverkillMask);
	const int32_t base = Pd2Percent((life[0] + life[1]) << 7, At<int32_t>(skill, SkParam5));  // average life, 1/256
	const int32_t extra = Pd2Percent(overkill, At<int32_t>(skill, SkParam6));
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(corpse, g_sacrificeState, 0);
	// srvmissilea (sacrificecorpseexplosion): the guts, from the corpse at skill level 1, as PD2's
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileAtXY;
	params.owner = corpse;
	params.origin = corpse;
	params.missile = At<int16_t>(skill, SkSrvMissileA);
	params.x = params.targetX = x;
	params.y = params.targetY = y;
	params.skill = SkillSacrifice;
	params.skillLevel = 1;
	const bool guts = d2r::ValidMissile(dataFlag, params.missile) && d2r::api.createMissile(game, &params) != nullptr;
	d2r::LeapKnockback block;
	block.resultFlags = ResultHit;  // PD2's block: hit flags 0, result 1, the physical damage only
	*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&block) + DmgPhys) = base + extra;
	SacrificeHit hit {&block, 0};
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(corpse), x, y};
	d2r::Engine<AreaFn>(AreaUnits)(game, owner, &center, radius, AreaUnitMask, SacrificeBlastOne, &hit, 0);
	d2r::ReleaseDamage(block);
	return 0;
}

// The deal's kill call (result flag 2): D2R's kill, then PD2's step after the deal

// The deal's kill call (result flag 2): D2R's kill, then PD2's step after the deal
auto SacrificeKill(Game* game, Unit* victim, Unit* attacker, uint64_t block) noexcept -> void {
	using namespace d2r::rva;
	g_unitKill(game, victim, attacker, 1);
	const bool life = t_lifeUnit == victim;  // PD2's stat is 0 on a unit no hit apply has reached
	const int32_t before = life ? t_lifeBefore : 0;
	t_lifeUnit = nullptr;
	// The chain (#2043): an explosion's kill arrives here with the killer as attacker (AreaApply -> DealDamage), so a
	// killer still on Sacrifice queues the next one. A missile kill (D2R passes its owner; skill/sacrifice_missile.cpp
	// keeps the missile) explodes as in PD2's 0x103025c0 only when the missile's skill is Sacrifice and the walked owner
	// (a pet up to its player, 0x10374a60) has the skill: the guts missile of a pet's corpse, never a Blessed Hammer.
	if (!g_killGetsBlock || game == nullptr || victim == nullptr || attacker == nullptr || g_sacrificeState < 0 ||
	    At<int32_t>(attacker, UnitType) == UnitMissile) {
		return;
	}
	Unit* owner = At<int32_t>(attacker, UnitType) == UnitMonster && g_petOwner != nullptr ? g_petOwner(attacker) : nullptr;
	owner = owner != nullptr ? owner : attacker;
	if (Unit* missile = SacrificeKillMissile(); missile != nullptr) {
		const int32_t skillId = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSkill)(missile);
		if (skillId != SkillSacrifice ||
		    d2r::Engine<void* (*)(Unit*, int32_t, int32_t) noexcept>(GetUnitSkill)(owner, SkillSacrifice, 0) == nullptr) {
			return;
		}
	} else {
		void* current = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(attacker);
		if (current == nullptr || UnitSkillIdOf(current) != SkillSacrifice) {
			return;
		}
	}
	const auto* damage = reinterpret_cast<const uint8_t*>(block);
	const int32_t overkill = std::max(At<int32_t>(damage, DmgTotal) - before, 0);
	const int32_t delay = SacrificeDelay(At<void*>(victim, UnitAnimData) != nullptr, At<uint32_t>(victim, UnitAnimFrames),
	                                     At<uint16_t>(victim, UnitAnimSpeed));
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(victim, g_sacrificeState, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(victim, g_sacrificeState, 1);
	// PD2 queues the walked owner as the killer
	const uint32_t value = (static_cast<uint32_t>(overkill) & OverkillMask) |
	                       static_cast<uint32_t>(At<int32_t>(owner, UnitType)) << 28;
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, void*, int32_t, int32_t, int32_t) noexcept>(AddUnitEventCallback)(
		game, victim, UnitEventCallback, At<int32_t>(game, GameFrame) + delay, reinterpret_cast<void*>(&SacrificeBlast),
		At<int32_t>(owner, UnitIdField), static_cast<int32_t>(value), 0);
}


}  // namespace

auto InstallSacrifice(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// Sacrifice's corpse explosion (#1994): the redirect first, then the kill's argument; until the argument patch is
	// in, the redirected kill only calls D2R's.
	g_sacrificeState = StateRow(context, "sacrifice_explosion");
	uint64_t killAt = 0;
	const bool sacrifice =
		g_sacrificeState >= 0 &&
		RedirectCall(context, SacrificeLifeCall, SacrificeLifeCallAt, reinterpret_cast<uint64_t>(&SacrificeLife),
		             reinterpret_cast<uint64_t*>(&g_addLife)) &&
		RedirectCall(context, SacrificeKillCall, SacrificeKillCallAt, reinterpret_cast<uint64_t>(&SacrificeKill),
		             reinterpret_cast<uint64_t*>(&g_unitKill)) &&
		sigscan::ScanUnique(context->exeBase, SacrificeKillCall, 0, &killAt) == 1 &&
		context->PatchBytes(killAt + SacrificeKillArgAt, KillArgOne, sizeof(KillArgOne), KillArgBlock,
		                    sizeof(KillArgBlock));
	g_killGetsBlock = sacrifice;
	plog::Info("sacrifice: corpse explosion %s", sacrifice ? "on"
	                                             : g_sacrificeState < 0
	                                                 ? "FAILED (states.txt has no sacrifice_explosion)"
	                                                 : "FAILED (hit apply life call, deal kill call signature or bytes)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; sacrifice corpse explosion%s", sacrifice ? "" : " FAILED");
	}
	return sacrifice;
}

PD2R_RULE(InstallSacrifice, -50)

