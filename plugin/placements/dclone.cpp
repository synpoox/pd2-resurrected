// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Diablo Clone: PD2 0x3b19, 0x1994 is the arena centre; the souls' spawn square is 50 x 50 from (0x3b00, 0x197b).
// Lane D (#958) flips `level` to `target` (209) with the new arena and moves the places to it.
const placements::Binding bindings[] = {{"dclone", 138, 209, Source::Existing, true, true}};
const placements::Place places[] = {
	{"dclone", "centre", 15129, 6548, 0, 0, 0},
	{"dclone", "souls", 0x3b00, 0x197b, 50, 50, 0},
};
const placements::Reg reg(bindings, std::size(bindings), places, std::size(places));
}  // namespace
