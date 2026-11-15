// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Harrogath, where an uber key opens (PD2 0x1037d840): the Blood Fountain and the tier-0 re-entry grey portals by it.
const placements::Binding bindings[] = {{"harrogath", 109, 109, Source::Vanilla, false, false}};
const placements::Place places[] = {
	{"harrogath", "fountain", 5074, 5114, 0, 0, 0},
	{"harrogath", "grey0", 5076, 5119, 0, 0, 0},
	{"harrogath", "grey1", 5068, 5108, 0, 0, 0},
	{"harrogath", "grey2", 5079, 5110, 0, 0, 0},
};
const placements::Reg reg(bindings, std::size(bindings), places, std::size(places));
}  // namespace
