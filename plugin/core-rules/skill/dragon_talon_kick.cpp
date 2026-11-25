// dragon_talon_kick
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

// --- Dragon Talon kick damage (#1862) ------------------------------------------------------------------------------
// PD2 patches 1.13c's Dragon Talon hit builder (D2Game +0x92bb0, srvdo 42's; only Dragon Talon has srvdo 42): at
// +0x92c5a the kick's damage% Param1 + (lvl-1) x Param2 (record +0x148 / +0x14c) becomes calc2 (+0x13c) through
// 0x10302290, and at +0x92c19 the last kick's result flags | 0xc (get-hit + knockback) become | 0x4 (get-hit only).
// D2R 93847's builder 0x572eb0 (from srvdo 42 0x570420) is 1.13c's: Param1/Param2 (+0x1b8 / +0x1bc) into the damage
// block's +0x1c, then the fill 0x573af0 (game, block, attacker, target, skill, level). No SDK service builds a skill
// hit, so the fill call is redirected by signature (calc2 written over the block's damage%) and the OR's imm8 patched.
constexpr const char* KickBuilder =
    "66 89 44 24 44 a8 01 74 ?? 39 9d 50 01 00 00 74 ?? 66 83 c8 ?? 66 89 44 24 44 41 0f b6 8e 06 01 00 00 41 8b d4 "
    "e8 ?? ?? ?? ?? 48 85 c0 74 ?? 85 f6 7e ?? 8d 5e ff 0f af 98 bc 01 00 00 03 98 b8 01 00 00 89 74 24 28 48 8d 54 24 "
    "40 4d 8b cf 44 89 64 24 20 4c 8b c7 89 5c 24 5c 49 8b ce e8";

constexpr uint32_t KickFlagsAt = 0x14, KickFillCallAt = 0x5e;  // the `or ax,0xc` imm8, the fill's E8

constexpr int KickBlockPercent = 0x1c;

using KickFillFn = void (*)(Game*, uint8_t*, Unit*, Unit*, int32_t, int32_t) noexcept;

KickFillFn g_kickFill = nullptr;

// D2R's builder leaves the damage% 0 without a record or at level 0; so does this.

// D2R's builder leaves the damage% 0 without a record or at level 0; so does this.
auto KickFill(Game* game, uint8_t* block, Unit* unit, Unit* target, int32_t skillId, int32_t level) noexcept -> void {
	const uint8_t* skill = game != nullptr && level > 0 ? d2r::api.getSkillRecord(d2r::DataFlag(game), skillId) : nullptr;
	if (skill != nullptr && block != nullptr) {
		const int32_t percent = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc2, skillId, level);
		std::memcpy(block + KickBlockPercent, &percent, sizeof(percent));
	}
	g_kickFill(game, block, unit, target, skillId, level);
}


}  // namespace

auto InstallDragonTalonKick(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// shortcut: if the redirect fails after the flag patch, the flag stays PD2's; undo it if that ever happens.
	static constexpr uint8_t KickKnockback[] = {0x0c}, KickGetHit[] = {0x04};
	uint64_t kickAt = 0;
	const bool kick = sigscan::ScanUnique(context->exeBase, KickBuilder, 0, &kickAt) == 1 &&
	                  context->PatchBytes(kickAt + KickFlagsAt, KickKnockback, sizeof(KickKnockback), KickGetHit,
	                                      sizeof(KickGetHit)) &&
	                  RedirectCall(context, KickBuilder, KickFillCallAt, reinterpret_cast<uint64_t>(&KickFill),
	                               reinterpret_cast<uint64_t*>(&g_kickFill));
	plog::Info("dragon talon: PD2 kick damage calc2, no knockback %s", kick ? "on" : "FAILED (srvdo 42 builder signature or bytes)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; dragon talon kick%s", kick ? "" : " FAILED");
	}
	return kick;
}

PD2R_RULE(InstallDragonTalonKick, -30)

