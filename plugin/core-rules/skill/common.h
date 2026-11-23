// Shared surface of core-rules' per-rule skill files (plugin/core-rules/skill/*.cpp): rules other core-rules code calls
// into (item_rules.cpp's HitEvent relay). The installs register themselves (registry.h), so they are not declared here.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

// --- PD2's leech reductions (#2009, leech.cpp) ---
// Missile and area hits halve life and mana leech; a Leap Attack hit divides life leech by 3.
// Names the victim of the one Leap Attack hit about to run (skill_functions.cpp LeapHitOne); nullptrs after it.
auto LeapAttackLeech(d2r::Unit* unit, d2r::Unit* victim) noexcept -> void;

// Inventory gold cap (#2027, gold_cap.cpp): the cap function's RVA (D2R's max inventory gold of a unit, `int32_t (*)(Unit*)`), 0 when its signature is not found.
auto GoldCapRva(const D2RL::PluginContext* context) noexcept -> uint64_t;

// --- Slow skips the prime evils (#2004, slow_prime_evil.cpp) ---
// Hides the attacker's item_slow event nodes for one event 5/6 dispatch (item_rules.cpp's HitEvent relay) when the
// target is a prime evil, and shows them again for a splash hit on another unit inside an outer call that hid them.
struct SlowNodes {
	static constexpr int Max = 16;  // a unit wears 12 items; more only guards a corrupt chain
	uint8_t* node[Max];
	uint16_t restore[Max];
	int count = 0;
	int32_t outer = -1;
};
auto SlowPrimeEvilBegin(d2r::Game* game, int32_t event, d2r::Unit* unit, d2r::Unit* target, SlowNodes& nodes) noexcept -> void;
auto SlowPrimeEvilEnd(const SlowNodes& nodes) noexcept -> void;

// --- Sacrifice's missile kills (#2043, sacrifice_missile.cpp) ---
// The missile of the missile damage or AreaApply call running on this thread (D2R passes the deal its owner), or null.
auto SacrificeKillMissile() noexcept -> d2r::Unit*;
