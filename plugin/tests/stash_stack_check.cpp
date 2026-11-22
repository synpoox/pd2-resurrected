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
	check("a single sees the count as is", CheckedCount(40, 1) == 40);
	check("12 onto 243 fits (255)", CheckedCount(243, 12) < TabMax);
	check("12 onto 244 is refused (256)", CheckedCount(244, 12) >= TabMax);
	check("a single onto 254 fits", CheckedCount(254, 1) < TabMax);
	check("a single onto 255 is refused", CheckedCount(255, 1) >= TabMax);check("12 onto 99 fits past the old 99", CheckedCount(99, 12) < TabMax);
	check("no wrap past 255", CheckedCount(250, 50) == 255);
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
