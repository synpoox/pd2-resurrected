// Host self-check of plugin/common/parry_roll.h (#1466): the cap, Lucion's third, the roll test.
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target parry_roll_check
#include "parry_roll.h"

#include <cstdio>
#include <cstdlib>

namespace pp = pd2::parry;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

int main() {
	CHECK("no block chance, no parry", pp::Chance(0, false) == 0 && pp::Chance(-5, false) == 0);
	CHECK("chance passes through under the cap", pp::Chance(40, false) == 40);
	CHECK("chance is capped at 75", pp::Chance(120, false) == 75 && pp::Chance(75, false) == 75);
	CHECK("Lucion's hits: a third, cap first", pp::Chance(60, true) == 20 && pp::Chance(120, true) == 25);
	CHECK("roll 39 parries at 40, roll 40 does not", pp::Parries(40, 39) && !pp::Parries(40, 40));
	CHECK("roll uses % 100", pp::Parries(40, 139) && !pp::Parries(40, 140));
	CHECK("chance 0 never parries", !pp::Parries(0, 0));
	int hits = 0;
	for (uint32_t r = 0; r < 100; ++r) hits += pp::Parries(pp::Chance(200, false), r) ? 1 : 0;
	CHECK("capped chance parries 75 of 100 rolls", hits == 75);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
