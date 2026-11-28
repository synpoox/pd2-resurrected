// The skills part's install results (#2286): one entry per PD2R_SLOT, PD2R_RULE and PD2R_RULE_N, kept for the session.
// Each install writes into its own buffer, so no install can push another one's diagnostic out; a diagnostic that fills
// its buffer is kept cut and says so. No engine types: plugin/tests/install_report_check.cpp runs it on the host.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pd2r::installs {

inline constexpr size_t DetailSize = 1024;  // per install; a longer diagnostic is kept cut and flagged

enum class Status { Ok, Partial, Failed };

struct Result {
	const char* kind;  // "slot" or "rule"
	std::string name;  // "pSrvHitFunc 65 return spawn", "InstallRouter"
	int installed;
	int expected;
	std::string detail;  // what the install wrote, or why it failed
	bool cut;            // the install wrote more than DetailSize - 1 chars

	auto status() const noexcept -> Status {
		return installed >= expected ? Status::Ok : installed <= 0 ? Status::Failed : Status::Partial;
	}
};

// "FAILED rule InstallX 0/1: <detail>". `out` gets the whole line; callers size it at DetailSize + 160.
inline void Line(const Result& r, char* out, size_t size) noexcept {
	const Status s = r.status();
	const char* word = s == Status::Ok ? "ok" : s == Status::Partial ? "PARTIAL" : "FAILED";
	const char* detail = r.detail.empty() ? (s == Status::Ok ? "" : "install returned failure, no detail") : r.detail.c_str();
	std::snprintf(out, size, "%s %s %s %d/%d%s%s%s", word, r.kind, r.name.c_str(), r.installed, r.expected,
	              *detail ? ": " : "", detail, r.cut ? " [diagnostic cut]" : "");
}

class Report {
public:
	// Runs `install(buffer, size) -> installed count` into a fresh buffer and records it.
	template <class Install>
	auto Run(const char* kind, const char* name, int expected, Install&& install) -> const Result& {
		char buffer[DetailSize] = "";
		const int installed = install(buffer, sizeof(buffer));
		const bool cut = std::strlen(buffer) >= DetailSize - 1;
		const char* text = buffer;
		while (*text == ';' || *text == ' ' || *text == ':') {  // rules append "; text" to the old shared report
			++text;
		}
		results_.push_back({kind, name, installed, expected, text, cut});
		return results_.back();
	}

	auto Results() const noexcept -> const std::vector<Result>& { return results_; }
	auto Installed() const noexcept -> int { return Sum(&Result::installed); }
	auto Expected() const noexcept -> int { return Sum(&Result::expected); }
	auto Count(Status s) const noexcept -> int {
		int n = 0;
		for (const Result& r : results_) {
			n += r.status() == s ? 1 : 0;
		}
		return n;
	}

	// The part's one startup line: "131/133 installed (24 slots, 109 rules); 1 partial, 1 failed: `core-rules installs failed`".
	void Summary(char* out, size_t size) const noexcept {
		int slots = 0;
		for (const Result& r : results_) {
			slots += std::strcmp(r.kind, "slot") == 0 ? 1 : 0;
		}
		const int partial = Count(Status::Partial);
		const int failed = Count(Status::Failed);
		std::snprintf(out, size, "%d/%d installed (%d slots, %d rules); %d partial, %d failed%s", Installed(), Expected(),
		              slots, static_cast<int>(results_.size()) - slots, partial, failed,
		              partial + failed ? ": `core-rules installs failed` lists them" : "");
	}

private:
	auto Sum(int Result::*field) const noexcept -> int {
		int n = 0;
		for (const Result& r : results_) {
			n += r.*field;
		}
		return n;
	}
	std::vector<Result> results_;
};

}  // namespace pd2r::installs
