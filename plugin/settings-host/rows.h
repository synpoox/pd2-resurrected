// settings-host's rows without engine types (#2237): the layout widget names a row shows itself through, and the
// dropdowns' option tables. tests/settings_rows_check.cpp checks them.
#pragma once

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace settings_rows {

// A toggle row's click sends PanelManager:OpenPanel:<ClickPrefix><plugin>/<key> (the core-ui padlock's route).
inline constexpr char ClickPrefix[] = "aa-settings-host/";

inline auto RowName(const std::string& plugin, const std::string& key) -> std::string {
	return plugin + "/" + key;
}

// A toggle row's button is "<row>:off" (the unticked box, always shown); "<row>:on", the ticked art over it, shows the value.
inline auto ToggleWidget(const std::string& row, bool on) -> std::string {
	return row + (on ? ":on" : ":off");
}

// A dropdown row is D2R's own DropdownListWidget named `widget`: the row name with ":" for "/", as D2R's child find
// reads a "/" as a path step. A pick sends PanelManager:OpenPanel with no text. Its states are static layout text, so
// the layout and this table list the same options in the same order: state i stands for the owner's label labels[i].
// Pyre names a filter by its "name", and adds " (<file stem>)" when two share one (the shipped filter.json is also
// "PD2R Starter"; Pyre lists it as "PD2R Starter (Legacy)"), so a state matches either form.
// shortcut: a filter with no state (filter.json, a player's own file) can't be picked here; they pick it in the toml.
// Upgrade when the loader can set a dropdown's states.
struct Dropdown {
	const char* row;
	const char* widget;
	const char* const* labels;
	const char* const* stems;  // nullptr, or one per label
	uint32_t count;
};

inline constexpr const char* LootFilterLabels[] = {"None (Off)",  "PD2R Starter",     "PD2R Regular",
                                                   "PD2R Strict", "PD2R Very strict", "PD2R Endgame"};
inline constexpr const char* LootFilterStems[] = {"", "pd2r-starter", "pd2r-regular", "pd2r-strict", "pd2r-very-strict", "pd2r-endgame"};
inline constexpr const char* RollRangeLabels[] = {"Hold Ctrl / RT", "Always Show"};
inline constexpr const char* GroundReticleLabels[] = {"Ice Blue", "Gold", "Emerald", "White", "Crimson", "Violet"};
inline constexpr const char* LockReticleLabels[] = {"Gold", "Crimson", "Emerald", "Ice Blue", "White", "Orange"};
inline constexpr Dropdown Dropdowns[] = {
	{"pyre-lootfilter/pyre-lootfilter_selected_filter", "pyre-lootfilter:pyre-lootfilter_selected_filter", LootFilterLabels,
	 LootFilterStems, static_cast<uint32_t>(std::size(LootFilterLabels))},
	{"item-roll-ranges/display_always_show_ranges", "item-roll-ranges:display_always_show_ranges", RollRangeLabels, nullptr,
	 static_cast<uint32_t>(std::size(RollRangeLabels))},
	{"controller-qol-updates/aim_ground_reticle_color", "controller-qol-updates:aim_ground_reticle_color", GroundReticleLabels,
	 nullptr, static_cast<uint32_t>(std::size(GroundReticleLabels))},
	{"controller-qol-updates/aim_lock_reticle_color", "controller-qol-updates:aim_lock_reticle_color", LockReticleLabels, nullptr,
	 static_cast<uint32_t>(std::size(LockReticleLabels))},
};

inline auto FindDropdown(const std::string& row) -> const Dropdown* {
	for (const Dropdown& d : Dropdowns) {
		if (row == d.row) {
			return &d;
		}
	}
	return nullptr;
}

// The state that shows an owner's option label, or -1 when the layout has none.
inline auto StateOf(const Dropdown& d, const std::string& label) -> int32_t {
	for (uint32_t i = 0; i < d.count; ++i) {
		if (label == d.labels[i] ||
		    (d.stems != nullptr && d.stems[i][0] != 0 && label == std::string(d.labels[i]) + " (" + d.stems[i] + ")")) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

// The owner's option a picked state stands for: the first one it matches, or -1.
inline auto OptionOf(const Dropdown& d, int32_t state, const std::vector<std::string>& options) -> int32_t {
	for (size_t i = 0; i < options.size(); ++i) {
		if (state >= 0 && StateOf(d, options[i]) == state) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

}  // namespace settings_rows
