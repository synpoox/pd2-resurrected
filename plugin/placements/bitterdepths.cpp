// Placements of one encounter (#947): a private author edits this file only (docs/cartographer-contracts.md).
#include "placements.h"

#include <iterator>

namespace {
using placements::Source;
// Bitter Depths, the Void/Tomb event's fixed area (PD2 207, the port's Tomb Event Map 198). Lane C (#960) flips it.
const placements::Binding bindings[] = {{"bitterdepths", 198, 215, Source::Existing, false, false}};
const placements::Reg reg(bindings, std::size(bindings));
}  // namespace
