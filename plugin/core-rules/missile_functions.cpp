// Split Throw's return missiles: PD2 missile pSrvHitFunc 65 on the thrown rows, pSrvDoFunc 38 / pCltDoFunc 70 on the
// <name>return rows. D2R fills sdo 38 and cdo 70, so ours sit in sdo 46 / cdo 78 and the data points there.
//
// W6 adds Exploding Arrow's spread (shit 63) and Multiple Shot's pierce falloff (PD2 dmg 17, here 20), both read from
// ProjectDiablo.dll (docs/research/skills/amazon-w6.md).
//
// The server calls missile srvdo as int64_t fn(Game*, Unit* missile) every frame, srvhit as
// int32_t fn(Game*, Unit* missile, Unit* hit) on a collision, srvdmg as void fn(Game*, Unit* missile, Unit* target,
// Damage*) before a hit's damage is applied, and the client missile cltdo as void fn(Unit* missile, float) (the
// toolkit's skill_router.h types).
#include "missile/shared.h"

auto MissileFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "returns spawned %u, caught %u, bounces %u, first hits %u, spread hits %u, explosions %u, "
	              "falloff hits %u, state hits %u, bolt hits %u, holy light client hits %u, fist delay client hits %u, "
	              "charge explodes %u, charge bolt hits %u, hammer frames %u, hits %u, kept %u, client frames %u, "
	              "chain falloffs %u, fragment walls %u, splits %u, damage %u, client walls %u, summon hits %u, rathma spear hits %u, "
	              "potion hits %u, potion client hits %u, holy bolt heals %u",
	              g_returnSpawns.load(), g_returnsCaught.load(), g_bounces.load(), g_firstHits.load(),
	              g_spreadHits.load(), g_spreadExplosions.load(), g_falloffHits.load(), g_stateHits.load(),
	              g_boltHits.load(), g_holyLightHits.load(), g_fistDelayHits.load(), g_chargeExplodes.load(),
	              g_chargeBoltHits.load(), g_hammerFrames.load(), g_hammerHits.load(), g_hammerKept.load(),
	              g_hammerClientFrames.load(), g_chainFalloffs.load(), g_fragmentWalls.load(), g_fragmentSplits.load(),
	              g_fragmentDamage.load(), g_fragmentClientWalls.load(), g_summonHits.load(), g_spearHits.load(),
	              g_potionHits.load(), g_potionClientHits.load(), g_holyBoltHeals.load());
}
