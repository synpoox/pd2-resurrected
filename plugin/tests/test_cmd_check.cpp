// Host self-check of plugin/common/test_cmd.h: the monster test commands' arguments.
#include "test_cmd.h"

#include <cstdio>

using pd2::test_cmd::Cmd;
using pd2::test_cmd::Parse;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

int main() {
	Cmd c = Parse("kill 700");
	CHECK("kill by class", c.kind == Cmd::Kind::Kill && c.classId == 700 && c.unitId == 0);
	c = Parse("  kill unit 42 ");
	CHECK("kill by unit id", c.kind == Cmd::Kind::Kill && c.unitId == 42 && c.classId == 0);
	c = Parse("life 10 unit 9");
	CHECK("life by unit id", c.kind == Cmd::Kind::LifeUnit && c.percent == 10 && c.unitId == 9);
	CHECK("life by class is the old command", Parse("life 10 700").kind == Cmd::Kind::None);
	CHECK("kill needs an id", Parse("kill").kind == Cmd::Kind::None && Parse("kill unit").kind == Cmd::Kind::None);
	CHECK("kill refuses zero, negatives and junk", Parse("kill 0").kind == Cmd::Kind::None &&
	      Parse("kill -3").kind == Cmd::Kind::None && Parse("kill 7 x").kind == Cmd::Kind::None);
	CHECK("life percent is 0-100", Parse("life 101 unit 3").kind == Cmd::Kind::None && Parse("life 0 unit 3").kind == Cmd::Kind::LifeUnit);
	CHECK("null and status text are none", Parse(nullptr).kind == Cmd::Kind::None && Parse("").kind == Cmd::Kind::None);
	CHECK("corrupt and desecrate take no arguments", Parse("corrupt").kind == Cmd::Kind::Corrupt &&
	      Parse(" desecrate").kind == Cmd::Kind::Desecrate && Parse("corrupt 5").kind == Cmd::Kind::None);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
