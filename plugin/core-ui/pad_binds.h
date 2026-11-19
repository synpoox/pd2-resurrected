// Pad chords on an item (#1472), SDK only. The SDK has no focused-item read, so a chord is held with A and acts on the item
// A activates (the ItemInteractionService event), as the stack split does.
#pragma once

#include <D2RLPlugin/api.h>

namespace pad_binds {

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

}
