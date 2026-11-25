// settings-host's rows without engine types (#2237): the layout widget names a row shows itself through, and the
// click step. tests/settings_rows_check.cpp checks them.
#pragma once

#include <cstdint>
#include <string>

namespace settings_rows {

// A click on a row sends PanelManager:OpenPanel:<ClickPrefix><plugin>/<key> (the core-ui padlock's route).
inline constexpr char ClickPrefix[] = "aa-settings-host/";

inline auto RowName(const std::string& plugin, const std::string& key) -> std::string {
	return plugin + "/" + key;
}

// A toggle row's button is "<row>:off" (the unticked box, always shown); "<row>:on", the ticked art over it, shows the value.
inline auto ToggleWidget(const std::string& row, bool on) -> std::string {
	return row + (on ? ":on" : ":off");
}

// A dropdown row's button is "<row>"; over it one text per option label the layout knows, "<row>=<label>", and
// "<row>=?" for any other label. Only the chosen one shows.
inline auto OptionWidget(const std::string& row, const std::string& label) -> std::string {
	return row + "=" + label;
}

// One click steps to the next option and wraps (no widget text can change on D2RLoader 1.3.1, so no list opens).
inline auto NextIndex(int32_t index, uint32_t count) -> int32_t {
	if (count == 0) {
		return 0;
	}
	return index < 0 || static_cast<uint32_t>(index) + 1 >= count ? 0 : index + 1;
}

}  // namespace settings_rows
