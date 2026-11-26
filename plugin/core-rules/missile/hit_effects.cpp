// State on hit (shit 61), bolt splash (shit 62) and the uber immolation arrow (shit 64).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_stateHits {0};
std::atomic<uint32_t> g_boltHits {0};


}  // namespace missiles

// --- W11: state on hit (Poison Dagger's cloud, the throwing potions' clouds, Kanemith's blaze dot) ---------------------
namespace {


}  // namespace

// shit 61 (PD2 0x103a84f0, through 0x1036c920): the hit unit gets state sHitPar1 for sHitPar2 frames, carrying stat
// sHitPar3 = cHitPar1 and stat cHitPar2 = cHitPar3 (a stat of -1 or none is left out), from the missile row's Skill at
// the missile's level. Returns PD2's 2 + CollideKill: D2R's collision (0x4639a0) deals the missile's damage only when
// the result has bit 2, so a 0 or 1 (no hit unit or owner, a bad row) deals none.
// shortcut: PD2 also halves a negative resist (stats 36-45) on a unit already over 99 in it, and treats states 16, 87
// and 188 specially; none of shit 61's rows reach those states, and the halving is an approximated row on
// docs/deviations.md. Upgrade if a resist-lowering row must match PD2 on immune monsters.
auto SrvHitState(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	if (hit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	if (owner == nullptr) {
		return 0;
	}
	if (row == nullptr || tables == nullptr) {
		return 1;
	}
	const int32_t damage = 2 + (At<uint8_t>(row, MsCollideKill) != 0);  // PD2's result: 2 deals the hit's damage
	const int32_t state = At<int32_t>(row, MsHitPar1), frames = At<int32_t>(row, MsHitPar2);
	if (state < 0 || static_cast<uint64_t>(state) >= At<uint64_t>(tables, TablesStateCount) || frames <= 0) {
		return 1;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	// No RemoveSkillState: like PD2's timed-state helper (0x1036d7d0), NewStateList extends a same-level list and
	// refuses a lower level, so a weaker cloud never replaces a stronger one.
	d2r::StateListRequest request {owner, hit, skillId, level, frames, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return damage;
	}
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const int32_t stats[2][2] = {{At<int32_t>(row, MsHitPar3), At<int32_t>(row, MsCltHitPar1)},
	                             {At<int32_t>(row, MsCltHitPar2), At<int32_t>(row, MsCltHitPar3)}};
	for (const auto& [stat, value] : stats) {
		if (stat >= 0) {  // the merge gives these the mod's itemstatcost ids (pd2_port.HIT_PARAMS)
			setStat(dataFlag, list, stat, value, 0);
		}
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(hit, state, 1);
	++g_stateHits;
	return damage;
}

// --- Bolt splash (PD2 shit 62, W28, docs/research/skills/w28-sorceress-bolts.md) -------------------------------------------
namespace {


using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*), void*,
                        int32_t) noexcept;  // rva::AreaUnits

struct BoltSplash {
	d2r::LeapKnockback* rolled;  // the one roll every unit in range takes
	uint32_t hitFlags, resultFlags;
	int64_t victims;
};

// One unit in range (PD2's callback 0x10304a90): a copy of the roll through the area apply.
auto BoltSplashOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* b = static_cast<BoltSplash*>(area->arg);
	d2r::LeapKnockback damage;
	d2r::CopyRolled(*b->rolled, damage);
	damage.hitFlags = b->hitFlags;
	damage.resultFlags = b->resultFlags;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(d2r::rva::AreaApply)(static_cast<Game*>(area->game),
	                                                                               area->unit, victim, &damage);
	b->victims += (damage.resultFlags & d2r::rva::ResultHit) != 0 ? 1 : 0;
	ReleaseDamage(damage);
	return 1;
}

}  // namespace

