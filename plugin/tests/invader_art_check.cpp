// Host self-check of plugin/common/invader_art_path.h and invader_look.h (#1215).
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target invader_art_check
#include "invader_art_path.h"
#include "invader_look.h"

#include <cstdint>
#include <cstdio>
#include <string>

static int g_fail = 0;
static std::string Win(std::string s) {
	for (char& c : s) {
		c = c == '/' ? '\\' : c;
	}
	return s;
}
static void Check(const char* in0, const char* want0) {
	const std::string in = Win(in0), want = Win(want0);
	std::string s = in;
	pd2::invader_art::Redirect(s.data());
	const bool ok = std::string(s.c_str()) == want;
	std::printf("%s  %s -> %s\n", ok ? "PASS" : "FAIL", in.c_str(), s.c_str());
	g_fail += ok ? 0 : 1;
}

int main() {
	Check("data/global/monsters/AM/COF/AMA1BOW.COF", "data/global/CHARS/AM/COF/AMA1BOW.COF");
	Check("data/global/monsters/so/lg/SOLGLITNU.dcc", "data/global/CHARS/so/lg/SOLGLITNU.dcc");
	Check("data/global/monsters/ZZ/COF/ZZA1HTH.COF", "data/global/monsters/ZZ/COF/ZZA1HTH.COF");  // not an Invader token
	Check("data/global/monsters/AMB/COF/x.COF", "data/global/monsters/AMB/COF/x.COF");  // token longer than 2
	Check("data/global/CHARS/AM/COF/AMA1BOW.COF", "data/global/CHARS/AM/COF/AMA1BOW.COF");  // already a player path
	Check("data/global/monsters/A", "data/global/monsters/A");
	namespace il = pd2::invader_look;
	uint8_t parts[il::PartBytes] = {};
	const auto W = [&](int rh, int lh) {
		parts[il::PartRH] = static_cast<uint8_t>(rh);
		parts[il::PartLH] = static_cast<uint8_t>(lh);
		return il::Barbarian(parts);
	};
	const struct { int rh, lh; il::Weapon want; } looks[] = {{0, 0, il::Weapon::OneHand}, {2, 0, il::Weapon::TwoHand}, {4, 2, il::Weapon::TwoHand},
	                                                         {1, 3, il::Weapon::OneHand}, {5, 0, il::Weapon::Unknown}, {0, 200, il::Weapon::Unknown}};
	for (const auto& l : looks) {
		const bool ok = W(l.rh, l.lh) == l.want;
		std::printf("%s  barbarian hands %d,%d\n", ok ? "PASS" : "FAIL", l.rh, l.lh);
		g_fail += ok ? 0 : 1;
	}
	const bool nul = il::Barbarian(nullptr) == il::Weapon::Unknown;
	std::printf("%s  no part bytes\n", nul ? "PASS" : "FAIL");
	g_fail += nul ? 0 : 1;
	std::printf(g_fail == 0 ? "all passed\n" : "FAILED\n");
	return g_fail == 0 ? 0 : 1;
}
