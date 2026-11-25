// revive_events
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

namespace pd2r::skills {

// --- Revive's aura event (PD2 auraeventfunc 32, ProjectDiablo.dll 0x10347370) ---------------------------------------
// Revive's row has auraevent1 domeleedamage: each melee hit of a revived monster runs proc_SplashDamage's do function
// (PD2 358, merged 430) at level 1 from it, called directly as PD2 does. PD2's splash finds the struck unit on the
// revive's path; this passes the unit the hit landed on.
std::atomic<uint32_t> g_reviveEvents {0};

std::atomic<uint32_t> g_reviveSplashes {0};


}  // namespace pd2r::skills

namespace {

auto AuraEventReviveSplash(Game* game, int32_t, Unit* unit, Unit* target, void* damage, int32_t,
                           int32_t) noexcept -> int32_t {
	(void)damage;
	if (t_splashing) {
		return 0;  // PD2 skips this handler for a splash's own hits
	}
	++g_reviveEvents;
	if (game == nullptr || unit == nullptr || target == nullptr) {
		return 0;
	}
	const auto cast = static_cast<int32_t>(Splash(game, unit, target, ProcSplashDamage, 1));
	g_reviveSplashes.fetch_add(cast != 0 ? 1 : 0);
	return cast != 0 ? 1 : 0;
}

// --- Splash on Whirlwind and Blade Dance hits (#1812) ---------------------------------------------------------------
// Whirlwind's start (srvst 38, 0x56a420 via 0x48e820) sets state uninterruptable for the whole spin, and D2R's
// skill-on-hit casts refuse an attacker in it (0x5896e0, which D2R's item event 20, 0x583b30, casts through). D2RCore
// dispatches item events itself and refuses there too: in game no item_splashonhit splash came on a Whirlwind hit, and
// a hook on 0x583b30 never ran (D2RCore does not call it). 1.13c has the same refusal (D2Game 0x6fd117e0), and PD2
// removes it: its patch list (0x1049c640: D2Game +0xf181d, 0x33) turns the TEST EAX,EAX after the state 54 check into
// XOR EAX,EAX, for every attacker (player, merc, monster). Its skill-on-hit (0x103473a0) casts through 0x6fd117e0 for
// skills whose ItemTarget is not 5 (splash's is 0). So on any melee hit (event 5, relayed by item_rules' HitEvent) of
// an attacker in state 54 each splash node rolls its chance as 0x583b30 does and the splash is called on the struck
// unit, as Revive's event does. The damage% is PD2's (0x10305eb0, players only: Whirlwind 151, Blade Dance PD2 380 =
// merged 452 take calc1; mercs and monsters, MonWhirlwind 495 and UberTalicWhirlwind 579 included, 0 on Attack).
// The other skill-on-hit nodes (item_skillonhit 198, #1889) are cast as 0x583b30 casts them, through 0x5896e0's body
// past its state check: 0x589930 picks the target (ItemTarget, skill +0x178) and 0x5390d0 sends the cast, with unit flag
// 0x10000000 on the caster around it. Skipped: a skill with ItemTgtDo (skill +0x27 bit 0x20), which 0x583b30 casts from
// the struck unit, so no spin refuses it; and ItemTarget 5 (Nova, Frost Nova), which PD2 casts at the caster's target
// point through a path that keeps the state 54 refusal.

}  // namespace

auto InstallReviveAuraEvent(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// Aura event 32 is filled by D2R but no D2R row uses it, so the expected value is D2R's own function, not null.
	const uint64_t d2rEvent = d2r::imageBase + d2r::rva::AuraEvent32D2R;
	uint8_t expected[8];
	std::memcpy(expected, &d2rEvent, sizeof(expected));
	const bool revive = context->PatchWriteU64(
		d2r::rva::AuraEventTable + static_cast<uint64_t>(d2r::rva::AuraEventUnused) * 8, expected, sizeof(expected),
		reinterpret_cast<uint64_t>(static_cast<AuraEventFn>(AuraEventReviveSplash)));
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; auraeventfunc %d revive splash%s",
		                            d2r::rva::AuraEventUnused, revive ? "" : " FAILED (slot is not D2R's event 32)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return revive;
}

PD2R_RULE(InstallReviveAuraEvent, -120)

auto InstallReviveDo(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	uint8_t expected[8];
	// Revive's do is D2R's: the expected value is D2R's function, which the wrapper calls first.
	const uint64_t d2rRevive = d2r::imageBase + d2r::rva::SrvDoReviveD2R;
	std::memcpy(expected, &d2rRevive, sizeof(expected));
	const bool reviveDo = context->PatchWriteU64(
		d2r::rva::SrvDoTable + static_cast<uint64_t>(d2r::rva::SrvDoRevive) * 8, expected, sizeof(expected),
		reinterpret_cast<uint64_t>(static_cast<SrvDoFn>(SrvDoRevive)));
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; srvdofunc %d revive events%s",
		                            d2r::rva::SrvDoRevive, reviveDo ? "" : " FAILED (slot is not D2R's Revive)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return reviveDo;
}

PD2R_RULE(InstallReviveDo, -100)

