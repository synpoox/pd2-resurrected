// Host self-check of settings-host/rows.h (#2237).
#include "../settings-host/rows.h"

#include <cstdio>

int main() {
	using namespace settings_rows;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	const std::string row = RowName("pyre-lootfilter", "pyre-lootfilter_selected_filter");
	check("row name is plugin/key", row == "pyre-lootfilter/pyre-lootfilter_selected_filter");
	check("toggle buttons", ToggleWidget("a/b", true) == "a/b:on" && ToggleWidget("a/b", false) == "a/b:off");
	check("option button", OptionWidget("a/b", "PD2R Starter") == "a/b=PD2R Starter");
	check("click steps on", NextIndex(0, 6) == 1 && NextIndex(4, 6) == 5);
	check("click wraps from the last", NextIndex(5, 6) == 0);
	check("an index out of range goes to the first", NextIndex(-1, 6) == 0 && NextIndex(9, 6) == 0);
	check("no or one option stays at 0", NextIndex(0, 0) == 0 && NextIndex(0, 1) == 0);
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
