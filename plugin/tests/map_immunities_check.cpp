// Host self-check of common/map_immunities.h (#1552).
//   cmake -S plugin -B plugin/build -A x64 && cmake --build plugin/build --config Release --target map_immunities_check
#include "map_immunities.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
int g_fail = 0;
void check(const char* what, bool ok) {
	std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	g_fail += ok ? 0 : 1;
}
std::string Plain(const pd2::mapimm::Resists& r) {  // the line with colour markers cut out
	char buf[256];
	pd2::mapimm::Line(r, buf, sizeof(buf));
	std::string s = buf;
	const std::string marker = std::string(pd2::mapimm::ColorMarker);
	for (size_t at; (at = s.find(marker)) != std::string::npos;) {
		s.erase(at, marker.size() + 1);
	}
	return s;
}
}  // namespace

int main() {
	using pd2::mapimm::Resists;
	char buf[256];
	check("no immunity: no line", pd2::mapimm::Line({99, 50, -20, 0}, buf, sizeof(buf)) == 0 && buf[0] == '\0');
	check("issue example", Plain({125, 130, 0, 0}) == "Immunities: C130, F125");
	check("100 counts", Plain({0, 0, 100, 0}) == "Immunities: L100");
	check("all four, C F L P order", Plain({101, 102, 103, 104}) == "Immunities: C102, F101, L103, P104");
	pd2::mapimm::Line({0, 0, 0, 150}, buf, sizeof(buf));
	check("poison colour 2", std::strstr(buf, "c2P150") != nullptr);
	check("too small a buffer gives nothing", pd2::mapimm::Line({125, 130, 0, 0}, buf, 10) == 0 && buf[0] == '\0');
	return g_fail == 0 ? 0 : 1;
}
