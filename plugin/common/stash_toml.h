// The integer keys of a d2rloader.toml's `[d2rcore.stash]` section (add_shared_tabs #1808, set_materials_limit #1767), with no
// engine types so it builds on the host: read one line or a file's key, find a section header, and rewrite a file's text so
// add_shared_tabs is live with a given value.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace pd2::stash_toml {

// The value of a `<key> = N` line; -1 for any other line (a commented one included)
constexpr auto IntOf(const char* line, const char* key) noexcept -> int32_t {
	size_t i = 0;
	while (line[i] == ' ' || line[i] == '\t') {
		++i;
	}
	for (size_t k = 0; key[k] != '\0'; ++k) {
		if (line[i++] != key[k]) {
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
static_assert(IntOf("add_shared_tabs = 15", "add_shared_tabs") == 15);
static_assert(IntOf("  add_shared_tabs=0 # none", "add_shared_tabs") == 0);
static_assert(IntOf("# add_shared_tabs = 15", "add_shared_tabs") == -1);
static_assert(IntOf("set_materials_limit = 99", "add_shared_tabs") == -1);
static_assert(IntOf("set_materials_limit = 255", "set_materials_limit") == 255);

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

// The `key` of the `[d2rcore.stash]` section of `path`: -1 when the file has no live key, -2 when it cannot be read
inline auto ReadKey(const std::wstring& path, const char* key) noexcept -> int32_t {
	FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
		return -2;
	}
	int32_t value = -1;
	bool inStash = false, first = true;
	char line[256];
	while (std::fgets(line, sizeof line, file) != nullptr) {
		const char* text = line;
		if (std::exchange(first, false) && std::strncmp(line, "\xEF\xBB\xBF", 3) == 0) {
			text += 3;  // a UTF-8 BOM
		}
		if (const int32_t section = SectionOf(text); section >= 0) {
			inStash = section == 1;
		} else if (const int32_t n = IntOf(text, key); inStash && n >= 0) {
			value = n;
		}
	}
	std::fclose(file);
	return value;
}

enum class Source { Mod, Game, None, Unreadable };

// The mod's value, else the game-wide one. `configPath` is a file in <game>/mods/<mod>/d2rloader/config/ (the plugin's config).
inline auto Resolve(const wchar_t* configPath, const char* key, int32_t* value) noexcept -> Source {
	std::wstring dir = configPath != nullptr ? configPath : L"";
	const size_t slash = dir.find_last_of(L"\\/");
	dir = slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);
	std::wstring game = dir;  // up four folders: <game>/
	for (int i = 0; i < 4 && !game.empty(); ++i) {
		game.pop_back();
		const size_t cut = game.find_last_of(L"\\/");
		game = cut == std::wstring::npos ? L"" : game.substr(0, cut + 1);
	}
	const int32_t mod = ReadKey(dir + L"d2rloader.toml", key);
	const int32_t all = mod >= 0 ? -1 : ReadKey(game + L"d2rloader/config/d2rloader.toml", key);
	*value = mod >= 0 ? mod : all >= 0 ? all : -1;
	if (mod >= 0) {
		return Source::Mod;
	}
	if (all >= 0) {
		return Source::Game;
	}
	return mod == -2 && all == -2 ? Source::Unreadable : Source::None;
}

}  // namespace pd2::stash_toml
