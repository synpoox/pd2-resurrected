// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Lucion's arena (PD2 188) and the Temple of the Triune (PD2 189). Lane D (#962) flips both levels to their targets.
const placements::Binding bindings[] = {
	{"lucion", 184, 213, Source::Existing, true, false},
	{"temple", 185, 214, Source::Existing, false, false},
};
const placements::Place places[] = {
	{"lucion", "centre", 0x6223, 0x17e1, 0, 0, 0},
	{"lucion", "adds", 0x620d, 0x17cb, 0x2c, 0x2c, 0},         // the square Lucion's summons land in
	{"lucion", "temple-portal", 0x6221, 0x17df, 0, 0, 0},      // the dark grey portal to the Temple
};
const placements::Reg reg(bindings, std::size(bindings), places, std::size(places));
}  // namespace
