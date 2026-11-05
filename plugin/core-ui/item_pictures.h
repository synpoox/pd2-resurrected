// HD pictures of 4-character item codes (#310, #298). D2R's items.json loader keys every entry by 3 characters, so a
// PD2 code like tpgs or r01s finds no picture and takes r01's (rva::ItemsJsonKey). A 32-byte patch keeps the 4th.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace item_pictures {

// Patches the loader's key (before the game reads items.json). Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

}  // namespace item_pictures
