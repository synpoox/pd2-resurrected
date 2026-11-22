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
	check("towns hidden, boss areas not", IsTown(1) && IsTown(40) && IsTown(75) && IsTown(103) && IsTown(109) && !IsTown(120) && !IsTown(132) && !IsTown(136));

	uint8_t region[0x300] = {};
	const int32_t spawned = 20, killed = 8;
	std::memcpy(region + 0x2cc, &spawned, 4);
	std::memcpy(region + 0x2d0, &killed, 4);
	const uint8_t* regions[4] = {nullptr, region, nullptr, region};
	Ledger l;
	check("reads region[1]", Read(regions, 1, 4, 0x2cc, 0x2d0, &l) && l.spawned == 20 && l.killed == 8);
	check("a null region is no ledger", !Read(regions, 2, 4, 0x2cc, 0x2d0, &l));
	check("a negative level is refused", !Read(regions, -1, 4, 0x2cc, 0x2d0, &l));
	check("a level past the cap is refused", !Read(regions, MaxLevel, MaxLevel + 1, 0x2cc, 0x2d0, &l));
	check("no array is refused", !Read(nullptr, 1, 4, 0x2cc, 0x2d0, &l));
	const int32_t junk = -5;
	std::memcpy(region + 0x2d0, &junk, 4);
	check("a negative count is no ledger", !Read(regions, 1, 4, 0x2cc, 0x2d0, &l));
	const int32_t huge = 0x7fffffff;
	std::memcpy(region + 0x2d0, &huge, 4);
	check("a huge count is no ledger", !Read(regions, 3, 4, 0x2cc, 0x2d0, &l));

	check("a level past the table's row count is refused", !Read(regions, 3, 3, 0x2cc, 0x2d0, &l));

	char text[64];
	const auto says = [&](int32_t n, Rooms rooms, const char* want) {
		Text(n, Explored(rooms), text, sizeof text);
		return std::strcmp(text, want) == 0;
	};
	const Rooms all {10, 10}, half {10, 5}, most {20, 19}, nine {10, 9}, under {100, 89};
	check("explored, 12: the number, no plus", says(12, all, "Monsters Remaining: 12"));
	check("explored, 50: the number, no plus", says(50, all, "Monsters Remaining: 50"));
	check("explored, 51 is 50+", says(51, all, "Monsters Remaining: 50+"));
	check("explored, 300 is 50+", says(300, all, "Monsters Remaining: 50+"));
	check("explored, 0", says(0, all, "Monsters Remaining: 0"));
	check("50% explored, 10 left: 50+", says(10, half, "Monsters Remaining: 50+"));
	check("95% explored, 10 left: 10", says(10, most, "Monsters Remaining: 10"));
	check("exactly 90%: explored", says(3, nine, "Monsters Remaining: 3"));
	check("89%: not explored", says(3, under, "Monsters Remaining: 50+"));
	check("no rooms: not explored", !Explored({}));

	// the region's room counts (+0x04 made, +0x0c counted): Blood Moor read 18 made of 47 counted (#1729)
	uint8_t counts[0x10] = {};
	const int32_t made = 18, total = 47, unset = -1;
	std::memcpy(counts + 0x04, &made, 4);
	std::memcpy(counts + 0x0c, &total, 4);
	const Rooms read = ReadRooms(counts, 0x04, 0x0c);
	check("region rooms: 18 of 47", read.made == 18 && read.total == 47 && !Explored(read));
	std::memcpy(counts + 0x04, &total, 4);
	check("region rooms: all 47 made is explored", Explored(ReadRooms(counts, 0x04, 0x0c)));
	std::memcpy(counts + 0x0c, &unset, 4);
	check("region rooms: an unset count (-1) is no rooms", ReadRooms(counts, 0x04, 0x0c).total == 0);
	std::memcpy(counts + 0x0c, &huge, 4);
	check("region rooms: a huge count is no rooms", ReadRooms(counts, 0x04, 0x0c).total == 0);
	std::memcpy(counts + 0x0c, &total, 4);
	std::memcpy(counts + 0x04, &junk, 4);
	check("region rooms: a negative made count is no rooms", ReadRooms(counts, 0x04, 0x0c).total == 0);

	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
