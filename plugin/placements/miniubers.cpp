// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// The vanilla mini-uber levels (D2R 133-135) and D2R's Uber Tristram arena (136): bindings only. They keep D2R's own
// areas, so no replacement arena is reserved. Tristram is `Existing` 181 until lane D (#957) points it at D2R's 136
// (with PD2's bosses) and adds its placements here; the three mini-ubers have none.
const placements::Binding bindings[] = {
	{"miniuber1", 133, 133, Source::Vanilla, false, false},
	{"miniuber2", 134, 134, Source::Vanilla, false, false},
	{"miniuber3", 135, 135, Source::Vanilla, false, false},
	{"tristram", 181, 136, Source::D2RTristram, true, true},
};
const placements::Reg reg(bindings, std::size(bindings));
}  // namespace
