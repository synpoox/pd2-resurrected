// PD2's inventory hotkeys, configurable (#1567).
#pragma once

#include <D2RLPlugin/api.h>

namespace hotkeys {

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

}
