// blood_golem
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

// --- Blood Golem's heal per hit (#2001) ------------------------------------------------------------------------------
// PD2 writes its own aura event 23 (ProjectDiablo.dll 0x10346f20, PD2.gpr; 0x10346f30 installed) over 1.13c's
// (D2Game +0x4e5f0) in the event table (+0x1077a8). Its rule: the target is a player or a monster (a monster with
// monstats Drain 0 for the game's difficulty is skipped, but the drain does not scale the heal), the target's
// alignment (D2Common #10830) is 0, golem and owner (0x103749f0(golem, 1); no owner: the golem itself) are alive; then
// h = calc2 (skill +0x13c) at the event's level for the owner, and the golem and the owner each get h life (1/256
// units, clipped at max life, 1.13c's +0x4df20), with overlay 151 on each one that gained life. The damage dealt is not
// read. D2R 93847's event 23 (0x55b2d0) evaluates calc2 and calc3 for the owner (or the golem) and calls 0x559ee0,
// which heals min(damage x drain%, target life) x calc2% to the golem and x calc3% to the owner; core-rules redirects
// that call. No SDK service runs a skill event, so the call is redirected.
constexpr const char* BloodGolemHealCall =
    "4c 8b c5 89 44 24 30 49 8b d6 89 5c 24 28 49 8b cd 89 7c 24 20 e8 ?? ?? ?? ?? 48 8b 74 24 78";

constexpr uint32_t BloodGolemHealCallAt = 21;

using BloodGolemHealFn = uint64_t (*)(Game*, Unit*, Unit*, int32_t, int32_t, int32_t, int32_t) noexcept;

BloodGolemHealFn g_bloodGolemHeal = nullptr;

sigscan::Entry* const BloodGolemUses[] = {&d2r::sig::UnitAlignment, &d2r::sig::UnitDead, &d2r::sig::UnitStat,
                                          &d2r::sig::UnitMaxLife, &d2r::sig::SetUnitStat, &d2r::sig::SetOverlay};

// 1.13c's +0x4df20 and D2R's 0x55a2e0: life + amount, clipped at max life; returns the life added

// 1.13c's +0x4df20 and D2R's 0x55a2e0: life + amount, clipped at max life; returns the life added
auto AddLife(Unit* unit, int32_t amount) noexcept -> int32_t {
	if (amount < 1) {
		return 0;
	}
	int32_t life = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(unit, StatHitpoints, 0) + amount;
	const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitMaxLife)(unit);
	if (max < life) {
		amount += max - life;
		life = max;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat)(unit, StatHitpoints, life, 0);
	return amount;
}

// In place of 0x559ee0 (game, golem, target, damage, calc2, calc3, target life); the caller tests al.

// In place of 0x559ee0 (game, golem, target, damage, calc2, calc3, target life); the caller tests al.
auto BloodGolemHeal(Game* game, Unit* golem, Unit* target, int32_t, int32_t calc2, int32_t, int32_t) noexcept -> uint64_t {
	using namespace d2r::rva;
	const AiOwnerFn aiOwner = AiOwnerGetter();
	if (game == nullptr || golem == nullptr || target == nullptr || aiOwner == nullptr) {
		return 0;
	}
	const int32_t type = At<int32_t>(target, UnitType);
	if (type != 0 && type != UnitMonster) {
		return 0;
	}
	if (type == UnitMonster) {
		const auto* data = At<const uint8_t*>(target, UnitData);
		const uint8_t* monstats = data != nullptr ? At<const uint8_t*>(data, 0) : nullptr;
		if (data != nullptr && (monstats == nullptr || At<uint8_t>(monstats, MonDrain + At<uint8_t>(game, GameDifficulty)) == 0)) {
			return 0;
		}
	}
	const auto dead = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitDead);
	if (d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitAlignment)(target) != 0 || dead(golem) != 0) {
		return 0;
	}
	Unit* owner = StunOwner(golem, aiOwner);  // PD2's 0x103749f0(golem, 1)
	if (owner == nullptr || dead(owner) != 0) {
		return 0;
	}
	const auto overlay = d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SetOverlay);
	const int32_t toGolem = AddLife(golem, calc2);
	if (toGolem > 0) {
		overlay(golem, OverlayLifeHeal, 0);
	}
	const int32_t toOwner = AddLife(owner, calc2);
	if (toOwner > 0) {
		overlay(owner, OverlayLifeHeal, 0);
	}
	return 1;
}

// --- Redemption skips owned corpses (#1987) --------------------------------------------------------------------------
// PD2 redirects the D2Common #10501 corpse test in 1.13c's Redemption callback (D2Game +0x98680, passed by srvdo 82 at
// +0x9a5a8; call at +0x986d8) to ProjectDiablo.dll 0x102fb290 (PD2.gpr 0x102fb280), which returns 0 when the corpse's
// owner id (+0x98) is set and its owner type (+0x94) is 0. D2R's srvdo 82 (0x564260) passes the callback 0x565e60,
// which calls the per-corpse worker 0x560d90 (its corpse test 0x33a5c0 has no owner test); that call is redirected.
// The worker's other callers (missile srvdo 19, radamentdeath) keep D2R's. No SDK service covers it.

}  // namespace

auto InstallBloodGolem(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	bool golem = AiOwnerGetter() != nullptr;
	for (const sigscan::Entry* e : BloodGolemUses) {
		golem = golem && e->state == sigscan::State::Ok;
	}
	golem = golem && RedirectCall(context, BloodGolemHealCall, BloodGolemHealCallAt, reinterpret_cast<uint64_t>(&BloodGolemHeal),
	                              reinterpret_cast<uint64_t*>(&g_bloodGolemHeal));
	plog::Info("blood golem: pd2 heal %s", golem ? "on" : "FAILED (event 23 call or helper signatures)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; blood golem heal%s", golem ? "" : " FAILED");
	}
	return golem;
}

PD2R_RULE(InstallBloodGolem, 380)

