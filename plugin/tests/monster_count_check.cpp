// Host self-check of common/monster_count.h (#1534).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target monster_count_check
#include "monster_count.h"

#include <cstdio>
#include <cstring>

int main() {
	using namespace pd2::monstercount;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	check("12 spawned, 5 killed: 7 remain", Remaining({12, 5}) == 7);
	check("all killed: 0 remain, still shown", Remaining({9, 9}) == 0 && Shown({9, 9}));
	check("more kills than spawns never goes negative", Remaining({3, 4}) == 0);
	check("nothing spawned: hidden", !Shown({0, 0}));
	check("one spawned: shown", Shown({1, 0}));

	uint8_t region[0x300] = {};
	const int32_t spawned = 20, killed = 8;
	std::memcpy(region + 0x2cc, &spawned, 4);
	std::memcpy(region + 0x2d0, &killed, 4);
	const uint8_t* regions[4] = {nullptr, region, nullptr, region};
	Ledger l;
	check("reads region[1]", Read(regions, 1, 0x2cc, 0x2d0, &l) && l.spawned == 20 && l.killed == 8);
	check("a null region is no ledger", !Read(regions, 2, 0x2cc, 0x2d0, &l));
	check("a negative level is refused", !Read(regions, -1, 0x2cc, 0x2d0, &l));
	check("a level past the cap is refused", !Read(regions, MaxLevel, 0x2cc, 0x2d0, &l));
	check("no array is refused", !Read(nullptr, 1, 0x2cc, 0x2d0, &l));
	const int32_t junk = -5;
	std::memcpy(region + 0x2d0, &junk, 4);
	check("a negative count is no ledger", !Read(regions, 1, 0x2cc, 0x2d0, &l));
	const int32_t huge = 0x7fffffff;
	std::memcpy(region + 0x2d0, &huge, 4);
	check("a huge count is no ledger", !Read(regions, 3, 0x2cc, 0x2d0, &l));

	char text[64];
	Text({20, 8}, text, sizeof text);
	check("text", std::strcmp(text, "Monsters remaining: 12") == 0);
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
