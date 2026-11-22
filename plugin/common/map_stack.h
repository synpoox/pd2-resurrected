// The map stack rule's stat compare (#1326), with no engine types so tests/map_stack_check.cpp builds it on the host.
// `entries(unit, stat, out, max)` is UnitStatEntries; returns the first stat id that differs between a and b (the
// quantity, stat 70, is skipped before anything is read), or -1 when every other stat matches layer by layer.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pd2::mapstack {

// UnitStatEntries (0x2f84b0 -> 0x2fa060 -> D2RCore+0x832940) writes 16 bytes per entry (movups copy, `add r8,0x10`):
// layer, stat, value, and a fourth dword nobody knows (so it is never compared or copied on purpose). A Slot3 dump of a
// map's stat 564 read 00000000 00000234 00000057. The toolkit's older 8-byte note ({u16 layer, u16 stat, i32}) is the
// original body, not D2RCore's.
struct Entry {
	int32_t layer, stat, value, unknown;
};
static_assert(sizeof(Entry) == 16);

template <class Unit, class Entries>
auto FirstDifference(Unit* a, Unit* b, int32_t statCount, int32_t quantityStat, Entries entries) noexcept -> int32_t {
	for (int32_t stat = 0; stat < statCount; ++stat) {
		if (stat == quantityStat) {
			continue;
		}
		Entry ea[64] {}, eb[64] {};
		const int32_t na = entries(a, static_cast<uint32_t>(stat), ea, 64);
		const int32_t nb = entries(b, static_cast<uint32_t>(stat), eb, 64);
		// A count past the buffer is clamped before it is read (the engine fills at most 64)
		const int32_t k = na > 64 ? 64 : na;
		if (na != nb) {
			return stat;
		}
		for (int32_t i = 0; i < k; ++i) {
			if (ea[i].layer != eb[i].layer || ea[i].stat != eb[i].stat || ea[i].value != eb[i].value) {
				return stat;
			}
		}
	}
	return -1;
}

// The non-quantity stats of one map, read off the source before they are written onto the split's new piece (two
// native-item callbacks, so the two units are never open at once). shortcut: 64 entries, a map has a handful.
struct Saved {
	Entry entry[64];
	int32_t count = 0;
};

template <class Unit, class Entries>
auto Collect(Unit* src, int32_t statCount, int32_t quantityStat, Entries entries, Saved& saved) noexcept -> void {
	saved.count = 0;
	for (int32_t stat = 0; stat < statCount; ++stat) {
		Entry e[64] {};
		int32_t n = stat == quantityStat ? 0 : entries(src, static_cast<uint32_t>(stat), e, 64);
		n = n > 64 ? 64 : n;
		for (int32_t i = 0; i < n && saved.count < 64; ++i) {
			saved.entry[saved.count++] = e[i];
		}
	}
}

// Whether two collected stat lists match entry by entry (layer, stat, value; the unknown dword is ignored)
inline auto SameStats(const Saved& a, const Saved& b) noexcept -> bool {
	if (a.count != b.count) {
		return false;
	}
	for (int32_t i = 0; i < a.count; ++i) {
		if (a.entry[i].layer != b.entry[i].layer || a.entry[i].stat != b.entry[i].stat || a.entry[i].value != b.entry[i].value) {
			return false;
		}
	}
	return true;
}

// Sets each saved (stat, layer) on dst; SetUnitStat adds to a stat the piece already has, so callers skip 564 (#1499, #1504)
template <class Unit, class SetStat>
auto Apply(Unit* dst, const Saved& saved, SetStat set) noexcept -> int32_t {
	for (int32_t i = 0; i < saved.count; ++i) {
		set(dst, saved.entry[i].stat, saved.entry[i].value, saved.entry[i].layer);
	}
	return saved.count;
}

// A map's item code (m01..m25, little-endian "m01 ")
constexpr auto IsMapCode(uint32_t code) noexcept -> bool {
	const auto ch = [code](int i) { return static_cast<char>((code >> (8 * i)) & 0xff); };
	return ch(0) == 'm' && ch(1) >= '0' && ch(1) <= '2' && ch(2) >= '0' && ch(2) <= '9' && (ch(3) == ' ' || ch(3) == 0);
}

}  // namespace pd2::mapstack
