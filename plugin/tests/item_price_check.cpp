// Host self-check of common/item_price.h (#1539): the gamble price of a stack of 100 throwing knives (tkf, gamble cost
// 352 in the merged weapons.txt, item level 2; 9tk 6820 / level 31; 7tk 16070 / level 64; all three stack 25-100), Charsi, Hell.
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target item_price_check
#include "item_price.h"

#include <cstdio>

int main() {
	using namespace pd2::itemprice;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	// D2R: midpoint (25 + 100) / 2 = 62; PD2: 1
	check("D2R gamble L20 = 51495", GamblePrice(20, 62, 352, 2, 31, 6820, 64, 16070) == 51495);
	check("PD2 gamble L20 = 4257", GamblePrice(20, 1, 352, 2, 31, 6820, 64, 16070) == 4257);
	check("D2R gamble L90 = 130330", GamblePrice(90, 62, 352, 2, 31, 6820, 64, 16070) == 130330);
	check("PD2 gamble L90 = 57061", GamblePrice(90, 1, 352, 2, 31, 6820, 64, 16070) == 57061);
	check("a book skips the quantity step", SkipsQuantity(true, false));
	check("a weapon skips it too", SkipsQuantity(false, true));
	check("anything else keeps it", !SkipsQuantity(false, false));
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
