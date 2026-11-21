// Stash affinity (#1553): which reserved shared stash page an item belongs on, with no engine types so it builds on
// the host. The routing (core-ui/stash_affinity.cpp) and a later stash view read these same rules.
//
// Page plan: D2R's 5 shared tabs (0-4), then 10 general Loader extra tabs (5-14), then the reserved pages at fixed
// indices, so add_shared_tabs must be at least 15. A slider set higher adds general tabs after them.
// Precedence: Maps, then Map Materials, then Bossing; an item the RotW Materials tab takes (AdvancedStashStackable:
// Pandemonium keys, organs, essences) stays there, so no Bossing code is one of those.
#pragma once

#include <cstdint>

namespace pd2::affinity {

enum class Category : uint8_t { None, Maps, Bossing, MapMaterials };

inline constexpr uint32_t VanillaSharedPages = 5;
inline constexpr uint32_t GeneralExtraPages = 10;
inline constexpr uint32_t FirstReservedPage = VanillaSharedPages + GeneralExtraPages;  // Maps tier 1
inline constexpr uint32_t BossingPage = FirstReservedPage + 3;
inline constexpr uint32_t MapMaterialsPage = FirstReservedPage + 4;
inline constexpr uint32_t RequiredExtraTabs = MapMaterialsPage + 1 - VanillaSharedPages;  // 15
inline constexpr int32_t PageWidth = 10;  // inventory.txt Big Bank Page: 10 x 10

// The Cartographer maps by tier (misc.txt type t1m/t2m/t3m; tools/tests/test_stash_affinity.py checks them against the
// table). A base's row on its tier's page is its place in the list.
inline constexpr const char* Tier1[] = {"m01", "m04", "m05", "m06", "m12", "m20", "m23", "m24"};
inline constexpr const char* Tier2[] = {"m02", "m03", "m08", "m10", "m13", "m15", "m16", "m21"};
inline constexpr const char* Tier3[] = {"m07", "m09", "m11", "m14", "m17", "m18", "m19", "m22", "m25"};

// PD2's map crafting and modification items: orbs, ready orbs, Fortify, Dungeon Scarab, Force Mapevent Shard and the
// invader ears the map + ear recipes take
inline constexpr const char* MapMaterials[] = {"imma", "imrn", "imra", "rera", "scou", "upma", "upmp", "fort", "scrb",
                                               "iwss", "irma", "irrn", "irra", "rrra", "urma", "ivea", "ivez", "iveb",
                                               "ived", "iven", "ivep", "ives"};

// PD2's uber encounter items (misc.txt type ubr) the RotW Materials tab does not take, and the Torch Fragment
inline constexpr const char* Bossing[] = {"ubtm", "dcma", "dcbl", "dcho", "dcso", "rtma", "rtmo", "rtmv", "rtmf", "uba",
                                          "ubaa", "ubab", "ubac", "luca", "lucb", "lucc", "lucd", "cm2f"};

// D2R's item code: the code's characters little-endian, a 3-character code padded with a space
constexpr auto Code(const char* s) noexcept -> uint32_t {
	uint32_t code = 0;
	for (int i = 0; i < 4; ++i) {
		const char c = s[i] == 0 ? ' ' : s[i];
		code |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (8 * i);
		if (s[i] == 0) {
			for (int j = i + 1; j < 4; ++j) {
				code |= static_cast<uint32_t>(' ') << (8 * j);
			}
			break;
		}
	}
	return code;
}

struct Home {
	Category category = Category::None;
	uint32_t page = 0;
	int32_t row = -1;  // maps only: the base's row on its tier page; -1 is the first free cell anywhere on the page
};

template <size_t N>
constexpr auto IndexIn(const char* const (&list)[N], uint32_t code) noexcept -> int32_t {
	for (size_t i = 0; i < N; ++i) {
		if (Code(list[i]) == code) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

// A code with a 0 in place of the space pad reads the same
constexpr auto Normal(uint32_t code) noexcept -> uint32_t {
	for (int i = 0; i < 4; ++i) {
		if (((code >> (8 * i)) & 0xff) == 0) {
			code |= static_cast<uint32_t>(' ') << (8 * i);
		}
	}
	return code;
}

constexpr auto HomeOf(uint32_t raw) noexcept -> Home {
	const uint32_t code = Normal(raw);
	if (const int32_t r = IndexIn(Tier1, code); r >= 0) {
		return {Category::Maps, FirstReservedPage, r};
	}
	if (const int32_t r = IndexIn(Tier2, code); r >= 0) {
		return {Category::Maps, FirstReservedPage + 1, r};
	}
	if (const int32_t r = IndexIn(Tier3, code); r >= 0) {
		return {Category::Maps, FirstReservedPage + 2, r};
	}
	if (IndexIn(MapMaterials, code) >= 0) {
		return {Category::MapMaterials, MapMaterialsPage, -1};
	}
	if (IndexIn(Bossing, code) >= 0) {
		return {Category::Bossing, BossingPage, -1};
	}
	return {};
}

// Whether an item already sits where it belongs: on its page and, for a map, in its row
constexpr auto AtHome(const Home& home, bool sharedStash, uint32_t page, int32_t y) noexcept -> bool {
	return home.category != Category::None && sharedStash && page == home.page && (home.row < 0 || y == home.row);
}

// The first free column of a map's row, from a bit per occupied column; -1 when the row is full
constexpr auto FreeColumn(uint32_t occupied) noexcept -> int32_t {
	for (int32_t x = 0; x < PageWidth; ++x) {
		if ((occupied & (1U << x)) == 0) {
			return x;
		}
	}
	return -1;
}

static_assert(RequiredExtraTabs == 15);
static_assert(Code("m01") == ('m' | ('0' << 8) | ('1' << 16) | (' ' << 24)));
static_assert(HomeOf(Code("m01")).page == 15 && HomeOf(Code("m01")).row == 0);
static_assert(HomeOf(Code("m24")).page == 15 && HomeOf(Code("m24")).row == 7);
static_assert(HomeOf(Code("m02")).page == 16 && HomeOf(Code("m21")).row == 7);
static_assert(HomeOf(Code("m25")).page == 17 && HomeOf(Code("m25")).row == 8);
static_assert(HomeOf('m' | ('0' << 8) | ('7' << 16)).page == 17);  // zero-padded code
static_assert(HomeOf(Code("fort")).category == Category::MapMaterials && HomeOf(Code("fort")).page == 19);
static_assert(HomeOf(Code("ivea")).category == Category::MapMaterials);  // ear: Map Materials before Bossing
static_assert(HomeOf(Code("dcma")).category == Category::Bossing && HomeOf(Code("dcma")).page == 18);
static_assert(HomeOf(Code("pk1")).category == Category::None);  // the RotW Materials tab keeps the keys
static_assert(HomeOf(Code("r01")).category == Category::None);
static_assert(AtHome(HomeOf(Code("m04")), true, 15, 1) && !AtHome(HomeOf(Code("m04")), true, 15, 2));
static_assert(AtHome(HomeOf(Code("fort")), true, 19, 7) && !AtHome(HomeOf(Code("fort")), false, 19, 7));
static_assert(FreeColumn(0) == 0 && FreeColumn(0b111) == 3 && FreeColumn(0x3ff) == -1);

}  // namespace pd2::affinity
