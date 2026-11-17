// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Bitter Depths (#960): level 151 is Cartographer's square Mesa arena (arenas.txt),
// the Void/Tomb event's area. Its monsters are the level row's own mon* columns (PD2's Tomb Event Map 198), and its
// spots are none: the portal lands at the arrival. So this file holds no places.
const placements::Binding bindings[] = {{"bitterdepths", 151, 151, Source::Private, false, false}};
const placements::Reg reg(bindings, std::size(bindings));
}  // namespace
