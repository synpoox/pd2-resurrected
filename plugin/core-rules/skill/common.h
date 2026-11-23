// Shared surface of core-rules' per-rule skill files (plugin/core-rules/skill/*.cpp): installs called once from
// InstallSkillFunctions (skill_functions.cpp), and rules other core-rules code calls into (item_rules.cpp's HitEvent relay).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

// Monster curse cast (#2003), monster_curse.cpp: true when installed; appends its part to `report`.
auto InstallMonsterCurse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// PD2's to-hit chance (#2010), to_hit.cpp: true when installed; appends its part to `report`.
auto InstallToHit(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// --- PD2's leech reductions (#2009, leech.cpp) ---
// Missile and area hits halve life and mana leech; a Leap Attack hit divides life leech by 3. True when installed;
// appends its part to `report`.
auto InstallLeech(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
// Names the victim of the one Leap Attack hit about to run (skill_functions.cpp LeapHitOne); nullptrs after it.
auto LeapAttackLeech(d2r::Unit* unit, d2r::Unit* victim) noexcept -> void;

// Baal's throne wave delay (#2014), baal_wave_delay.cpp: true when installed; appends its part to `report`.
auto InstallBaalWaveDelay(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Just-portaled state lasts 15 frames (#2022), portal_state_time.cpp: true when installed; appends its part to `report`.
auto InstallPortalStateTime(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Whirlwind hit rate (#2015), whirlwind.cpp: a player's Whirlwind hits every Param3 frames; appends its part to `report`.
auto InstallWhirlwind(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

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
