// Host self-check of plugin/common/invader_art_path.h and invader_dress.h (#1215, #1708).
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target invader_art_check
#include "invader_art_path.h"
#include "invader_dress.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
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
	namespace dr = pd2::invader_dress;
	const auto code = [](const char* c) {
		uint32_t v = 0x20202020;
		std::memcpy(&v, c, std::strlen(c));
		return v;
	};
	const auto expect = [&](const char* what, bool ok) {
		std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
		g_fail += ok ? 0 : 1;
	};
	{  // Paladin: Corona, Sacred Armor, Phase Blade, Zakarum Shield (ring and gloves show nothing)
		uint8_t p[dr::PartBytes] = {};
		const uint32_t c[] = {code("rin"), code("ulg"), code("urn"), code("uar"), code("7cr"), code("pae")};
		const auto r = dr::Dress(5, c, 6, p);
		expect("paladin helm crn", p[dr::HD] == 0);
		expect("paladin body 2,2,1,2 / 2,0", p[dr::TR] == 2 && p[dr::LG] == 2 && p[dr::RA] == 1 && p[dr::LA] == 2 && p[dr::S1] == 2 && p[dr::S2] == 0);
		expect("paladin crs in RH", p[dr::RH] == 1);
		const uint32_t x[] = {code("7ax")};
		dr::Dress(5, x, 1, p);
		expect("paladin axe in RH", p[dr::RH] == 10);
		expect("paladin pa3 in SH", p[dr::SH] == 0);
		expect("paladin all shown", r.shown == 4 && r.unshown == 0);
	}
	{  // Barbarian: one Thunder Maul in the right hand, the left hand empty
		uint8_t p[dr::PartBytes] = {};
		p[dr::LH] = 3;
		const uint32_t c[] = {code("bae"), code("7gm")};
		const auto r = dr::Dress(2, c, 2, p);
		expect("barbarian ba5, mau as whm, empty left", p[dr::HD] == 0 && p[dr::RH] == 4 && p[dr::LH] == 28 && r.shown == 2);
		const uint32_t t[] = {code("7ta"), code("7tk")};
		dr::Dress(2, t, 2, p);
		expect("barbarian throwing axe, knife as ssd", p[dr::RH] == 5 && p[dr::LH] == 8);
		const uint32_t l[] = {code("7la")};
		dr::Dress(2, l, 1, p);
		expect("barbarian large axe as axe", p[dr::RH] == 0);
	}
	{  // Amazon: a bow in LH, a spear in RH, a javelin and shield; the other hand and the shield empty
		uint8_t p[dr::PartBytes] = {};
		p[dr::RH] = p[dr::LH] = p[dr::SH] = 3;
		const uint32_t b[] = {code("amb")};
		dr::Dress(0, b, 1, p);
		expect("amazon am1 in LH, RH and SH empty", p[dr::LH] == 0 && p[dr::RH] == 0 && p[dr::SH] == 0);
		const uint32_t s[] = {code("7sr")};
		dr::Dress(0, s, 1, p);
		expect("amazon spear in RH, LH empty", p[dr::RH] == 1 && p[dr::LH] == 8);
		const uint32_t j[] = {code("ama"), code("uit")};
		const auto r = dr::Dress(0, j, 2, p);
		expect("amazon javelin RH, monarch SH", p[dr::RH] == 6 && p[dr::SH] == 4 && p[dr::LH] == 8 && r.shown == 2);
		const uint32_t x[] = {code("6hx")};
		dr::Dress(0, x, 1, p);
		expect("amazon crossbow as lbw in LH", p[dr::LH] == 3 && p[dr::RH] == 0);
	}
	{  // Necromancer: the head in S3; Sorceress: orb and shield, then a staff with no shield
		uint8_t p[dr::PartBytes] = {};
		const uint32_t c[] = {code("ned")};
		dr::Dress(4, c, 1, p);
		expect("necromancer ne3 in S3", p[dr::S3] == 2);
		const uint32_t o[] = {code("obd"), code("uit")};
		dr::Dress(6, o, 2, p);
		expect("sorceress ob3 as sst RH, kit SH", p[dr::RH] == 2 && p[dr::SH] == 4);
		const uint32_t s[] = {code("6ws")};
		dr::Dress(6, s, 1, p);
		expect("sorceress lst RH, SH empty", p[dr::RH] == 1 && p[dr::SH] == 0);
	}
	expect("no part bytes", dr::Dress(1, nullptr, 0, nullptr).shown == 0);
	std::printf(g_fail == 0 ? "all passed\n" : "FAILED\n");
	return g_fail == 0 ? 0 : 1;
}
