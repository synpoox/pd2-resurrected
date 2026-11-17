// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Rathma's three levels (210-212): Cartographer's 1x1 Mesa arenas (arenas.txt, #961). Per level: portal0.. (the
// in-level ghostly portal; the third level picks one of its three), marker (where it takes the group; the tier-0 re-entry
// spot), boss0/boss1 (arg: merged monstats class, 950 RathmaBone, 951 RathmaPoison, 952 BoneClone, 953 PoisonClone) and
// arena (the 80 x 80 subtiles about it). Places are the arenaspots.txt spots of the same names in subtiles:
// x = (OffsetX + 8 + tx) * 5 + 2, y = (OffsetY + 8 + ty) * 5 + 2 (OffsetX 6000, OffsetY 4500/4800/5100); a test checks them.
const placements::Binding bindings[] = {
	{"rathma1", 210, 210, Source::Private, true, true},
	{"rathma2", 211, 211, Source::Private, false, true},
	{"rathma3", 212, 212, Source::Private, false, true},
};
const placements::Place places[] = {
	{"rathma1", "portal0", 30047, 22557, 0, 0, 0},
	{"rathma1", "marker", 30062, 22572, 0, 0, 0},
	{"rathma1", "boss0", 30062, 22547, 0, 0, 951},
	{"rathma1", "arena", 30062, 22547, 0, 0, 0},
	{"rathma2", "portal0", 30072, 24057, 0, 0, 0},
	{"rathma2", "marker", 30062, 24072, 0, 0, 0},
	{"rathma2", "boss0", 30062, 24047, 0, 0, 950},
	{"rathma2", "arena", 30062, 24047, 0, 0, 0},
	{"rathma3", "portal0", 30047, 25557, 0, 0, 0},
	{"rathma3", "portal1", 30062, 25562, 0, 0, 0},
	{"rathma3", "portal2", 30072, 25557, 0, 0, 0},
	{"rathma3", "marker", 30062, 25572, 0, 0, 0},
	{"rathma3", "boss0", 30059, 25547, 0, 0, 953},
	{"rathma3", "boss1", 30065, 25547, 0, 0, 952},
	{"rathma3", "arena", 30062, 25547, 0, 0, 0},
};
const placements::Reg reg(bindings, std::size(bindings), places, std::size(places));
}  // namespace
