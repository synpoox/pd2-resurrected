// Host self-check of settings-host/rows.h (#2237).
#include "../settings-host/rows.h"

#include <cstdio>

int main() {
	using namespace settings_rows;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	const std::string row = RowName("pyre-lootfilter", "pyre-lootfilter_selected_filter");
	check("row name is plugin/key", row == "pyre-lootfilter/pyre-lootfilter_selected_filter");
	check("Pyre's rows live in Esc > Loot Filter", std::string(PanelOf(row)) == LootFilterPanel &&
	                                                   std::string(PanelOf("item-roll-ranges/display_always_show_ranges")) == OptionsPanel);
	check("toggle buttons", ToggleWidget("a/b", true) == "a/b:on" && ToggleWidget("a/b", false) == "a/b:off");
	const Dropdown* loot = FindDropdown("pyre-lootfilter/pyre-lootfilter_selected_filter");
	check("the loot filter has a dropdown", loot != nullptr && loot->count == 8 && FindDropdown("a/b") == nullptr);
	check("a label maps to its state", StateOf(*loot, "None (Off)") == 0 && StateOf(*loot, "PD2R Endgame") == 7);
	check("the new levels sit in order", StateOf(*loot, "PD2R Semi strict") == 3 && StateOf(*loot, "PD2R Ultra strict (pd2r-ultra-strict)") == 6);
	check("an unknown label has none", StateOf(*loot, "my own filter") == -1);
	check("a stem-suffixed label maps", StateOf(*loot, "PD2R Starter (pd2r-starter)") == 1 && StateOf(*loot, "PD2R Starter (Legacy)") == -1);
	const std::vector<std::string> pyre = {"None (Off)", "PD2R Endgame", "PD2R Starter (Legacy)", "PD2R Starter (pd2r-starter)"};
	check("a pick finds the owner's option", OptionOf(*loot, 1, pyre) == 3 && OptionOf(*loot, 7, pyre) == 1 && OptionOf(*loot, 0, pyre) == 0);
	check("a pick with no option", OptionOf(*loot, 2, pyre) == -1 && OptionOf(*loot, -1, pyre) == -1);
	const std::vector<std::string> own = {"None (Off)", "synpoo", "PD2R Starter", "mine"};
	check("a player's filters get extra states", ExtraOptions(*loot, own) == std::vector<std::string> {"synpoo", "mine"} &&
	                                                 StateFor(*loot, own, 1) == 8 && StateFor(*loot, own, 3) == 9 &&
	                                                 StateFor(*loot, own, 2) == 1 && StateFor(*loot, own, 4) == -1);
	check("an extra state picks its option", OptionOf(*loot, 9, own) == 3 && OptionOf(*loot, 8, own) == 1 && OptionOf(*loot, 10, own) == -1);
	const Dropdown* ranges = FindDropdown("item-roll-ranges/display_always_show_ranges");
	check("roll ranges has two states", ranges != nullptr && StateOf(*ranges, "Always Show") == 1);
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
