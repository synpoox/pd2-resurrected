// Host self-check of common/map_stack.h (#1326).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target map_stack_check
#include "map_stack.h"

#include <cstdio>
#include <cstring>
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
	f.stats[564] = {0, 564, level, 0};
	if (quantity > 1) {
		f.stats[70] = {0, 70, quantity, 0};
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
	event.stats[553] = {1, 553, 63, 0};
	check("a forced event differs", diff(single, event) == 553 && diff(event, single) == 553);
	check("m01 and m25 are map codes", pd2::mapstack::IsMapCode(0x2031306D) && pd2::mapstack::IsMapCode(0x2035326D));
	check("a rune is not", !pd2::mapstack::IsMapCode(0x20373272 /* r27 */ + 0) && !pd2::mapstack::IsMapCode(0x20657a67 /* gze */));
	// The split's copy: a piece with no stats, and one that kept the Area Level, both end equal to the source
	Fake src = Map(3, 87), bare, kept = Map(1, 87);
	src.stats[553] = {1, 553, 63, 0};
	const auto set = [](Fake* u, int32_t stat, int32_t value, int32_t layer) {
		u->stats[stat] = {layer, stat, value, 0};
	};
	pd2::mapstack::Saved saved;
	pd2::mapstack::Collect(&src, 600, 70, Read, saved);
	check("collect skips the quantity", saved.count == 2);
	check("apply sets both stats", pd2::mapstack::Apply(&bare, saved, set) == 2 && diff(src, bare) == 70 - 70 - 1);
	pd2::mapstack::Apply(&kept, saved, set);
	pd2::mapstack::Apply(&kept, saved, set);
	check("apply twice, or over kept stats, is the same", diff(src, kept) == -1);
	// The 16-byte layout: the unknown fourth dword is ignored, and entries past the 12-byte stride are compared
	Fake a = Map(1, 87), b = Map(1, 87);
	a.stats[564].unknown = 7;
	check("the unknown dword is not compared", diff(a, b) == -1);
	static_assert(sizeof(pd2::mapstack::Entry) == 16);
	struct Many { pd2::mapstack::Entry e[64]; };
	Many ma {}, mb {};
	for (int i = 0; i < 64; ++i) { ma.e[i] = mb.e[i] = {i, 600, i, 0}; }
	mb.e[63].value = 1;
	const auto many = [](Many* u, uint32_t stat, pd2::mapstack::Entry* out, int32_t max) {
		if (stat != 600) {
			return 0;
		}
		std::memcpy(out, u->e, sizeof(pd2::mapstack::Entry) * static_cast<size_t>(max));
		return max;
	};
	check("a difference in the 64th entry is found", pd2::mapstack::FirstDifference(&ma, &mb, 601, 70, many) == 600);
	// Route's compare (#1759): equal stats stack, a rare's extra mod or another Area Level does not
	Fake plain = Map(1, 87), plain2 = Map(3, 87), rare = Map(1, 87), higher = Map(1, 97);
	rare.stats[100] = {0, 100, 25, 0};
	pd2::mapstack::Saved sp, sp2, sr, sh;
	pd2::mapstack::Collect(&plain, 600, 70, Read, sp);
	pd2::mapstack::Collect(&plain2, 600, 70, Read, sp2);
	pd2::mapstack::Collect(&rare, 600, 70, Read, sr);
	pd2::mapstack::Collect(&higher, 600, 70, Read, sh);
	check("same stats ignore the quantity", pd2::mapstack::SameStats(sp, sp2));
	check("a rare's extra mod differs", !pd2::mapstack::SameStats(sp, sr) && !pd2::mapstack::SameStats(sr, sp));
	check("another Area Level differs (saved)", !pd2::mapstack::SameStats(sp, sh));
	return fail;
}
