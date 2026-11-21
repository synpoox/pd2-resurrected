// PD2's vendor and gamble price rules D2R lacks (#1539), with no engine types so tests/item_price_check.cpp builds it on
// the host. docs/research/items/item-prices.md has the evidence.
#pragma once

#include <algorithm>
#include <cstdint>

namespace pd2::itemprice {

inline constexpr int32_t TypeBook = 0x12;
inline constexpr int32_t TypeWeapon = 45;  // itemtypes "weap": throwing knives, axes and javelins are under it

inline constexpr int32_t QualitySuperior = 3;
inline constexpr int32_t RepairCap = 250000;

// D2R's stat term (bonus stat prices) runs for superior items; PD2 runs it for tempered (9) but not superior (3)
constexpr auto SkipsStatTerm(int32_t quality) noexcept -> bool {
	return quality == QualitySuperior;
}

// PD2 caps a repair quote (price type 3) at 250000 after the reduced-prices cut and the minimum of 1; D2R has no cap
constexpr auto CapRepair(int32_t price) noexcept -> int32_t {
	return std::min(price, RepairCap);
}

// D2R skips the quantity step of a price for books; PD2 also skips it for every weapon (buy and sell)
constexpr auto SkipsQuantity(bool isBook, bool isWeapon) noexcept -> bool {
	return isBook || isWeapon;
}

// D2R's gamble price (0x376b10) for a non-ring, non-amulet base: `mid` is the stack midpoint (min + max) / 2, at least 1
// in D2R; PD2 uses 1 for every item. `cost` is the item's gamble cost, `uberCost` / `ultraCost` those of its upgraded
// rows, `level` its item level, `playerLevel` the buyer's.
constexpr auto GamblePrice(int32_t playerLevel, int32_t mid, int32_t cost, int32_t level, int32_t uberLevel, int32_t uberCost,
                           int32_t ultraLevel, int32_t ultraCost) noexcept -> int32_t {
	const int32_t u = std::max(50 * (playerLevel - uberLevel) + 1, 0);
	const int32_t x = std::max(25 * (playerLevel - ultraLevel) + 1, 0);
	const int32_t l = std::max(playerLevel, 5);
	const int32_t base = ((10000 - x - u) * mid * cost + ultraCost * x + uberCost * u) / 10000;
	const int32_t levels = (std::max(level - 45, 0) - level / 2 + l) * 250 / 3;
	return ((base + levels) * ((l * 2 + 1) / 3 + 20)) / 15;
}

}  // namespace pd2::itemprice
