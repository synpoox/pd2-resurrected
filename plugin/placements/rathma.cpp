// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Rathma's three levels (PD2 161-163). Per level: portal0.. (the in-level ghostly portal; the third level picks one of
// its three), marker (where it takes the group; the tier-0 re-entry spot), boss0/boss1 (arg: merged monstats class,
// 950 RathmaBone, 951 RathmaPoison, 952 BoneClone, 953 PoisonClone) and arena (the 80 x 80 subtiles about it).
// Lane D (#961) flips each level to its target (210-212).
const placements::Binding bindings[] = {
	{"rathma1", 160, 210, Source::Existing, true, true},
	{"rathma2", 161, 211, Source::Existing, false, true},
	{"rathma3", 162, 212, Source::Existing, false, true},
};
const placements::Place places[] = {
	{"rathma1", "portal0", 0x4862, 0x363d, 0, 0, 0},
	{"rathma1", "marker", 0x476b, 0x35d3, 0, 0, 0},
	{"rathma1", "boss0", 0x476c, 0x35bf, 0, 0, 951},
	{"rathma1", "arena", 0x476c, 0x35bf, 0, 0, 0},
	{"rathma2", "portal0", 0x488a, 0x3a25, 0, 0, 0},
	{"rathma2", "marker", 0x47d7, 0x3961, 0, 0, 0},
	{"rathma2", "boss0", 0x47d3, 0x394b, 0, 0, 950},
	{"rathma2", "arena", 0x47d3, 0x394b, 0, 0, 0},
	{"rathma3", "portal0", 0x4fb1, 0x1590, 0, 0, 0},
	{"rathma3", "portal1", 0x4ff0, 0x151c, 0, 0, 0},
	{"rathma3", "portal2", 0x5096, 0x1549, 0, 0, 0},
	{"rathma3", "marker", 0x4e98, 0x1419, 0, 0, 0},
	{"rathma3", "boss0", 0x4e97, 0x1403, 0, 0, 953},
	{"rathma3", "boss1", 0x4e9d, 0x1403, 0, 0, 952},
	{"rathma3", "arena", 0x4e9a, 0x1403, 0, 0, 0},
};
const placements::Reg reg(bindings, std::size(bindings), places, std::size(places));
}  // namespace
