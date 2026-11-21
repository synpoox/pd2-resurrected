// Invader gear matches the look (#1215). An Invader's rolled parts are 16 bytes in its UnitData at +0x08 (HD TR LG RA LA RH LH SH
// S1..S8; 1.13c layout, confirmed in game by invaders.cpp's death log line), each an index into that part's monstats2 list
// (RHv, LHv, ...). At death the drop set follows the weapon the Invader shows:
//   Amazon      LHv am1,am2,lbb: all bows     -> the bow + quiver set, always
//   Assassin    RHv/LHv axf,ktr,clw,skr       -> one weapon set (claws and daggers), nothing to choose
//   Barbarian   RHv/LHv axe,bsd,gsd,lsd,whm   -> a gsd (great sword) in a hand: the 2-hander set; else the 1-hander set x2
//   Druid, Necromancer, Paladin               -> one weapon set each, nothing to choose
//   Sorceress   RHv cst,lst,sst: all staves   -> the 2-hander (staff) set, always
// PD2 rolled the weapon set at random; here every class drops the gear it shows (the sets with no look are never dropped).
#pragma once

#include <cstdint>

namespace pd2::invader_look {

constexpr int PartBytes = 16, PartRH = 5, PartLH = 6;  // HD TR LG RA LA RH LH SH S1..S8
constexpr int BarbarianHandTokens = 5, BarbarianGreatSword = 2;  // axe,bsd,gsd,lsd,whm: gsd is index 2 (monstats2 RHv and LHv)

enum class Weapon { Unknown, OneHand, TwoHand };

// The Barbarian's weapon from its part bytes; Unknown when a hand byte is outside the list (wrong offset: random roll)
inline Weapon Barbarian(const uint8_t* parts) noexcept {
	if (parts == nullptr || parts[PartRH] >= BarbarianHandTokens || parts[PartLH] >= BarbarianHandTokens) {
		return Weapon::Unknown;
	}
	return parts[PartRH] == BarbarianGreatSword || parts[PartLH] == BarbarianGreatSword ? Weapon::TwoHand : Weapon::OneHand;
}

}  // namespace pd2::invader_look
