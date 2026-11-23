// Shared declarations for the core-rules skill rules that live in their own files under skill/.
#pragma once

#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

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
