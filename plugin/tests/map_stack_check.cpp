// Host self-check of common/map_stack.h (#1326).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target map_stack_check
#include "map_stack.h"

#include <cstdio>
#include <map>

namespace {
struct Fake {
	std::map<int, pd2::mapstack::Entry> stats;  // one entry per stat id
};
int32_t Read(Fake* u, uint32_t stat, pd2::mapstack::Entry* out, int32_t) {
	const auto it = u->stats.find(static_cast<int>(stat));
	if (it == u->stats.end()) {
		return 0;
	}
	out[0] = it->second;
	return 1;
}
Fake Map(int quantity, int level) {  // a single has no quantity entry, a stack has stat 70
	Fake f;
	f.stats[564] = {0, 564, level};
	if (quantity > 1) {
		f.stats[70] = {0, 70, quantity};
	}
	return f;
}
}  // namespace

int main() {
	int fail = 0;
	const auto check = [&](const char* name, bool ok) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name); fail += ok ? 0 : 1; };
	const auto diff = [](Fake& a, Fake& b) { return pd2::mapstack::FirstDifference(&a, &b, 600, 70, Read); };
	Fake single = Map(1, 87), pair = Map(2, 87), triple = Map(3, 87), other = Map(1, 97);
	check("single onto single", diff(single, single) == -1);
	check("single onto a stack of 2", diff(single, pair) == -1);
	check("stack of 2 onto a single", diff(pair, single) == -1);
	check("stack of 3 onto a stack of 2", diff(triple, pair) == -1);
	check("another Area Level differs", diff(single, other) == 564);
	Fake event = Map(1, 87);
	event.stats[553] = {1, 553, 63};
	check("a forced event differs", diff(single, event) == 553 && diff(event, single) == 553);
	check("m01 and m25 are map codes", pd2::mapstack::IsMapCode(0x2031306D) && pd2::mapstack::IsMapCode(0x2035326D));
	check("a rune is not", !pd2::mapstack::IsMapCode(0x20373272 /* r27 */ + 0) && !pd2::mapstack::IsMapCode(0x20657a67 /* gze */));
	// The split's copy: a piece with no stats, and one that kept the Area Level, both end equal to the source
	Fake src = Map(3, 87), bare, kept = Map(1, 87);
	src.stats[553] = {1, 553, 63};
	const auto set = [](Fake* u, int32_t stat, int32_t value, int32_t layer) {
		u->stats[stat] = {static_cast<uint16_t>(layer), static_cast<uint16_t>(stat), value};
	};
	pd2::mapstack::Saved saved;
	pd2::mapstack::Collect(&src, 600, 70, Read, saved);
	check("collect skips the quantity", saved.count == 2);
	check("apply sets both stats", pd2::mapstack::Apply(&bare, saved, set) == 2 && diff(src, bare) == 70 - 70 - 1);
	pd2::mapstack::Apply(&kept, saved, set);
	pd2::mapstack::Apply(&kept, saved, set);
	check("apply twice, or over kept stats, is the same", diff(src, kept) == -1);
	return fail;
}
