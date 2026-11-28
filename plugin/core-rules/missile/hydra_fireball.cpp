// Sorceress Hydra fireball pierce: PD2's srvhit 1 kill rule, for the Hydra's fireball only (#2228).
#include "shared.h"

#include "../skill/registry.h"
#include "sig.h"

namespace {

constexpr int StockSlot = 1;                // srvhit 1; slots 61-70, the free ones, are all taken
constexpr int16_t SkillHydra = 62;          // the Sorceress Hydra, hydrafireball's Skill column
constexpr int32_t PierceResult = 6;         // PD2's: deal the missile's hit damage (2), keep flying (4)

MissileSrvHitFn g_stock = nullptr;  // D2R's srvhit 1: the explosion, result 1 (die)

// srvhit 1 (PD2 ProjectDiablo.dll 0x103a75c0): D2R's explosion, then on a unit hit with CollideKill 0 PD2's result 6, so
// the fireball passes through and explodes on each unit. Only rows whose Skill is Hydra get it; the other srvhit 1
// rows keep D2R's rule (ringoffire is D2R's own Warlock skill), per the owner (#2228).
auto SrvHitExplode(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	const int32_t result = g_stock(game, missile, hit);
	if (game == nullptr || missile == nullptr || hit == nullptr) {
		return result;
	}
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::MissileRecord)(
	    d2r::DataFlag(game), At<int32_t>(missile, d2r::rva::UnitClassId));
	const bool pierce = row != nullptr && At<int16_t>(row, d2r::rva::MsSkill) == SkillHydra &&
	                    At<uint8_t>(row, d2r::rva::MsCollideKill) == 0;
	return pierce ? PierceResult : result;
}

}  // namespace

auto InstallHydraFireball(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t slot = d2r::rva::MissileSrvHitTable + StockSlot * 8;
	const uint64_t stock = *reinterpret_cast<const uint64_t*>(context->exeBase + slot);
	g_stock = reinterpret_cast<MissileSrvHitFn>(stock);  // set before the slot points at SrvHitExplode
	const bool ok = stock != 0 && d2r::sig::MissileRecord.state == sigscan::State::Ok &&
	                context->PatchWriteU64(slot, &stock, sizeof(stock), reinterpret_cast<uint64_t>(&SrvHitExplode));
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; hydra fireball pierce%s", ok ? "" : " FAILED");
	}
	return ok;
}

PD2R_RULE(InstallHydraFireball, 450)
