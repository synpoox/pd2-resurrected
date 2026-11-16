// Host self-check of plugin/common/event_roll.h (#950): the odds by map quality and the Altar redraw when forced.
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target event_roll_check
#include "event_roll.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace er = pd2::event_roll;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

int main() {
	CHECK("odds: normal 11, magic 17, rare 34", er::Chance(er::QualityNormal) == 11 && er::Chance(er::QualityMagic) == 17 && er::Chance(6) == 34);
	// the first rand() is the chance roll (percent), the next ones the draws
	int values[8], n = 0;
	const auto fixed = [&] { return values[n++]; };
	const auto run = [&](int quality, bool forced, std::initializer_list<int> v) {
		n = 0;
		int i = 0;
		for (int x : v) values[i++] = x;
		return er::Pick(quality, forced, fixed);
	};
	CHECK("normal map: roll 10 carries an event", run(er::QualityNormal, false, {10, 1}) == 11);
	CHECK("normal map: roll 11 carries none", run(er::QualityNormal, false, {11, 1}) == er::NoEvent);
	CHECK("magic map: roll 16 yes, 17 no", run(er::QualityMagic, false, {16, 4}) == 14 && run(er::QualityMagic, false, {17, 4}) == er::NoEvent);
	CHECK("rare map: roll 33 yes, 34 no", run(6, false, {33, 8}) == 18 && run(6, false, {34, 8}) == er::NoEvent);
	CHECK("a draw of 0 draws again", run(6, false, {0, 0, 3, 5}) == 15);
	CHECK("unforced map can draw the Altar", run(6, false, {0, 9}) == er::Altar);
	CHECK("forced map redraws the Altar", run(er::QualityNormal, true, {9, 5}) == 15);
	CHECK("forced map skips the chance roll", run(er::QualityNormal, true, {1}) == 11);
	CHECK("forced map with 40 empty draws is no event", [&] { n = 0; return er::Pick(er::QualityNormal, true, [] { return 0; }) == er::NoEvent; }());
	const auto noTomb = [&] {
		n = 0;
		values[0] = 0, values[1] = 11, values[2] = 5;
		return er::Pick(6, false, fixed, false);
	};
	CHECK("Tomb (draw 11) is drawn while the Bitter Depths is built", run(6, false, {0, 11}) == er::Tomb);
	CHECK("Tomb redraws while the Bitter Depths is not built", noTomb() == 15);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
