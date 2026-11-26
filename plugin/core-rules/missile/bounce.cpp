// Double Throw's bouncing missiles and Psychic Hammer's chain falloff (shit 66, dmg 21, dmg 22).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_bounces {0};
std::atomic<uint32_t> g_firstHits {0};
std::atomic<uint32_t> g_chainFalloffs {0};


}  // namespace missiles

// --- Double Throw's missiles (PD2 pSrvHitFunc 30 -> 66, pSrvDmgFunc 16 -> 21) ------------------------------------
namespace {


using MissileIntFn = int32_t (*)(Unit*) noexcept;

auto MissileSkillRecord(Game* game, Unit* missile, int32_t* skillId, int32_t* level) noexcept -> const uint8_t* {
	*skillId = d2r::Engine<MissileIntFn>(d2r::rva::MissileSkill)(missile);
	*level = d2r::Engine<MissileIntFn>(d2r::rva::MissileLevel)(missile);
	return d2r::api.getSkillRecord(d2r::DataFlag(game), *skillId);
}

}  // namespace

// shit 66 (PD2 30, ProjectDiablo.dll 0x103a7a50): D2R's Chain Lightning hit (srvhit 12) as PD2 changed it. While the
// missile has bounces left, throw the same missile from it at the nearest other target within sHitPar1 or the skill's
// aurarangecalc (no minimum of 1, unlike srvhit 12), with one bounce less, and set damagepercent = calc1 on the
// missile that hit (PD2 sets it there, not on the new one).
// shortcut: PD2 also never bounces while the owner is in PD2's PvP maps, cut.
auto SrvHitBounce(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || hit == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (row == nullptr || owner == nullptr) {
		return 1;
	}
	int32_t skillId = 0, level = 0;
	const uint8_t* skill = MissileSkillRecord(game, missile, &skillId, &level);
	const int32_t bounces = d2r::Engine<MissileIntFn>(MissileBounces)(missile);
	int32_t x = 0, y = 0, tx = 0, ty = 0;
	if (skill == nullptr || bounces <= 1 || !d2r::UnitXY(missile, &x, &y)) {
		return 3;
	}
	int32_t range = At<int32_t>(row, MsHitPar1);
	if (range < 1) {
		range = d2r::EvalCalc(game, owner, skill, SkAuraRangeCalc, skillId, level);
	}
	using NearestFn = Unit* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
	Unit* target = d2r::Engine<NearestFn>(NearestTarget)(game, owner, x, y, range, NearestTargetFlags,
	                                                       At<uint32_t>(hit, UnitIdField), nullptr);
	if (target == nullptr || target == hit || !d2r::UnitXY(target, &tx, &ty)) {
		return 3;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;  // start at x/y, as srvhit 12 sets it
	params.owner = owner;
	params.target = target;
	params.missile = At<int32_t>(missile, UnitClassId);
	params.x = x;
	params.y = y;
	params.targetX = tx;
	params.targetY = ty;
	params.skill = skillId;
	params.skillLevel = level;
	params.sourceKey = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(missile);  // srvhit 12 sets it too
	Unit* next = d2r::api.createMissile(game, &params);
	if (next == nullptr) {
		return 3;
	}
	const int32_t damage = d2r::EvalCalc(game, owner, skill, SkCalc1, skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(missile, StatDamagePercent, damage, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(next, bounces - 1);
	++g_bounces;
	return 3;
}

// dmg 21 (PD2 16, 0x103a6da0): while the missile still has all calc2 bounces (its first hit), raise the physical,
// fire, lightning, magic, cold and poison damage by calc3 percent. PD2's rows have calc3 = par4 = 0, so it adds nothing.
// shortcut: the fire to poison offsets assume 1.13c's +0x10 shift seen on phys and poison length (rvas_93847.h);
// read a D2R srvdmg function that writes them when a row gives calc3 a value.
void SrvDmgFirstHit(Game* game, Unit* missile, Unit* hit, d2r::Damage* damageBlock) noexcept {
	auto* damage = reinterpret_cast<uint8_t*>(damageBlock);  // the block is opaque in the toolkit; its fields are read by offset
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || hit == nullptr || damage == nullptr) {
		return;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	int32_t skillId = 0, level = 0;
	const uint8_t* skill = MissileSkillRecord(game, missile, &skillId, &level);
	if (owner == nullptr || skill == nullptr) {
		return;
	}
	const int32_t bounces = d2r::EvalCalc(game, owner, skill, SkCalc2, skillId, level);
	const int32_t bonus = d2r::EvalCalc(game, owner, skill, SkCalc3, skillId, level);
	if (bounces != d2r::Engine<MissileIntFn>(MissileBounces)(missile) || bonus <= 0) {
		return;
	}
	for (int field : {DmgPhys, DmgLight, DmgCold, DmgFire, DmgMagic, DmgPoison}) {
		auto* value = reinterpret_cast<uint32_t*>(damage + field);
		*value = *value * static_cast<uint32_t>(bonus + 100) / 100;  // PD2's unsigned multiply and divide
	}
	++g_firstHits;
}

// --- Psychic Hammer (PD2 pSrvDmgFunc 15 -> 22; docs/research/skills/w31-dragon-flight-psychic-hammer.md) -----------------
namespace {


// PD2 dmg 15's cut: calc2 percent per hit already made, (calc1 - bounces left) of them; at 100% or more, none left.
// PD2 multiplies in 32 bits; 64 here, the same below 2^31.
constexpr auto ChainFalloffMagic(int32_t magic, int32_t hits, int32_t bounces, int32_t step) noexcept -> int32_t {
	const int64_t cut = static_cast<int64_t>(hits - bounces) * step;
	return cut > 99 ? 0 : static_cast<int32_t>((100 - cut) * magic / 100);
}
static_assert(ChainFalloffMagic(1000, 3, 3, 20) == 1000);  // the first hit: no cut
static_assert(ChainFalloffMagic(1000, 3, 1, 20) == 600);   // the third: 40% less
static_assert(ChainFalloffMagic(1000, 6, 1, 20) == 0);     // the sixth: 100%, nothing left
static_assert(ChainFalloffMagic(999, 2, 1, 20) == 799);    // truncated

}  // namespace

// dmg 22 (PD2 15, ProjectDiablo.dll 0x103a6c90, Psychic Hammer): each unit the chain hits after the first takes calc2
// percent less magic damage per earlier hit, counted as calc1 (the skill's hits) - the missile's bounces left (D2Common
// #11018, D2R's MissileBounces). D2R's collision (0x4639a0) calls it when srvhit 12 returns 3 (bit 2: deal the damage).
void SrvDmgChainFalloff(Game* game, Unit* missile, Unit* target, d2r::Damage* damageBlock) noexcept {
	auto* damage = reinterpret_cast<uint8_t*>(damageBlock);  // the block is opaque in the toolkit; its fields are read by offset
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || target == nullptr || damage == nullptr) {
		return;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	int32_t skillId = 0, level = 0;
	const uint8_t* skill = MissileSkillRecord(game, missile, &skillId, &level);
	if (owner == nullptr || skill == nullptr) {
		return;
	}
	const int32_t hits = d2r::EvalCalc(game, owner, skill, SkCalc1, skillId, level);
	const int32_t bounces = d2r::Engine<MissileIntFn>(MissileBounces)(missile);
	const int32_t step = d2r::EvalCalc(game, owner, skill, SkCalc2, skillId, level);
	auto* magic = reinterpret_cast<int32_t*>(damage + DmgMagic);
	const int32_t before = *magic;
	*magic = ChainFalloffMagic(before, hits, bounces, step);
	++g_chainFalloffs;
}
