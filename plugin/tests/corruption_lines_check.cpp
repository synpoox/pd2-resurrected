// Host self-check of plugin/common/corruption_lines.h: corruption markers (#2070).
#include "corruption_lines.h"

#include <cstdio>
#include <string>

using namespace pd2::corruption_lines;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

static auto Marked(const char* with, const char* without, size_t size = 1024) -> std::string {
	char buf[1024] = {};
	std::snprintf(buf, sizeof(buf), "%s", with);
	Mark(buf, size, without);
	return buf;
}

int main() {
	const std::string red(Red), blue(Blue);
	CHECK("only the changed line is red, the others stay as they are",
	      Marked("+20% Faster Cast Rate\n+240% Enhanced Damage\n+10 to Strength",
	             "+20% Faster Cast Rate\n+200% Enhanced Damage\n+10 to Strength") ==
	          "+20% Faster Cast Rate\n" + red + "+240% Enhanced Damage" + blue + "\n+10 to Strength");
	CHECK("an added line is red", Marked("a\nb", "a") == "a\n" + red + "b" + blue);
	CHECK("a line the corruption did not touch is never red, also when it repeats",
	      Marked("x\nx\ny", "x\nx\ny") == "x\nx\ny");
	CHECK("an equal line counts once", Marked("x\nx", "x") == "x\n" + red + "x" + blue);
	CHECK("several red lines (All Resistances split by a corruption)",
	      Marked("Fire Resist +30%\nCold Resist +10%\nLightning Resist +10%\nPoison Resist +10%", "Fire Resist +20%") ==
	          red + "Fire Resist +30%" + blue + "\n" + red + "Cold Resist +10%" + blue + "\n" + red + "Lightning Resist +10%" +
	              blue + "\n" + red + "Poison Resist +10%" + blue);
	CHECK("empty lines and the trailing newline stay", Marked("a\n\nb\n", "a\n\n\n") == "a\n\n" + red + "b" + blue + "\n");
	CHECK("no room: left untouched", Marked("a\nb", "a", 8) == "a\nb");
	CHECK("nothing differs: left untouched", Marked("a\nb", "b\na") == "a\nb");
	const Table t = ParseTable("\xEF\xBB\xBF" "code\tfunc1\tstat1\r\nres-all\t1\tfireresist\r\n\r\ndmg%\t7\t\r\n");
	CHECK("table: header without BOM, blank lines skipped", t.Column("code") == 0 && t.Column("stat1") == 2 && t.rows.size() == 2);
	CHECK("table: cells", Table::Cell(t.rows[0], 2) == "fireresist" && Table::Cell(t.rows[1], 2).empty() && Table::Cell(t.rows[1], 9).empty());
	CHECK("numbers", Number("413") == 413 && Number("-25") == -25 && Number("") == 0);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
