// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// The vanilla mini-uber levels (D2R 133-135) and D2R's Uber Tristram arena (136): bindings only. They keep D2R's own
// areas, so no replacement arena is reserved. Tristram needs no places: the stock arena spawns the primes, which
// run on PD2's AI, minions and drops (map_bosses.cpp, prime_minions.cpp, uber_tristram.cpp).
const placements::Binding bindings[] = {
	{"miniuber1", 133, 133, Source::Vanilla, false, false},
	{"miniuber2", 134, 134, Source::Vanilla, false, false},
	{"miniuber3", 135, 135, Source::Vanilla, false, false},
	{"tristram", 136, 136, Source::D2RTristram, true, true},
};
const placements::Reg reg(bindings, std::size(bindings));
}  // namespace
