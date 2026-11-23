// Host self-check of plugin/common/corruption_lines.h: corruption markers (#2070).
#include "corruption_lines.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace pd2::corruption_lines;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

static auto Marked(const std::string& with, const std::string& lower, const std::string& higher, size_t size = 1024) -> std::string {
	char buf[1024] = {};
	std::snprintf(buf, sizeof(buf), "%s", with.c_str());
	Mark(buf, size, lower, higher);
	return buf;
}

int main() {
	const std::string red = std::string(Lead) + RedCode, blue = std::string(Lead) + BlueCode;
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
	CHECK("a line with no number the corruption added is red (Cannot Be Frozen)",
	      Marked("a\nCannot Be Frozen", "a", "a") == "a\n" + r("Cannot Be Frozen"));
	const std::string gold = std::string(Lead) + ';';
	CHECK("a red line ends on the colour before it, not always blue",
	      Marked("a" + gold + "\nb", "a" + gold, "a" + gold) == "a" + gold + "\n" + red + "b" + gold);
	{
		char buf[64] = "a\nSocketed (2)\n";
		const size_t from = 2;
		CHECK("MarkTail: the appended line is red, its newline stays",
		      MarkTail(buf, sizeof(buf), from) && std::string(buf) == "a\n" + r("Socketed (2)") + "\n");
		char eth[96] = "a\nEthereal (Cannot be Repaired), Socketed (2)\n";
		CHECK("MarkTail from mid-line: only the Socketed part of the ethereal line is red",
		      MarkTail(eth, sizeof(eth), 33) && std::string(eth) == "a\nEthereal (Cannot be Repaired), " + r("Socketed (2)") + "\n");
		CHECK("MarkTail: nothing appended, untouched", !MarkTail(buf, sizeof(buf), strlen(buf)));
		char small[12] = "a\nSocketed\n";  // 11 bytes and its terminator: no room for the colour codes
		CHECK("MarkTail: no room, untouched", !MarkTail(small, sizeof(small), 2) && std::string(small) == "a\nSocketed\n");
	}
	const Table t = ParseTable("\xEF\xBB\xBF" "code\tfunc1\tstat1\r\nres-all\t1\tfireresist\r\n\r\ndmg%\t7\t\r\n");
	CHECK("table: header without BOM, blank lines skipped", t.Column("code") == 0 && t.Column("stat1") == 2 && t.rows.size() == 2);
	CHECK("table: cells", Table::Cell(t.rows[0], 2) == "fireresist" && Table::Cell(t.rows[1], 2).empty() && Table::Cell(t.rows[1], 9).empty());
	CHECK("numbers", Number("413") == 413 && Number("-25") == -25 && Number("") == 0);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
