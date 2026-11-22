// Host self-check of plugin/common/stash_toml.h (#1808): the add_shared_tabs rewrite.
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target stash_toml_check
#include "stash_home.h"
#include "stash_toml.h"

#include <cstdio>
#include <string>

using pd2::stash_toml::WithExtraTabs;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

int main() {
	// #1854: the extra tabs the self-heal writes reach every reserved page in every game mode (3 stock pages Classic/LoD, 5 RotW)
	CHECK("Classic/LoD reach Bossing", pd2::affinity::MinStockSharedPages + pd2::affinity::RequiredExtraTabs > pd2::affinity::BossingPage);
	CHECK("RotW reaches Bossing", pd2::affinity::VanillaSharedPages + pd2::affinity::RequiredExtraTabs > pd2::affinity::BossingPage);
	CHECK("commented key goes live", WithExtraTabs("[a]\nx = 1\n[d2rcore.stash]\n# add_shared_tabs = 0\nset_materials_limit = 9\n[b]\n",
	                                                15) == "[a]\nx = 1\n[d2rcore.stash]\nadd_shared_tabs = 15\nset_materials_limit = 9\n[b]\n");
	CHECK("live key gets the value", WithExtraTabs("[d2rcore.stash]\nadd_shared_tabs = 0\n", 15) == "[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("missing key is added under the header", WithExtraTabs("[d2rcore.stash]\nset_materials_limit = 9\n", 15) ==
	                                                   "[d2rcore.stash]\nadd_shared_tabs = 15\nset_materials_limit = 9\n");
	CHECK("missing section is appended", WithExtraTabs("[a]\nx = 1\n", 15) == "[a]\nx = 1\n\n[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("empty file", WithExtraTabs("", 15) == "[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("a key in another section is left alone", WithExtraTabs("[a]\n# add_shared_tabs = 0\n[d2rcore.stash]\n", 15) ==
	                                                    "[a]\n# add_shared_tabs = 0\n[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("CRLF is kept", WithExtraTabs("[d2rcore.stash]\r\n# add_shared_tabs = 0\r\nx = 1\r\n", 15) == "[d2rcore.stash]\r\nadd_shared_tabs = 15\r\nx = 1\r\n");
	CHECK("CRLF section appended", WithExtraTabs("[a]\r\nx = 1\r\n", 15) == "[a]\r\nx = 1\r\n\r\n[d2rcore.stash]\r\nadd_shared_tabs = 15\r\n");
	CHECK("one header only", [] {
		const std::string out = WithExtraTabs("[d2rcore.stash]\n", 15);
		return out.find("[d2rcore.stash]") == out.rfind("[d2rcore.stash]");
	}());
	CHECK("a string value is replaced, no duplicate key", WithExtraTabs("[d2rcore.stash]\nadd_shared_tabs = \"3\"\n", 15) == "[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("a hex or signed value is replaced", WithExtraTabs("[d2rcore.stash]\nadd_shared_tabs=0x0F\n", 15) == "[d2rcore.stash]\nadd_shared_tabs = 15\n" &&
	                                           WithExtraTabs("[d2rcore.stash]\nadd_shared_tabs = +15\n", 15) == "[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("a BOM stays and the section is found", WithExtraTabs("\xEF\xBB\xBF[d2rcore.stash]\n# add_shared_tabs = 0\n", 15) ==
	                                                  "\xEF\xBB\xBF[d2rcore.stash]\nadd_shared_tabs = 15\n");
	CHECK("a spaced header is left to the player", WithExtraTabs("[ d2rcore.stash ]\nx = 1\n", 15).empty());
	CHECK("an inline table is left to the player", WithExtraTabs("[d2rcore]\nstash = { add_shared_tabs = 0 }\n", 15).empty());
	CHECK("a comment mentioning stash is no block", !WithExtraTabs("# the stash\n[a]\n", 15).empty());
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
