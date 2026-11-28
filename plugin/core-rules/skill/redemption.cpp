// redemption
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

// --- Redemption skips owned corpses (#1987) --------------------------------------------------------------------------
// PD2 redirects the D2Common #10501 corpse test in 1.13c's Redemption callback (D2Game +0x98680, passed by srvdo 82 at
// +0x9a5a8; call at +0x986d8) to ProjectDiablo.dll 0x102fb290 (PD2.gpr 0x102fb280), which returns 0 when the corpse's
// owner id (+0x98) is set and its owner type (+0x94) is 0. D2R's srvdo 82 (0x564260) passes the callback 0x565e60,
// which calls the per-corpse worker 0x560d90 (its corpse test 0x33a5c0 has no owner test); that call is redirected.
// The worker's other callers (missile srvdo 19, radamentdeath) keep D2R's. No SDK service covers it.
constexpr const char* RedemptionCorpseCall = "8b 43 04 44 8b 0b 89 44 24 20 e8 ?? ?? ?? ?? 85 c0 74 03 ff 43 0c";

constexpr uint32_t RedemptionCorpseCallAt = 10;

using RedemptionCorpseFn = int32_t (*)(Game*, Unit*, Unit*, int32_t, int32_t, int32_t) noexcept;

RedemptionCorpseFn g_redemptionCorpse = nullptr;

// Ownership IDs survive death; the shared helper reads AI fields only on server units.
auto RedemptionCorpse(Game* game, Unit* unit, Unit* corpse, int32_t skillId, int32_t level, int32_t flag) noexcept
    -> int32_t {
	const bool owned = corpse != nullptr && OwnedByPlayer(corpse);
	const int32_t taken = owned ? 0 : g_redemptionCorpse(game, unit, corpse, skillId, level, flag);
	return taken;
}

// --- Mind Blast's stacks and damage only (#1946) ---------------------------------------------------------------------
// PD2's srvdo 51 (ProjectDiablo.dll 0x103af800) and cltdo 8 (0x103aaf10). The server reads `had`, aurastat1 on the
// caster's aurastate list (a new list starts at 0), raises it by 1 while under calc2, sets the list's life to
// max(had, 1) x auralencalc frames and searches around the target; its per-unit callback (0x10304a90) only deals the
// copied damage block (0x10305760). The client draws the blast missile at the radius from its own copy of the list.
// The radius follows PD2's published numbers (wiki: +1.33 / 2.66 / 4 yards at 1 / 2 / 3+ stacks): aurarangecalc +
// the count after this cast's raise x calc1. The DLL as decoded uses the count before the raise, (max(had, 1) - 1)
// x calc1 (docs/deviations.md).
// D2R 93847's srvdo 51 (0x570780) takes its radius from the call at 0x570801 (prgcalc by D2R's progressive flag, which
// Mind Blast lacks, so 0 and then aurarangecalc) and never stacks; its cltdo 8 (0x2b3020) evaluates aurarangecalc at
// 0x2b30b9; its per-unit callback (0x56e590, used by srvdo 51 alone) first rolls vanilla's convert (0x5727e0, called
// only there: calc1 % for calc2 + rand(Param4) frames, the monster then taking no damage). The radius calls are
// redirected and the convert call becomes `xor eax,eax` (not converted: the damage follows), all by signature: no SDK
// service reaches a skill's area radius or its per-unit step.

}  // namespace

auto InstallRedemption(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	const bool redemption = RedirectCall(context, RedemptionCorpseCall, RedemptionCorpseCallAt,
	                                     reinterpret_cast<uint64_t>(&RedemptionCorpse),
	                                     reinterpret_cast<uint64_t*>(&g_redemptionCorpse));
	plog::Info("redemption: skip owned corpses %s", redemption ? "on" : "FAILED (srvdo 82 callback signature)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; redemption skips owned corpses%s", redemption ? "" : " FAILED");
	}
	return redemption;
}

PD2R_RULE(InstallRedemption, -40)

