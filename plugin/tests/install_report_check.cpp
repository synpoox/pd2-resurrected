// Host self-check of plugin/core-rules/install_report.h (#2286): every skills install keeps its own result.
#include "install_report.h"

#include <cstdio>
#include <cstring>
#include <string>

using pd2r::installs::Report;
using pd2r::installs::Status;

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

// A rule as the registry runs it: appends "; text" to the report it is given and returns how many parts worked.
static auto Append(char* report, size_t size, const char* text) -> void {
	const size_t used = std::strlen(report);
	std::snprintf(report + used, size - used, "; %s", text);
}

int main() {
	char line[pd2r::installs::DetailSize + 160];
	Report r;
	r.Run("slot", "pSrvHitFunc 65 return spawn", 1, [](char*, size_t) { return 1; });
	r.Run("rule", "InstallOk", 1, [](char* t, size_t n) { Append(t, n, "ok rule on"); return 1; });
	r.Run("rule", "InstallPartial", 3, [](char* t, size_t n) { Append(t, n, "a on, b FAILED (bytes differ), c on"); return 2; });
	r.Run("rule", "InstallFailed", 1, [](char* t, size_t n) { Append(t, n, "hook FAILED (signature not found)"); return 0; });
	r.Run("slot", "srvdofunc 9 x", 1, [](char*, size_t) { return 0; });

	CHECK("success is ok", r.Results()[1].status() == Status::Ok && r.Results()[1].detail == "ok rule on");
	CHECK("partial is partial", r.Results()[2].status() == Status::Partial);
	CHECK("failure is failed", r.Results()[3].status() == Status::Failed);
	CHECK("totals add every install", r.Installed() == 4 && r.Expected() == 7);
	CHECK("counts by status", r.Count(Status::Ok) == 2 && r.Count(Status::Partial) == 1 && r.Count(Status::Failed) == 2);
	pd2r::installs::Line(r.Results()[2], line, sizeof(line));
	CHECK("partial line names the rule, counts and reason",
	      std::strcmp(line, "PARTIAL rule InstallPartial 2/3: a on, b FAILED (bytes differ), c on") == 0);
	pd2r::installs::Line(r.Results()[3], line, sizeof(line));
	CHECK("failed line names the rule and reason", std::strcmp(line, "FAILED rule InstallFailed 0/1: hook FAILED (signature not found)") == 0);
	pd2r::installs::Line(r.Results()[4], line, sizeof(line));
	CHECK("failure with no text still says so", std::strstr(line, "FAILED slot srvdofunc 9 x 0/1: install returned failure") == line);
	r.Summary(line, sizeof(line));
	CHECK("summary", std::strcmp(line, "4/7 installed (2 slots, 3 rules); 1 partial, 2 failed: `core-rules installs failed` lists them") == 0);

	// An install that writes more than its buffer: kept cut and flagged, never dropped.
	Report big;
	big.Run("rule", "InstallHuge", 2, [](char* t, size_t n) {
		const std::string text(5000, 'x');
		Append(t, n, text.c_str());
		return 1;
	});
	pd2r::installs::Line(big.Results()[0], line, sizeof(line));
	CHECK("oversized diagnostic is flagged", big.Results()[0].cut && std::strstr(line, "[diagnostic cut]") != nullptr);
	CHECK("oversized line keeps identity", std::strncmp(line, "PARTIAL rule InstallHuge 1/2: xxx", 33) == 0);

	// The old shared report stopped at 2800 chars: 200 installs of ~40 chars each, the last one failing, past that.
	Report many;
	size_t written = 0;
	for (int i = 0; i < 200; ++i) {
		char name[32];
		std::snprintf(name, sizeof(name), "InstallRule%03d", i);
		const bool last = i == 199;
		many.Run("rule", name, 1, [&](char* t, size_t n) {
			Append(t, n, last ? "final hook FAILED (bytes differ)" : "this rule installed fine, nothing to see");
			written += std::strlen(t);
			return last ? 0 : 1;
		});
	}
	CHECK("the run wrote past the former limit", written > 2800 * 2);
	const auto& tail = many.Results().back();
	pd2r::installs::Line(tail, line, sizeof(line));
	CHECK("final failure kept with identity and reason",
	      std::strcmp(line, "FAILED rule InstallRule199 0/1: final hook FAILED (bytes differ)") == 0);
	CHECK("final totals", many.Installed() == 199 && many.Expected() == 200 && many.Count(Status::Failed) == 1);
	many.Summary(line, sizeof(line));
	CHECK("summary stays one short line", std::strlen(line) < 120);

	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
