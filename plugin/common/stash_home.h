// Stash affinity (#1553): which reserved shared stash page and cell an item belongs on, with no engine types so it
// builds on the host. The routing (core-ui/stash_affinity.cpp) and the stash tabs (core-ui/stash_tabs.cpp) read these.
//
// Page plan: D2R's 5 shared tabs (0-4), then 10 general Loader extra tabs (5-14), then the reserved pages at fixed
// indices, so add_shared_tabs must be at least 15. A slider set higher adds general tabs after them.
// Page 15 is the Maps tab, laid out round the Horadric Cube the stash shows at the RotW tabs' spot (grid columns 3-6,
// rows 6-9): row 0 under the tier buttons and row 1 under the base buttons stay empty, the selected base's maps sit
// above the cube (MapView), the map currency in fixed cells in two columns each side of it (CurrencyCell). Pages 16-18 store each tier's maps,
// a row per base; the Maps tab swaps the selected base's maps in and out. Page 19 is the Bossing tab.
// Precedence: Maps, then Map Materials, then Bossing; an item the RotW Materials tab takes (AdvancedStashStackable:
// Pandemonium keys, organs, essences) stays there, so no Bossing code is one of those.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace pd2::affinity {

enum class Category : uint8_t { None, Maps, Bossing, MapMaterials };

inline constexpr uint32_t VanillaSharedPages = 5;
inline constexpr uint32_t GeneralExtraPages = 10;
inline constexpr uint32_t FirstReservedPage = VanillaSharedPages + GeneralExtraPages;
inline constexpr uint32_t MapsPage = FirstReservedPage;              // the Maps tab: shown maps and the map currency
inline constexpr uint32_t FirstStoragePage = FirstReservedPage + 1;  // tier 1's stored maps; tiers 2 and 3 follow
inline constexpr uint32_t BossingPage = FirstReservedPage + 4;
inline constexpr uint32_t LastReservedPage = BossingPage;
inline constexpr uint32_t RequiredExtraTabs = LastReservedPage + 1 - VanillaSharedPages;  // 15
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
	int32_t row = -1;     // a map's row on its tier's storage page, a currency's cell row; -1: any free cell
	int32_t column = -1;  // a currency's cell column; -1 for the rest
};

struct Cell {
	int32_t x = -1;
	int32_t y = -1;
};

// The Maps tab's cells for the selected base's maps: rows 2-3; row 4 between the currency columns is stone
inline constexpr int32_t MapViewCells = 20;
constexpr auto MapView(int32_t i) noexcept -> Cell {
	if (i < 0 || i >= MapViewCells) {
		return {};
	}
	return {i % PageWidth, 2 + i / PageWidth};
}
constexpr auto InMapView(int32_t x, int32_t y) noexcept -> bool {
	return (y == 2 || y == 3) && x >= 0 && x < PageWidth;
}

// A map currency's cell, by its place in MapMaterials: two columns each side of the cube, rows 4-9, 12 left (columns
// 0-1) and the rest right (8-9), so stone shows between them and the cube; a cell per currency, none spare (#1699)
inline constexpr int32_t CurrencyCells = static_cast<int32_t>(std::size(MapMaterials));
constexpr auto CurrencyCell(int32_t i) noexcept -> Cell {
	if (i < 0 || i >= CurrencyCells) {
		return {};
	}
	const int32_t j = i % 12;
	return {(i < 12 ? 0 : 8) + j % 2, 4 + j / 2};
}

// The Bossing tab, laid out like the RotW Materials tab round the cube: a block per boss (its map, then its parts) in
// rows 0 and 2 with stone between the blocks, the Tristram map and torch fragments over the cube, by place in Bossing
inline constexpr Cell BossingCell[] = {{4, 4}, {0, 0}, {1, 0}, {2, 0}, {3, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0},
                                       {0, 2}, {1, 2}, {2, 2}, {3, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2}, {5, 4}};
// The boss maps do not stack: a second one, or a part past a full stack, goes back to the inventory (the tab has no
// spare cells, #1699)
inline constexpr const char* BossingMaps[] = {"ubtm", "dcma", "rtma", "uba", "luca"};

