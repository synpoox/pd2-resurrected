// Host self-check of common/stash_stack.h (#1537).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target stash_stack_check
#include "stash_stack.h"

#include <cstdio>

int main() {
	using namespace pd2::stashstack;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	check("a single with no quantity stat is one item", Units(0) == 1);
	check("a stack of 12 is 12 items", Units(12) == 12);
	const auto is = [](TopUp t, int32_t added, int32_t left) { return t.added == added && t.left == left; };
	check("50 onto 250 adds 5, leaves 45", is(TopUpOf(250, 50, 255), 5, 45));
	check("10 onto 255 is refused (0 added)", is(TopUpOf(255, 10, 255), 0, 10));
	check("5 onto 250 adds 5, leaves 0", is(TopUpOf(250, 5, 255), 5, 0));
	check("50 onto 50 fits whole under 255", is(TopUpOf(50, 50, 255), 50, 0));
	check("50 onto 50 under 99 adds 49, leaves 1", is(TopUpOf(50, 50, 99), 49, 1));
	check("a single onto 254 fits", is(TopUpOf(254, 1, 255), 1, 0));
	check("a single onto 255 is refused", is(TopUpOf(255, 1, 255), 0, 1));
	check("a limit past 255 is clamped", is(TopUpOf(250, 50, 999), 5, 45));
	check("local: 50 onto 250 is taken", Takes(250, 50, 255, true));
	check("not local: 50 onto 250 is refused", !Takes(250, 50, 255, false));
	check("not local: 5 onto 250 is taken", Takes(250, 5, 255, false));
	check("local: 10 onto 255 is refused", !Takes(255, 10, 255, true));
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
