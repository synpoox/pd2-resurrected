// The odds-only TC roll (tc_roll.cpp): PD2's 1-in-N from a TC's Unique x Set, shared by the key rolls and the map rolls.
#pragma once

#include "pd2_engine.h"

#include <atomic>
#include <cstdint>

// PD2's 0x10375ee0: N of the 1-in-N roll; 0 means no drop. More than 8 players counts as 1.
constexpr auto Odds(int32_t unique, int32_t set, int32_t players) noexcept -> int64_t {
	if (players > 1 && players <= 8) {
		unique = unique * (127 - 2 * (players - 1)) / 127;
	}
	return static_cast<int64_t>(unique) * (set > 1 ? set : 1);
}
// N for the TC named `name` of the data bank `dataFlag`; 0 (and a sampled warning) when the table has no such TC.
auto TcOdds(uint8_t dataFlag, const char* name, int32_t players) noexcept -> int64_t;
// TCs or items the tables lack; one sampled warning counter for the key rolls and the map rolls.
extern std::atomic<uint32_t> g_tcMissing;
