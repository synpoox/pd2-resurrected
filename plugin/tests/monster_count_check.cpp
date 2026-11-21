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

	// room list: {populated, next} per room, a pointer chain
	struct Room {
		const Room* next;
		bool populated;
	};
	const Room c {nullptr, true}, b {&c, true}, a {&b, true};
	const auto pop = [](const uint8_t* r) { return reinterpret_cast<const Room*>(r)->populated; };
	check("every room populated: explored", Explored(reinterpret_cast<const uint8_t*>(&a), 0, pop));
	const Room d {nullptr, false}, e {&d, true};
	check("1 of 2 rooms populated: not explored", !Explored(reinterpret_cast<const uint8_t*>(&e), 0, pop));
	check("no room list: not explored", !Explored(nullptr, 0, pop));
	int visited = 0;
	const Room g {nullptr, true}, f {&g, false};
	Explored(reinterpret_cast<const uint8_t*>(&f), 0, [&](const uint8_t* r) { ++visited; return pop(r); });
	check("every room is visited even after an unpopulated one", visited == 2);
	Room loop1 {nullptr, true}, loop2 {&loop1, true};
	loop1.next = &loop2;
	check("a looping list is not explored", !Explored(reinterpret_cast<const uint8_t*>(&loop1), 0, pop));

	// level A fully seen, a trip to town and back: A still reads exact
	SeenRooms seen;
	const auto seenPop = [&](int32_t lv) { return [&seen, lv](const uint8_t* r) { return seen.Has(lv, r); }; };
	seen.Add(8, &a); seen.Add(8, &b); seen.Add(8, &c);
	seen.Add(1, &d);
	check("level seen fully: exact", Explored(reinterpret_cast<const uint8_t*>(&a), 0, seenPop(8)));
	check("another level's rooms do not count", !Explored(reinterpret_cast<const uint8_t*>(&a), 0, seenPop(1)));
	check("after town and back, still exact", Explored(reinterpret_cast<const uint8_t*>(&a), 0, seenPop(8)) && seen.Count(8) == 3);

	// PD2's 90%: 9 of 10 rooms is explored, 8 of 10 is not
	Room ten[10] {};
	for (int i = 0; i < 9; ++i) {
		ten[i].next = &ten[i + 1];
	}
	const auto upTo = [&](int n) { return [&ten, n](const uint8_t* r) { return reinterpret_cast<const Room*>(r) - ten < n; }; };
	check("9 of 10 rooms: explored", Explored(reinterpret_cast<const uint8_t*>(ten), 0, upTo(9)));
	check("8 of 10 rooms: not explored", !Explored(reinterpret_cast<const uint8_t*>(ten), 0, upTo(8)));

	char text[64];
	const auto says = [&](int32_t n, bool explored, const char* want) {
		Text(n, explored, text, sizeof text);
		return std::strcmp(text, want) == 0;
	};
	check("text, explored", says(12, true, "Monsters Remaining: 12"));
	check("text, not explored: plus", says(12, false, "Monsters Remaining: 12+"));
	check("text, 25 is exact", says(25, true, "Monsters Remaining: 25"));
	check("text, 26 is 25+", says(26, true, "Monsters Remaining: 25+"));
	check("text, 300 not explored is 25+", says(300, false, "Monsters Remaining: 25+"));
	check("text, 0", says(0, true, "Monsters Remaining: 0"));
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
