// Placements of one encounter (#947): a private author edits this file only (docs/cartographer/contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Diablo Clone (#958): level 145 is Cartographer's 1x1 Wilderness (Swamp) arena (arenas.txt). Its
// spots (centre, the souls) are named in arenaspots.txt and read through cartographer.arena.v1 (core-events arena_spots.h),
// so this file holds no places.
const placements::Binding bindings[] = {{"dclone", 145, 145, Source::Private, true, true}};
const placements::Reg reg(bindings, std::size(bindings));
}  // namespace
