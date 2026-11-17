// The map stack rule's stat compare (#1326), with no engine types so tests/map_stack_check.cpp builds it on the host.
// `entries(unit, stat, out, max)` is UnitStatEntries; returns the first stat id that differs between a and b (the
// quantity, stat 70, is skipped before anything is read), or -1 when every other stat matches layer by layer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pd2::mapstack {

struct Entry {
	uint16_t layer, stat;
	int32_t value;
};

template <class Unit, class Entries>
auto FirstDifference(Unit* a, Unit* b, int32_t statCount, int32_t quantityStat, Entries entries) noexcept -> int32_t {
	for (int32_t stat = 0; stat < statCount; ++stat) {
		if (stat == quantityStat) {
			continue;
		}
		Entry ea[64] {}, eb[64] {};
		const int32_t na = entries(a, static_cast<uint32_t>(stat), ea, 64);
		const int32_t nb = entries(b, static_cast<uint32_t>(stat), eb, 64);
		if (na != nb || (na > 0 && std::memcmp(ea, eb, static_cast<size_t>(na) * sizeof(Entry)) != 0)) {
			return stat;
		}
	}
	return -1;
}

// A map's item code (m01..m25, little-endian "m01 "). The split refuses these: splitStack's new piece is not known to
// keep the source's stats, and a map without its Area Level is worse than no split.
constexpr auto IsMapCode(uint32_t code) noexcept -> bool {
	const auto ch = [code](int i) { return static_cast<char>((code >> (8 * i)) & 0xff); };
	return ch(0) == 'm' && ch(1) >= '0' && ch(1) <= '2' && ch(2) >= '0' && ch(2) <= '9' && (ch(3) == ' ' || ch(3) == 0);
}

}  // namespace pd2::mapstack
