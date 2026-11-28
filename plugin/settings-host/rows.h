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

// The panel a row's widgets live in: Pyre's in the host's own panel, opened from Esc > Loot Filter
// (aa-settings-host/lootfilterhd.json, #2243), the rest in Options > Gameplay.
inline constexpr char OptionsPanel[] = "GameplayOptionsPanel";
inline constexpr char LootFilterPanelId[] = "LootFilter";
inline constexpr char LootFilterPanel[] = "aa-settings-host/LootFilter";
inline auto PanelOf(const std::string& row) -> const char* {
	return row.starts_with("pyre-lootfilter/") ? LootFilterPanel : OptionsPanel;
}

// A toggle row's button is "<row>:off" (the unticked box, always shown); "<row>:on", the ticked art over it, shows the value.
inline auto ToggleWidget(const std::string& row, bool on) -> std::string {
	return row + (on ? ":on" : ":off");
}

// A dropdown row is D2R's own DropdownListWidget named `widget`: the row name with ":" for "/", as D2R's child find
// reads a "/" as a path step. A pick sends PanelManager:OpenPanel with no text. Its states are static layout text, so
// the layout and this table list the same options in the same order: state i stands for the owner's label labels[i].
// Pyre names a filter by its "name", and adds " (<file stem>)" when two share one (a player's own filter.json named
// "PD2R Starter" lists as "PD2R Starter (Legacy)"), so a state matches either form.
// An option with no layout state (a player's own filter) gets an extra state the plugin adds at runtime (ExtraOptions).
struct Dropdown {
	const char* row;
	const char* widget;
	const char* const* labels;
	const char* const* stems;  // nullptr, or one per label
	uint32_t count;
};

inline constexpr const char* LootFilterLabels[] = {"None (Off)",       "PD2R Starter",     "PD2R Regular",
                                                   "PD2R Semi strict", "PD2R Strict",      "PD2R Very strict",
                                                   "PD2R Ultra strict", "PD2R Endgame"};
inline constexpr const char* LootFilterStems[] = {"",
                                                  "pd2r-starter",     "pd2r-regular",     "pd2r-semi-strict",
                                                  "pd2r-strict",      "pd2r-very-strict", "pd2r-ultra-strict",
                                                  "pd2r-endgame"};
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

// D2R's dropdown holds at most this many states.
inline constexpr uint32_t MaxStates = 250;

// The options the layout has no state for (a player's own filter): the plugin adds them after the layout's states,
// in the owner's order, so the extra k is state d.count + k.
inline auto ExtraOptions(const Dropdown& d, const std::vector<std::string>& options) -> std::vector<std::string> {
	std::vector<std::string> extra;
	for (const std::string& option : options) {
		if (StateOf(d, option) < 0 && d.count + extra.size() < MaxStates) {
			extra.push_back(option);
		}
	}
	return extra;
}

// The state that shows the owner's option i: the layout's, else its extra state, else -1.
inline auto StateFor(const Dropdown& d, const std::vector<std::string>& options, size_t i) -> int32_t {
	if (i >= options.size()) {
		return -1;
	}
	if (const int32_t state = StateOf(d, options[i]); state >= 0) {
		return state;
	}
	uint32_t state = d.count;
	for (size_t j = 0; j < i; ++j) {
		state += StateOf(d, options[j]) < 0 ? 1 : 0;
	}
	return state < MaxStates ? static_cast<int32_t>(state) : -1;
}

// The owner's option a picked state stands for: the first one it matches, or -1.
inline auto OptionOf(const Dropdown& d, int32_t state, const std::vector<std::string>& options) -> int32_t {
	for (size_t i = 0; i < options.size(); ++i) {
		if (state >= 0 && StateFor(d, options, i) == state) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

}  // namespace settings_rows
