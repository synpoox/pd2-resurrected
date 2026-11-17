// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Lucion's arena (PD2 188) and the Temple of the Triune (PD2 189): Cartographer's 1x1 Desert arenas (arenas.txt, #962). Their spots (centre, temple-portal, the altar and chests) are named in
// arenaspots.txt and read through cartographer.arena.v1 (core-events arena_spots.h), so this file holds no places.
const placements::Binding bindings[] = {
	{"lucion", 213, 213, Source::Private, true, false},
	{"temple", 214, 214, Source::Private, false, false},
};
const placements::Reg reg(bindings, std::size(bindings));
}  // namespace
