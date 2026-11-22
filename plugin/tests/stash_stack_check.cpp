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
	check("50 onto 50 fits under 255", Fits(50, 50, 255));
	check("50 onto 50 is refused under 99", !Fits(50, 50, 99));
	check("12 onto 243 fits (255)", Fits(243, 12, 255));
	check("12 onto 244 is refused (256)", !Fits(244, 12, 255));
	check("a single onto 254 fits", Fits(254, 1, 255));
	check("a single onto 255 is refused", !Fits(255, 1, 255));
	check("a limit past 255 is clamped", !Fits(250, 50, 999));
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
