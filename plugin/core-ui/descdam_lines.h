// PD2's skill tooltip damage lines (skilldesc descdam, #2005): PD2 rewrites D2Client's descdam table (ProjectDiablo.dll
// 0x103ae5d0, PD2.gpr) for ids 11, 13, 15, 16 and adds 25-28. D2R's own 25, 26 and 28 print other skills' lines, and
// its 11, 13 and 15 read other columns. Data moves 28 onto D2R's 24 (the same line); this part puts PD2's lines into
// D2R's descdam table for 11, 13, 15, 25 and 26. Client-only display.
#pragma once

#include "sig.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace descdam_lines {

// The descdam ids whose table slot this part fills, in install order
inline constexpr int32_t Ids[] = {11, 13, 15, 25, 26};

// PD2's charge index for descdam 25: the charges held (aurastat1 on the aurastate list), at most 2; 0 without the state
constexpr auto ChargeIndex(int32_t charges) noexcept -> int32_t {
	return charges > 2 ? 2 : (charges < 0 ? 0 : charges);
}

// PD2's line colour for an elemtypes code (fire 1 -> red 1, lightning 2 -> yellow 9, magic 3 and cold 4 -> blue 3,
// poison 5 -> green 2); -1 keeps the colour the weapon fill gave
constexpr auto ElemColor(int32_t elem) noexcept -> int32_t {
	switch (elem) {
	case 1: return 1;
	case 2: return 9;
	case 3:
	case 4: return 3;
	case 5: return 2;
	default: return -1;
	}
}

// PD2's 0x1037c4c0 with 100: v * pct / 100, truncated
constexpr auto Percent(int32_t v, int32_t pct) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(v) * pct / 100);
}

// v * srcDam / 128, rounded toward zero (PD2's cdq / and 0x7f / sar 7)
constexpr auto SrcDamScale(int32_t v, int32_t srcDam) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(v) * srcDam / 128);
}

// The shared signature entries the part calls (its own are resolved by Install)
extern sigscan::Entry* const Uses[9];

// Fills the table slots. Returns the report for the log ("pd2 lines on" or FAILED with the reason).
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// Lines printed per id, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace descdam_lines
