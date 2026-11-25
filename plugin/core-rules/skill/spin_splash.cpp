// spin_splash
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
constexpr int32_t StatSplashOnHit = 412, StatMapMonSplash = 480;  // item_splashonhit, map_mon_splash (merged)

constexpr int32_t StatSkillOnHit = 198;                            // item_skillonhit

constexpr int EventNodeLayer = 0x10;                               // the node's stat layer (seen in game: 412, 0x6b81)

constexpr int SkItemTgtDo = 0x27, SkItemTarget = 0x178;            // 93847 skill record: flag byte (bit 0x20), uint8

constexpr uint8_t ItemTgtDoBit = 0x20;

constexpr uint8_t ItemTargetAimPoint = 5;

constexpr int32_t UnitFlagItemEventCast = 0x10000000;

sigscan::Entry ItemSkillTarget {"ItemSkillTarget", "spin", sigscan::Kind::Code, 0, 0,
                                "44 89 4c 24 20 53 55 56 57 41 55 41 57"};  // 93847 0x589930

sigscan::Entry ItemSkillSend {"ItemSkillSend", "spin", sigscan::Kind::Code, 0, 0,
                              "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 20 4c 8b f1 "
                              "41 8b f1 48 8d 0d ?? ?? ?? ?? 41 8b f8"};  // 93847 0x5390d0

sigscan::Entry* const SpinOwn[] = {&ItemSkillTarget, &ItemSkillSend};

bool g_castOnHit = false;  // both signatures found at install


auto Spinning(Unit* unit) noexcept -> bool {
	return d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::UnitHasState)(unit, Uninterruptable) != 0;
}

// 0x5896e0 without its state 54 refusal (PD2's patch of 1.13c 0x6fd117e0): 1 when the skill was cast at the struck unit

// 0x5896e0 without its state 54 refusal (PD2's patch of 1.13c 0x6fd117e0): 1 when the skill was cast at the struck unit
auto CastOnHit(Game* game, Unit* unit, Unit* struck, int32_t skillId, int32_t level) noexcept -> int64_t {
	using TargetFn = uint64_t (*)(Game*, Unit*, int32_t, int32_t, Unit*, int32_t, int32_t, int32_t*, uint32_t*, int32_t*,
	                              int32_t*, int32_t) noexcept;
	using SendFn = void (*)(Unit*, int32_t, int32_t, int32_t, uint32_t, int32_t) noexcept;
	using FlagFn = void (*)(Unit*, int32_t, int32_t) noexcept;
	int32_t type = 6, x = 0, y = 0;  // 6: no target picked, cast at the struck unit
	uint32_t id = 0xffffffff;
	d2r::Engine<FlagFn>(d2r::sig::SetUnitFlag)(unit, UnitFlagItemEventCast, 1);
	const bool cast = static_cast<int32_t>(d2r::Engine<TargetFn>(ItemSkillTarget)(game, unit, skillId, level, struck, 0,
	                                                                              0, &type, &id, &x, &y, 1)) != 0;
	if (cast) {
		if (type == 6) {
			type = At<int32_t>(struck, d2r::rva::UnitType);
			id = d2r::Engine<uint32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(struck, __FILE__, __LINE__);
		}
		d2r::Engine<SendFn>(ItemSkillSend)(unit, skillId, level, type, id, 1);
	}
	d2r::Engine<FlagFn>(d2r::sig::SetUnitFlag)(unit, UnitFlagItemEventCast, 0);
	return cast ? 1 : 0;
}


}  // namespace

namespace pd2r::skills {

// Revive's do (srvdofunc 58): D2R's own, then Revive's aura events go on the revived monster. D2R's Revive never
// registers them, so without this event 32 never fires. The corpse is revived in place (seen in game: same unit,
// result 1, mode neutral), so the target before the call is the pet after it; a refused revive leaves it dead.
std::atomic<uint32_t> g_revives {0};

// --- Summon first think and revive size (#1835) --------------------------------------------------------------------
// PD2's patch table: 1.13c D2Game +0x4d879 `14` -> `08` (srvdo 16 Valkyrie, used by no other skill: pet AI event at
// frame + 8, not + 20) and +0x50ad1 `0f` -> `08` (Revive's pet, srvdo 58, + 8, not + 15). D2R 93847 keeps both, in
// 0x558000 and 0x55e7e0 (`add r9d,imm8` before the AI event queue 0x48b720, type 2). No SDK service sets a pet's first
// think, so a MutationService byte patch changes each imm8, found by signature, expected byte checked.

}  // namespace pd2r::skills

auto SpinSplashes(Game* game, int32_t event, Unit* unit, Unit* target, const uint8_t* damage) noexcept -> void {
	using namespace d2r::rva;
	// The splash's own hits raise event 5 again while the spin lasts: they must not splash in turn
	static thread_local bool t_spinSplashing = false;
	if (t_spinSplashing || game == nullptr || unit == nullptr || target == nullptr ||
	    (damage != nullptr && (damage[0] & 0x20) == 0) || !Spinning(unit)) {
		return;
	}
	t_spinSplashing = true;
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(d2r::DataFlag(game));
	for (auto* node = At<uint8_t*>(unit, UnitEventNodes); node != nullptr; node = At<uint8_t*>(node, EventNodeNext)) {
		const int32_t stat = At<int32_t>(node, EventNodeStat);
		const bool splash = stat == StatSplashOnHit || stat == StatMapMonSplash;
		if (At<uint16_t>(node, EventNodeEvent) != event || (!splash && stat != StatSkillOnHit)) {
			continue;
		}
		const uint32_t layer = At<uint16_t>(node, EventNodeLayer);
		const auto skillId = static_cast<int32_t>(layer >> (At<uint32_t>(tables, TablesSkillLayerShift) & 0x1f));
		const auto level = static_cast<int32_t>(layer & At<uint32_t>(tables, TablesSkillLayerMask));
		const uint8_t* skill = splash ? nullptr : d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
		if (!splash && (skill == nullptr || !g_castOnHit ||
		                (At<uint8_t>(skill, SkItemTgtDo) & ItemTgtDoBit) != 0)) {
			continue;  // D2R already cast it from the struck unit, or the cast was not found
		}
		const int32_t chance = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(unit, stat, layer);
		const auto roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit), 100));
		if (chance < 1 || roll >= chance) {
			continue;
		}
		if (!splash) {
			const uint8_t itemTarget = At<uint8_t>(skill, SkItemTarget);
			const int64_t cast = itemTarget == ItemTargetAimPoint ? 0 : CastOnHit(game, unit, target, skillId, level);
			continue;
		}
		const int64_t cast = Splash(game, unit, target, skillId, level);
	}
	t_spinSplashing = false;
}

auto InstallSpinOnHit(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	sigscan::Resolve(SpinOwn, std::size(SpinOwn), context->exeBase, nullptr, 0);
	g_castOnHit = ItemSkillTarget.state == sigscan::State::Ok && ItemSkillSend.state == sigscan::State::Ok;
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; spin skill on hit%s", g_castOnHit ? "" : " FAILED (signature)");
	}
	return g_castOnHit;
}

PD2R_RULE(InstallSpinOnHit, 330)

