// The pair's half (#645, PD2's 0x1037c4c0 with 50): value * 50 / 100 in doubles, truncated toward zero. No engine types, so
// tests/rathma_split_check.cpp builds it on the host.
#pragma once

#include <cstdint>

namespace pd2::rathma {

inline constexpr auto Half(int32_t value) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(value) * 50 / 100);
}

}  // namespace pd2::rathma