// shit 62 (PD2 0x103a86d0; Fire Bolt, Ice Bolt, Blizzard, Chilling Armor's bolt): wherever the missile hits, a unit or
// not, every unit within sHitPar1 of the missile takes the missile row's Skill's elemental damage at the missile's
// level + sHitPar2, rolled once, from the owner with PD2's unit mask, the row's HitFlags and ResultFlags | hit. Returns
// 1 as PD2 does, so the collision deals no damage of its own: the struck unit takes the splash.
// shortcut: PD2 also adds the skill's physical damage (1.13c D2Game 0x9f1a0) and puts the missile in the block
// (1.13c +0x5c); none of shit 62's skills has MinDam/MaxDam. Upgrade if a physical row joins shit 62.
auto SrvHitBoltSplash(Game* game, Unit* missile, Unit* /*hit*/) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	int32_t x = 0, y = 0;
	if (row == nullptr || owner == nullptr || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile) + At<int32_t>(row, MsHitPar2);
	const int32_t radius = At<int32_t>(row, MsHitPar1);
	d2r::LeapKnockback rolled;
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(owner, &rolled, skillId, level);
	BoltSplash b {&rolled, At<uint32_t>(row, MsHitFlags), At<uint16_t>(row, MsResultFlags) | uint32_t {ResultHit}, 0};
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(missile), x, y};
	d2r::Engine<AreaFn>(AreaUnits)(game, owner, &center, radius, AreaUnitMask, BoltSplashOne, &b, 0);
	++g_boltHits;
	ReleaseDamage(rolled);
	return 1;
}

// --- Uber immolation arrow (PD2 shit 64, UB4: the trapped souls' UberDiabloBoulder) --------------------------------
namespace {


}  // namespace

// shit 64 (PD2 0x103a8ca0): D2R's Immolation Arrow hit (srvhit 9, 0x45d020) as PD2 changed it. HitSubMissile1 burns at
// every open spot within sHitPar1 of the missile (0: the skill's calc1, at least 1) for SHitCalc1 frames; then every
// unit within sHitPar2 (0: the skill's aurarangecalc, not clamped) takes the missile's damage with the row's HitFlags
// and ResultFlags. Returns 1, so the struck unit takes no damage of its own. PD2's UberDiabloBoulder has no
// aurarangecalc, so the radius is 0, D2R's area walk (0x4398b0) skips it, and only the fire disc does damage.
// srvhit 9 also clears the missile's cold length and freeze before the disc (stats 56, 134), clamps the damage radius
// to 1 and reads calc2 for it, and returns 3; PD2's leaves all four out.
auto SrvHitUberImmolation(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	if (row == nullptr) {
		return 1;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t x = 0, y = 0;
	if (skill == nullptr || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	const int32_t sub = At<int16_t>(row, MsHitSubMissile1);
	int32_t discRadius = 0, frames = 0;
	if (sub >= 0) {
		discRadius = At<int32_t>(row, MsHitPar1);
		if (discRadius < 1) {
			discRadius = owner == nullptr ? 0 : d2r::EvalCalc(game, owner, skill, SkCalc1, skillId, level);
			discRadius = discRadius < 1 ? 1 : discRadius;
		}
		using EvalFn = int32_t (*)(uint32_t, Unit*, Unit*, int32_t, int32_t, int32_t) noexcept;
		frames = d2r::Engine<EvalFn>(EvalMissileCalc)(dataFlag, missile, owner, At<int32_t>(row, MsHitCalc1), -1, -1);
		frames = frames < 0 ? 0 : frames;
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t) noexcept>(MissileFireDisc)(game, missile, discRadius,
		                                                                                     sub, frames);
	}
	int32_t radius = At<int32_t>(row, MsHitPar2);
	if (radius < 1 && owner != nullptr) {
		radius = d2r::EvalCalc(game, owner, skill, SkAuraRangeCalc, skillId, level);
	}
	if (owner != nullptr && radius >= 1) {  // D2R's area walk does nothing below 1, as 1.13c's does
		d2r::LeapKnockback damage;
		damage.resultFlags = 0;
		d2r::Engine<void (*)(Game*, Unit*, Unit*, d2r::LeapKnockback*) noexcept>(MissileDamageFill)(game, missile, hit,
		                                                                                         &damage);
		damage.hitFlags |= At<uint32_t>(row, MsHitFlags);
		damage.resultFlags |= At<uint16_t>(row, MsResultFlags);
		// srvhit 9 lends the owner the missile's state key for the area hit, then puts the owner's back
		const auto key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey);
		const auto setKey = d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitStateKey);
		const int32_t ownerKey = key(owner);
		setKey(owner, key(missile));
		uint8_t scratch[0x20] {};  // as srvhit 9 passes it: only its byte +0x18 (0) is read
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, d2r::LeapKnockback*, int32_t, void*) noexcept>(
			AreaKnockback)(game, owner, x, y, radius, &damage, 0, scratch);
		setKey(owner, ownerKey);
		ReleaseDamage(damage);
	}
	return 1;
}
