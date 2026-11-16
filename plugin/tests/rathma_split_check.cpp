// Host self-check of common/rathma_split.h (#645).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target rathma_split_check
#include "rathma_split.h"

#include <climits>
#include <cstdio>

int main() {
	using pd2::rathma::Half;
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	check("even halves", Half(100) == 50);
	check("odd truncates", Half(7) == 3 && Half(1) == 0);
	check("zero and negative", Half(0) == 0 && Half(-7) == -3);
	check("INT32_MAX does not overflow", Half(INT_MAX) == 1073741823);
	return fail;
}
