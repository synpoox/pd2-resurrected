// Host self-check of plugin/common/pad_modifier.h: Controller QoL's modifier setting (#1442).
#include "pad_modifier.h"

#include <cstdio>

using namespace pd2::pad_modifier;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

int main() {
	CHECK("the shipped default is LB", FromConfig("[qol]\nrequire_modifier = true\nmodifier = \"bumper\"\nground_pickup_button = \"bumper\"\n") == LB);
	CHECK("rb", FromConfig("modifier = \"rb\"") == RB);
	CHECK("a trigger and the comment after the value", FromConfig("modifier = \"LT\" # integer\r\n") == LT);
	CHECK("any is LB or LT", FromConfig("modifier = \"any\"") == (LB | LT));
	CHECK("trigger and l2 are LT only", FromConfig("modifier = \"trigger\"") == LT && FromConfig("modifier = \"l2\"") == LT);
	CHECK("l1 is LB, r1 is RB, r2 is RT", FromConfig("modifier = \"l1\"") == LB && FromConfig("modifier = \"r1\"") == RB &&
	      FromConfig("modifier = \"r2\"") == RT);
	CHECK("an empty value is LT", FromConfig("modifier = \"\"") == LT);
	CHECK("l3 and r3", FromConfig("modifier = \"l3\"") == L3 && FromConfig("modifier = \"r3\"") == R3);
	CHECK("other keys do not count", FromConfig("ground_pickup_button = \"rt\"\nrequire_modifier = \"rt\"\n") == LB);
	CHECK("a comment does not count", FromConfig("# modifier = \"rt\"\nmodifier = \"r3\"") == R3);
	CHECK("a missing file or key falls back to LB", FromConfig("") == LB && FromConfig("[qol]\nenabled = true") == LB);
	CHECK("an unknown value falls back to LB", FromConfig("modifier = \"banana\"") == LB);
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
