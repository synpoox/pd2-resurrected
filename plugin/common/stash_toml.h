// The `add_shared_tabs` line of a d2rloader.toml's `[d2rcore.stash]` section (#1808), with no engine types so it builds on
// the host: read one line, find a section header, and rewrite a file's text so the key is live with a given value.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pd2::stash_toml {

// The value of an `add_shared_tabs = N` line; -1 for any other line (a commented one included)
constexpr auto ExtraTabsOf(const char* line) noexcept -> int32_t {
	constexpr char Key[] = "add_shared_tabs";
	size_t i = 0;
	while (line[i] == ' ' || line[i] == '\t') {
		++i;
	}
	for (const char c : Key) {
		if (c == '\0') {
			break;
		}
		if (line[i++] != c) {
			return -1;
		}
	}
	while (line[i] == ' ' || line[i] == '=') {
		++i;
	}
	int32_t n = 0;
	bool any = false;
	for (; line[i] >= '0' && line[i] <= '9'; ++i) {
		n = n * 10 + (line[i] - '0');
		any = true;
	}
	return any ? n : -1;
}
static_assert(ExtraTabsOf("add_shared_tabs = 15") == 15);
static_assert(ExtraTabsOf("  add_shared_tabs=0 # none") == 0);
static_assert(ExtraTabsOf("# add_shared_tabs = 15") == -1);
static_assert(ExtraTabsOf("set_materials_limit = 99") == -1);

// 1 for the `[d2rcore.stash]` header, 0 for any other `[section]` header, -1 for any other line
constexpr auto SectionOf(const char* line) noexcept -> int32_t {
	constexpr char Name[] = "[d2rcore.stash]";
	size_t i = 0;
	while (line[i] == ' ' || line[i] == '\t') {
		++i;
	}
	if (line[i] != '[') {
		return -1;
	}
	for (const char c : Name) {
		if (c == '\0') {
			return 1;
		}
		if (line[i++] != c) {
			return 0;
		}
	}
	return 1;
}
static_assert(SectionOf("[d2rcore.stash]") == 1);
static_assert(SectionOf("  [d2rcore.stash] # x") == 1);
static_assert(SectionOf("[d2rcore.other]") == 0);
static_assert(SectionOf("add_shared_tabs = 3") == -1);

// A line that sets or (after a `#`) comments the key, whatever its value (a string, a sign, hex: all of them are that key's line)
inline auto IsKeyLine(const char* line) noexcept -> bool {
	constexpr char Key[] = "add_shared_tabs";
	while (*line == ' ' || *line == '\t') {
		++line;
	}
	for (size_t i = 0; i + 1 < sizeof Key; ++i) {
		if (line[i] != Key[i]) {
			return false;
		}
	}
	const char next = line[sizeof Key - 1];
	return next == ' ' || next == '\t' || next == '=';
}

// `text` with `add_shared_tabs = value` live in `[d2rcore.stash]`: a key line there gets the value, else the first commented
// `# add_shared_tabs ...` line there is made live, else the line is added under the header, else the section is appended.
// Every other line, a UTF-8 BOM and the file's line endings stay as they are (the result always ends in a newline).
// Empty when the text should be fixed by hand: no plain `[d2rcore.stash]` header, yet a line that mentions `stash` (a spaced or
// dotted header, an inline table), where appending a header could duplicate the section.
inline auto WithExtraTabs(const std::string& whole, int32_t value) -> std::string {
	const bool bom = whole.compare(0, 3, "\xEF\xBB\xBF") == 0;
	const std::string text = bom ? whole.substr(3) : whole;
	std::vector<std::string> lines;  // each without its '\n' (a '\r' stays, so a CRLF file round-trips)
	for (size_t at = 0; at < text.size();) {
		const size_t nl = text.find('\n', at);
		lines.push_back(text.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
		at = nl == std::string::npos ? text.size() : nl + 1;
	}
	size_t header = SIZE_MAX, liveAt = SIZE_MAX, commentedAt = SIZE_MAX;
	bool inStash = false, mentionsStash = false;
	for (size_t i = 0; i < lines.size(); ++i) {
		const char* line = lines[i].c_str();
		const char* first = line;
		while (*first == ' ' || *first == '\t') {
			++first;
		}
		mentionsStash = mentionsStash || (*first != '#' && lines[i].find("stash") != std::string::npos);
		if (const int32_t section = SectionOf(line); section >= 0) {
			inStash = section == 1;
			header = inStash && header == SIZE_MAX ? i : header;
		} else if (inStash) {
			if (IsKeyLine(line)) {
				liveAt = i;
			} else if (commentedAt == SIZE_MAX && *first == '#') {
				for (++first; *first == ' ' || *first == '\t'; ++first) {
				}
				if (IsKeyLine(first)) {
					commentedAt = i;
				}
			}
		}
	}
	if (header == SIZE_MAX && mentionsStash) {
		return {};
	}
	const auto crOf = [&](size_t i) -> std::string { return !lines[i].empty() && lines[i].back() == '\r' ? "\r" : ""; };
	const std::string live = "add_shared_tabs = " + std::to_string(value);
	if (const size_t at = liveAt != SIZE_MAX ? liveAt : commentedAt; at != SIZE_MAX) {
		lines[at] = live + crOf(at);
	} else if (header != SIZE_MAX) {
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(header) + 1, live + crOf(header));
	} else {
		const std::string cr = text.find("\r\n") != std::string::npos ? "\r" : "";
		if (!lines.empty()) {
			lines.push_back(cr);
		}
		lines.push_back("[d2rcore.stash]" + cr);
		lines.push_back(live + cr);
	}
	std::string out = bom ? "\xEF\xBB\xBF" : "";
	for (const std::string& l : lines) {
		out += l + "\n";
	}
	return out;
}

}  // namespace pd2::stash_toml
