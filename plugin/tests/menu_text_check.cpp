#include "menu_text.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(name, cond) do { if (!(cond)) { ++g_fail; std::printf("FAIL %s\n", name); } } while (0)

int main() {
	constexpr const char* orb = "\xee\x81\xbe" "4Horadrim Orb";
	CHECK("gold Buy line contains only the orb name", std::strcmp(pd2::menu_text::GoldName(orb, 4), "Horadrim Orb") == 0);
	CHECK("the dialog keeps its independent gold color", std::strcmp(pd2::menu_text::GoldName(orb, 0), orb) == 0);
	CHECK("plain names stay unchanged", std::strcmp(pd2::menu_text::GoldName("Horadrim Orb", 4), "Horadrim Orb") == 0);
	CHECK("different embedded colors stay unchanged", std::strcmp(pd2::menu_text::GoldName("\xee\x81\xbe" "3Orb", 4), "\xee\x81\xbe" "3Orb") == 0);
	CHECK("empty input stays empty", *pd2::menu_text::GoldName("", 4) == '\0');
	CHECK("truncated marker stays unchanged", std::strcmp(pd2::menu_text::GoldName("\xee\x81", 4), "\xee\x81") == 0);
	CHECK("null stays null", pd2::menu_text::GoldName(nullptr, 4) == nullptr);
	std::printf("menu text: %d failures\n", g_fail);
	return g_fail != 0;
}