// The tier (0-2) and the place in its list of a map code; {-1, -1} for the rest
struct Base {
	int32_t tier = -1;
	int32_t index = -1;
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

constexpr auto BaseOf(uint32_t raw) noexcept -> Base {
	const uint32_t code = Normal(raw);
	if (const int32_t r = IndexIn(Tier1, code); r >= 0) {
		return {0, r};
	}
	if (const int32_t r = IndexIn(Tier2, code); r >= 0) {
		return {1, r};
	}
	if (const int32_t r = IndexIn(Tier3, code); r >= 0) {
		return {2, r};
	}
	return {};
}

// The map code of a tier's base button (0 past the tier's list)
constexpr auto BaseCode(int32_t tier, int32_t index) noexcept -> uint32_t {
	const auto in = [index](const auto& list) noexcept -> uint32_t {
		return index >= 0 && static_cast<size_t>(index) < std::size(list) ? Code(list[index]) : 0;
	};
	return tier == 0 ? in(Tier1) : tier == 1 ? in(Tier2) : tier == 2 ? in(Tier3) : 0;
}

// Whether two of an item merge into one stack: all but the boss maps (map currency and boss parts stack to 50)
constexpr auto Stacks(uint32_t code) noexcept -> bool {
	return IndexIn(BossingMaps, Normal(code)) < 0;
}

constexpr auto HomeOf(uint32_t raw) noexcept -> Home {
	const uint32_t code = Normal(raw);
	if (const Base b = BaseOf(code); b.tier >= 0) {
		return {Category::Maps, FirstStoragePage + static_cast<uint32_t>(b.tier), b.index};
	}
	if (const int32_t i = IndexIn(MapMaterials, code); i >= 0) {
		const Cell c = CurrencyCell(i);
		return {Category::MapMaterials, MapsPage, c.y, c.x};
	}
	if (const int32_t i = IndexIn(Bossing, code); i >= 0) {
		return {Category::Bossing, BossingPage, BossingCell[i].y, BossingCell[i].x};
	}
	return {};
}

// Whether an item already sits where it belongs: on its page, in a map's row, on a currency's cell; a map of the
// shown base (shown: its code, 0 for none) also belongs in the Maps tab's map cells
constexpr auto AtHome(const Home& home, uint32_t code, uint32_t shown, bool sharedStash, uint32_t page, int32_t x,
                      int32_t y) noexcept -> bool {
	if (home.category == Category::None || !sharedStash) {
		return false;
	}
	if (home.category == Category::Maps && shown != 0 && Normal(code) == Normal(shown) && page == MapsPage &&
	    InMapView(x, y)) {
		return true;
	}
	return page == home.page && (home.row < 0 || y == home.row) && (home.column < 0 || x == home.column);
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
static_assert(std::size(MapMaterials) <= CurrencyCells);  // one cell each beside the cube
static_assert(Code("m01") == ('m' | ('0' << 8) | ('1' << 16) | (' ' << 24)));
static_assert(HomeOf(Code("m01")).page == 16 && HomeOf(Code("m01")).row == 0);
static_assert(HomeOf(Code("m24")).page == 16 && HomeOf(Code("m24")).row == 7);
static_assert(HomeOf(Code("m02")).page == 17 && HomeOf(Code("m21")).row == 7);
static_assert(HomeOf(Code("m25")).page == 18 && HomeOf(Code("m25")).row == 8);
static_assert(HomeOf('m' | ('0' << 8) | ('7' << 16)).page == 18);  // zero-padded code
static_assert(BaseCode(2, 8) == Code("m25") && BaseCode(0, 8) == 0 && BaseCode(3, 0) == 0);
static_assert(HomeOf(Code("imma")).page == 15 && HomeOf(Code("imma")).column == 0 && HomeOf(Code("imma")).row == 4);
static_assert(HomeOf(Code("ivea")).category == Category::MapMaterials);  // ear: Map Materials before Bossing
static_assert(HomeOf(Code("ivea")).column == 9 && HomeOf(Code("ivea")).row == 5);  // index 15: the right block
static_assert(HomeOf(Code("dcma")).category == Category::Bossing && HomeOf(Code("dcma")).page == 19);
static_assert(HomeOf(Code("pk1")).category == Category::None);  // the RotW Materials tab keeps the keys
static_assert(HomeOf(Code("r01")).category == Category::None);
static_assert(CurrencyCell(11).x == 1 && CurrencyCell(11).y == 9 && CurrencyCell(12).x == 8 && CurrencyCell(12).y == 4);
static_assert(CurrencyCell(21).x == 9 && CurrencyCell(21).y == 8 && CurrencyCell(22).x == -1);  // (8, 9), (9, 9) dropped
static_assert(MapView(0).y == 2 && MapView(19).x == 9 && MapView(19).y == 3 && MapView(20).x == -1);
static_assert(InMapView(5, 3) && !InMapView(5, 4) && !InMapView(1, 4) && !InMapView(4, 5));
static_assert([] {  // no map cell is a currency cell
	for (int32_t i = 0; i < CurrencyCells; ++i) {
		if (InMapView(CurrencyCell(i).x, CurrencyCell(i).y)) {
			return false;
		}
	}
	return true;
}());
static_assert(AtHome(HomeOf(Code("m04")), Code("m04"), 0, true, 16, 3, 1));
static_assert(!AtHome(HomeOf(Code("m04")), Code("m04"), 0, true, 16, 3, 2));
static_assert(AtHome(HomeOf(Code("m04")), Code("m04"), Code("m04"), true, 15, 3, 2));
static_assert(!AtHome(HomeOf(Code("m04")), Code("m04"), Code("m05"), true, 15, 3, 2));
static_assert(AtHome(HomeOf(Code("imma")), Code("imma"), 0, true, 15, 0, 4));
static_assert(!AtHome(HomeOf(Code("imma")), Code("imma"), 0, true, 15, 1, 4));
static_assert(AtHome(HomeOf(Code("dcma")), Code("dcma"), 0, true, 19, 0, 0));
static_assert(!AtHome(HomeOf(Code("dcma")), Code("dcma"), 0, true, 19, 4, 4));
static_assert(!AtHome(HomeOf(Code("dcma")), Code("dcma"), 0, true, 19, 9, 9));  // no spare cell (#1699)
static_assert(HomeOf(Code("cm2f")).column == 5 && HomeOf(Code("cm2f")).row == 4 && HomeOf(Code("lucd")).column == 9);
static_assert(std::size(BossingCell) == std::size(Bossing));
static_assert(!Stacks(Code("dcma")) && Stacks(Code("dcbl")) && Stacks(Code("imma")));
static_assert([] {  // no two bossing items share a cell
	for (size_t i = 0; i < std::size(BossingCell); ++i) {
		for (size_t j = i + 1; j < std::size(BossingCell); ++j) {
			if (BossingCell[i].x == BossingCell[j].x && BossingCell[i].y == BossingCell[j].y) {
				return false;
			}
		}
	}
	return true;
}());
static_assert(!AtHome(HomeOf(Code("dcma")), Code("dcma"), 0, false, 19, 4, 4));
static_assert(FreeColumn(0) == 0 && FreeColumn(0b111) == 3 && FreeColumn(0x3ff) == -1);

}  // namespace pd2::affinity
