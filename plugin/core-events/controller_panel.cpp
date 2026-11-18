#include "controller_panel.h"

#include "log.h"

auto RegisterPanelControllerRoute(const D2RL::PluginContext* context, const D2RL::PanelService* panels,
                                  D2RL::Panels::RegistrationHandle panel, const char* labelKey) noexcept -> bool {
	using namespace D2RL::Panels;
	if (!D2RL::HasPanelServiceField(panels, D2RL::PanelServiceRegisterControllerRouteFieldEnd)) {
		plog::Info("controller: this D2RLoader has no controller routes");
		return false;
	}
	const ControllerRouteRegistration route {.structSize = ControllerRouteRegistrationSize,
	                                         .flags = ControllerRouteFlags::PairWithPlayerInventory,
	                                         .panel = panel,
	                                         .placement = ControllerRoutePlacement::AfterPlayerInventory,
	                                         .reserved = 0,
	                                         .localizedLabelKey = labelKey};
	ControllerRouteHandle handle = InvalidControllerRouteHandle;
	const auto r = panels->registerControllerRoute(context, &route, &handle);
	plog::Info("controller: route for the panel -> %u", static_cast<uint32_t>(r));
	return r == Result::Success;
}
