// Host self-check of common/map_boss_tier.h (#1423).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target map_boss_tier_check
#include "map_boss_tier.h"

#include <cstdio>
#include <cstring>

int main() {
	int bad = 0;
	const auto expect = [&](const char* configured, int tier, const char* want) {
		const char* got = pd2::mapboss::TcForTier(configured, tier);
		const bool ok = want == nullptr ? got == nullptr : got != nullptr && std::strcmp(got, want) == 0;
		if (!ok) {
			std::printf("FAIL %s at tier %d: %s, wanted %s\n", configured != nullptr ? configured : "null", tier, got != nullptr ? got : "null",
			            want != nullptr ? want : "null");
			++bad;
		}
	};
	for (int from = 1; from <= 3; ++from) {
		const char* plain[] = {"Map Boss T1", "Map Boss T2", "Map Boss T3"};
		const char* blood[] = {"Map Boss T1 Blood", "Map Boss T2 Blood", "Map Boss T3 Blood"};
		for (int tier = 1; tier <= 3; ++tier) {
			expect(plain[from - 1], tier, plain[tier - 1]);
			expect(blood[from - 1], tier, blood[tier - 1]);
		}
	}
	expect("Map Boss T4", 2, nullptr);  // the T4 class is not a map tier
	expect("Act 5 (H) Good", 2, nullptr);
	expect("Map Boss T1", 0, nullptr);  // no open map
	expect("Map Boss T1", 4, nullptr);
	expect(nullptr, 1, nullptr);
	std::puts(bad == 0 ? "map_boss_tier_check: ok" : "map_boss_tier_check: FAILED");
	return bad == 0 ? 0 : 1;
}
