// PD2's Holy Sword parry roll without engine types, so a host check covers it (plugin/tests/parry_roll_check.cpp, #1466).
// PD2 avoid roll 0x103035f0: chance = block chance (cap 75), weapon class 5 (2hs, two-handed swing) parries, as 13
// (ht2, claws) blocks in D2R's avoid (0x44ef70); both give result 0x10.
#pragma once

#include <algorithm>
#include <cstdint>

namespace pd2::parry {

inline constexpr int32_t WeaponClass2HS = 5;     // D2R's composit weapon class of a two-handed sword (0-based: hth 0 ... ht2 13)
inline constexpr int32_t WeaponClassHT2 = 13;    // D2R's own block (Assassin claws)
inline constexpr int32_t Cap = 75;               // PD2 0x4b
inline constexpr int32_t LucionMerged = 1129;    // PD2 monstats 1112 (0x458): his hits are parried a third as often
inline constexpr uint32_t ResultBlock = 0x10;

// `raw` is the defender's block chance (D2R 0x450070, passive_weaponblock included); 0 or less never parries
constexpr auto Chance(int32_t raw, bool lucion) noexcept -> int32_t {
	return raw <= 0 ? 0 : (lucion ? std::min(raw, Cap) / 3 : std::min(raw, Cap));
}

// `roll` is the unit's seed roll; PD2 and D2R both test roll % 100 < chance
constexpr auto Parries(int32_t chance, uint32_t roll) noexcept -> bool { return roll % 100 < static_cast<uint32_t>(chance); }

}  // namespace pd2::parry
