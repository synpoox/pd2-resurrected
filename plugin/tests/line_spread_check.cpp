// Host self-check of plugin/common/line_spread.h (#1056): Multiple Shot's line, and its triggered rule (0 = none).
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target line_spread_check
#include "line_spread.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ls = pd2::line_spread;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

struct Missile {
	int32_t x, y;
	bool outer;
};

// The missiles of a spread from the origin to (tx, ty), in creation order
static std::vector<Missile> Run(int32_t tx, int32_t ty, int32_t count, int32_t triggered) {
	std::vector<Missile> out;
	ls::Spread(0, 0, tx, ty, count, triggered, [&](int32_t x, int32_t y, bool outer) {
		out.push_back({x, y, outer});
		return true;
	});
	return out;
}

// One letter per missile: T = outer (NotTriggering), F = triggering
static std::string Pattern(const std::vector<Missile>& m) {
	std::string s;
	for (const Missile& x : m) s += x.outer ? 'T' : 'F';
	return s;
}

int main() {
	CHECK("triggered 0 is none: five outer", Pattern(Run(10, 0, 5, 0)) == "TTTTT");
	CHECK("triggered 3 of 5: the ends are outer", Pattern(Run(10, 0, 5, 3)) == "TFFFT");
	CHECK("triggered 1 of 5: the middle one triggers", Pattern(Run(10, 0, 5, 1)) == "TTFTT");
	CHECK("triggered above count is all triggering", Pattern(Run(10, 0, 5, 9)) == "FFFFF");
	CHECK("triggered below 0 is none", Pattern(Run(10, 0, 5, -4)) == "TTTTT");
	CHECK("one missile, triggered 1 triggers", Pattern(Run(10, 0, 1, 1)) == "F");
	CHECK("one missile, triggered 0 does not", Pattern(Run(10, 0, 1, 0)) == "T");
	const auto line = Run(10, 0, 5, 3);
	CHECK("the line starts count/2 steps off the target and passes through it", line.size() == 5 && line[0].x == 10 && line[0].y == 2 && line[2].x == 10 && line[2].y == 0);
	CHECK("the line runs at right angles to the aim", line[0].x == line[4].x && line[0].y > line[4].y);
	CHECK("every created missile counts", ls::Spread(0, 0, 10, 0, 5, 3, [](int32_t, int32_t, bool) { return true; }) == 5);
	CHECK("a refused missile does not count", ls::Spread(0, 0, 10, 0, 5, 3, [](int32_t, int32_t, bool outer) { return !outer; }) == 3);
	CHECK("a zero aim does not loop", Run(0, 0, 3, 3).size() == 3);

	return g_fail == 0 ? 0 : 1;
}
