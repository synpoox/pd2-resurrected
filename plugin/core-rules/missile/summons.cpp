// Summon-on-hit missiles (shit 67) and Rathma's bone spear (dmg 18).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_summonHits {0};
std::atomic<uint32_t> g_spearHits {0};


}  // namespace missiles

// --- Summon-on-hit missiles (PD2 pSrvHitFunc 6, here 67; docs/research/skills/w36-monster-kits.md) -------------------------
// Read from ProjectDiablo.dll: shit 6 at 0x103a7750. D2R's own srvhit 6 (0x45ce90) spawns the same monster the same
// way but leaves out the flags, so the spawns would drop loot and give experience.
namespace {
}  // namespace

// The monstats row sHitPar1 at the missile, in mode sHitPar2 (1 when 16 or more), with sHitPar3 added to its flags
// (PD2's rows: no treasure class and no experience). Always 1, as both games: the missile deals no damage here.
auto SrvHitSummon(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	(void)hit;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	if (row == nullptr) {
		return 1;
	}
	const int32_t mon = At<int32_t>(row, MsHitPar1);
	if (mon < 0 || d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(dataFlag, mon) == nullptr) {
		return 1;
	}
	const auto par2 = At<uint32_t>(row, MsHitPar2), flags = At<uint32_t>(row, MsHitPar3);
	const int32_t mode = par2 < 16 ? static_cast<int32_t>(par2) : 1;
	int32_t x = 0, y = 0;
	if (!d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	Unit* spawn = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
		SpawnMonsterAt)(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(missile), mon, x, y, mode, 0);
	if (spawn != nullptr && flags <= 0x7fffffff) {  // PD2 adds nothing for a negative sHitPar3
		*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(spawn) + UnitMainFlags) |= flags;
	}
	++g_summonHits;
	return 1;
}

// --- Rathma's bone spear (PD2 dmg 18, ProjectDiablo.dll 0x103a6fd0; UB5, #477) ------------------------------------------
namespace {


}  // namespace

// dmg 18: besides its own damage, the spear deals a second hit of DmgCalc1 percent of the target's max life, from the
// missile's owner with hit flags 0x1000 (PD2 builds a fresh block and runs 1.13c's hit events 0x10305990 with area 0
// and its result 0x103025b0). PD2 writes the full share to the block's magic (+0x20) and min(share, life) to its total
// (+0x4c): with area 0 the resist step that sums the total never runs, so the target loses the total as it stands, with
// no magic resist. The missile's own block is left as it is.
// D2R native calls: PluginSDK has no damage service, so this calls D2R's HitEvents and DealDamage. PD2 runs no
// crit step here, so D2R's DamageStep (0x5889e0, the monster crit) is not called.
// PD2 also puts the missile in the block (+0x5c): 0x103025b0 reads it only for a kill by skill 96's missile, not this
// one, and D2R's block keeps the hit's x/y there (0x13c, 0x140, read by DealDamage), so the port leaves it out.
constexpr uint32_t HitNoRoll = 0x1000;  // 1.13c's hit flag the spear's block carries (no to-hit roll)

void SrvDmgRathmaSpear(Game* game, Unit* missile, Unit* target, d2r::Damage* damageBlock) noexcept {
	auto* damage = reinterpret_cast<uint8_t*>(damageBlock);  // the block is opaque in the toolkit; its fields are read by offset
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || target == nullptr || damage == nullptr) {
		return;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (row == nullptr || owner == nullptr) {
		return;
	}
	using EvalFn = int32_t (*)(uint32_t, Unit*, Unit*, int32_t, int32_t, int32_t) noexcept;
	const int32_t percent =
		d2r::Engine<EvalFn>(EvalMissileCalc)(dataFlag, missile, owner, At<int32_t>(row, MsDmgCalc1), -1, -1);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto life = static_cast<uint32_t>(unitStat(target, StatLife, 0));
	const auto maxLife = static_cast<uint32_t>(unitStat(target, StatMaxLife, 0));
	const double share = static_cast<double>(percent) / 100.0 * static_cast<double>(maxLife);  // PD2's doubles
	// PD2's 0x103b8a40: the double to uint32, negatives to 0, past 2^32 - 1 to 2^32 - 1
	constexpr double UintMax = 4294967295.0;
	const uint32_t magic = share <= 0.0 ? 0 : share >= UintMax ? UINT32_MAX : static_cast<uint32_t>(share);
	const uint32_t total = magic < life ? magic : life;
	d2r::DamageBlock hit;
	hit.hitFlags = HitNoRoll;
	// zeroed, as PD2's block: without the hit bit neither game fires on-hit events 5 and 1 (1.13c 0x10305990 and D2R's
	// HitEvents 0x44ce80 both need result & 0x21 == 1), and HitEvents still takes the total from life
	hit.resultFlags = 0;
	auto* block = reinterpret_cast<uint8_t*>(&hit);
	*reinterpret_cast<uint32_t*>(block + DmgMagic) = magic;
	*reinterpret_cast<uint32_t*>(block + DmgTotal) = total;
	using HitFn = void (*)(Game*, Unit*, Unit*, void*) noexcept;
	// area 0, as PD2: D2R's HitEvents then skips DamageEffects (the resists and the total), as 1.13c skips its step
	d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept>(HitEvents)(game, owner, target, 0, &hit);
	d2r::Engine<HitFn>(DealDamage)(game, owner, target, &hit);
	d2r::ReleaseDamage(hit);
	++g_spearHits;
}
