// Host self-check of plugin/common/shrine_math.h (#1143).
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target shrine_rules_check
#include "shrine_math.h"

#include <cstdio>

namespace sh = pd2::shrines;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

int main() {
	CHECK("codes 6-15 are shared", sh::IsBooster(6) && sh::IsBooster(15) && sh::IsBooster(11));
	CHECK("others are not", !sh::IsBooster(5) && !sh::IsBooster(16) && !sh::IsBooster(18));
	CHECK("distance 21 shares (441)", sh::InShareRange(0, 0, 21, 0));
	CHECK("distance 22 does not (484)", !sh::InShareRange(0, 0, 22, 0));
	CHECK("diagonal 15,15 does not (450, PD2 is < 450)", !sh::InShareRange(100, 100, 115, 115));
	CHECK("diagonal 14,15 shares (421)", sh::InShareRange(100, 100, 114, 115));
	CHECK("diagonal 16,15 does not", !sh::InShareRange(100, 100, 116, 115));
	CHECK("fire raises maxfireresist", sh::MaxResistStat(8) == 40);
	CHECK("cold raises maxcoldresist", sh::MaxResistStat(9) == 44);
	CHECK("lightning raises maxlightresist", sh::MaxResistStat(10) == 42);
	CHECK("poison raises maxpoisonresist", sh::MaxResistStat(11) == 46);
	CHECK("armor raises none", sh::MaxResistStat(6) == 0 && sh::MaxResistStat(12) == 0);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
