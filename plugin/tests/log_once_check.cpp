// Host self-check of common/log_once.h (#2094).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target log_once_check
#include "log_once.h"

#include <cstdio>

int main() {
	using namespace pd2;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	LogOnce once;
	check("first sight logs", once.First(Key(7, 19)));
	check("the same item and page again does not", !once.First(Key(7, 19)));
	check("a hundred more passes do not", [&] { for (int i = 0; i < 100; ++i) { if (once.First(Key(7, 19))) return false; } return true; }());
	check("another item logs once", once.First(Key(8, 19)) && !once.First(Key(8, 19)));
	check("the same item on another page logs", once.First(Key(7, 20)));
	check("key 0,0 is not treated as seen", once.First(Key(0, 0)));
	for (uint32_t i = 100; i < 170; ++i) {
		once.First(Key(i, 1));
	}
	check("a key past 64 newer ones may log again", once.First(Key(7, 19)));
	std::printf(fail == 0 ? "all passed\n" : "%d failed\n", fail);
	return fail == 0 ? 0 : 1;
}
