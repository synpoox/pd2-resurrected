// Controller support for a plugin panel, kept apart from the panel code so other panels can reuse it.
#pragma once

#include <D2RLPlugin/panels.h>

// Plugin load: puts `panel` in the controller UI switcher beside the player inventory. D2R then owns the focus, the
// D-pad moves, B to close and the help bar, from the panel's controller layout. Only one route can exist globally.
// `labelKey` is a D2R string key starting with '@'. Returns true when the route is registered.
auto RegisterPanelControllerRoute(const D2RL::PluginContext* context, const D2RL::PanelService* panels,
                                  D2RL::Panels::RegistrationHandle panel, const char* labelKey) noexcept -> bool;
