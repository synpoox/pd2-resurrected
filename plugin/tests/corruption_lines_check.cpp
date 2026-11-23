// Host self-check of plugin/common/corruption_lines.h: corruption markers (#2070).
#include "corruption_lines.h"

#include <cstdio>
#include <string>

using namespace pd2::corruption_lines;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

static auto Marked(const char* with, const char* lower, const char* higher, size_t size = 1024) -> std::string {
	char buf[1024] = {};
	std::snprintf(buf, sizeof(buf), "%s", with);
	Mark(buf, size, lower, higher);
	return buf;
}

int main() {
	const std::string red(Red), blue(Blue);
	const auto r = [&](const char* line) { return red + line + blue; };
	CHECK("only the changed line is red, the others stay as they are",
	      Marked("+20% Faster Cast Rate\n+240% Enhanced Damage\n+10 to Strength",
	             "+20% Faster Cast Rate\n+200% Enhanced Damage\n+10 to Strength",
	             "+20% Faster Cast Rate\n+280% Enhanced Damage\n+10 to Strength") ==
	          "+20% Faster Cast Rate\n" + r("+240% Enhanced Damage") + "\n+10 to Strength");
	CHECK("an added line is red", Marked("a\nb", "a", "a\nc") == "a\n" + r("b"));
	CHECK("a line the corruption did not touch is never red, also when it repeats",
	      Marked("x\nx\ny", "x\nx\ny", "x\nx\ny") == "x\nx\ny");
	CHECK("an equal line counts once", Marked("x\nx", "x", "x\nz") == "x\n" + r("x"));
	CHECK("a group line split by the corruption: only its own line is red",
	      Marked("Fire Resist +30%\nCold Resist +20%\nLightning Resist +20%\nPoison Resist +20%", "All Resistances +20",
	             "Fire Resist +40%\nCold Resist +20%\nLightning Resist +20%\nPoison Resist +20%") ==
	          r("Fire Resist +30%") + "\nCold Resist +20%\nLightning Resist +20%\nPoison Resist +20%");
	CHECK("a group line the corruption completed is red",
	      Marked("All Resistances +30", "Fire Resist +20%\nCold Resist +30%\nLightning Resist +30%\nPoison Resist +30%",
	             "Fire Resist +40%\nCold Resist +30%\nLightning Resist +30%\nPoison Resist +30%") == r("All Resistances +30"));
	CHECK("every line the corruption changed is red (res-all on an item with its own fire resist)",
	      Marked("Fire Resist +30%\nCold Resist +10%", "Fire Resist +20%", "Fire Resist +40%\nCold Resist +20%") ==
	          r("Fire Resist +30%") + "\n" + r("Cold Resist +10%"));
	CHECK("empty lines and the trailing newline stay", Marked("a\n\nb\n", "a\n\n\n", "a\n\nc\n") == "a\n\n" + r("b") + "\n");
	CHECK("no room: left untouched", Marked("a\nb", "a", "a", 8) == "a\nb");
	CHECK("nothing differs: left untouched", Marked("a\nb", "b\na", "a\nb") == "a\nb");
	const Table t = ParseTable("\xEF\xBB\xBF" "code\tfunc1\tstat1\r\nres-all\t1\tfireresist\r\n\r\ndmg%\t7\t\r\n");
	CHECK("table: header without BOM, blank lines skipped", t.Column("code") == 0 && t.Column("stat1") == 2 && t.rows.size() == 2);
	CHECK("table: cells", Table::Cell(t.rows[0], 2) == "fireresist" && Table::Cell(t.rows[1], 2).empty() && Table::Cell(t.rows[1], 9).empty());
	CHECK("numbers", Number("413") == 413 && Number("-25") == -25 && Number("") == 0);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
