// Corruption markers (#2070): the property lines a corruption added or changed turn red. PD2 appends a red `*` to them;
// here the whole line is red. No engine or Windows types (tests/corruption_lines_check.cpp).
#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace pd2::corruption_lines {

// The tooltip text's colour code: U+E07E (UTF-8) plus the code character. A code holds for the rest of its line and
// the lines after it, so a red line ends on the colour that was active before it (the property blue, `3`, by default).
inline constexpr std::string_view Lead = "\xEE\x81\xBE";
inline constexpr char RedCode = '1', BlueCode = '3';

// The colour code active after `text`, starting from `current`
inline auto LastCode(std::string_view text, char current) -> char {
	for (size_t at = text.find(Lead); at != std::string_view::npos; at = text.find(Lead, at + Lead.size())) {
		if (at + Lead.size() < text.size()) {
			current = text[at + Lead.size()];
		}
	}
	return current;
}

// Appends `line` in red, then the code that was active before it
inline void AppendRed(std::string& out, std::string_view line, char before) {
	out.append(Lead).append(1, RedCode).append(line).append(Lead).append(1, before);
}

inline auto Split(std::string_view text) -> std::vector<std::string_view> {
	std::vector<std::string_view> lines;
	for (size_t at = 0;;) {
		const size_t eol = text.find('\n', at);
		lines.push_back(text.substr(at, eol == std::string_view::npos ? eol : eol - at));
		if (eol == std::string_view::npos) {
			return lines;
		}
		at = eol + 1;
	}
}

// Takes `line` out of `spare` (each equal line counts once); false when it is not there.
inline auto Take(std::vector<std::string_view>& spare, std::string_view line) -> bool {
	for (std::string_view& s : spare) {
		if (s.data() != nullptr && s == line) {
			s = {};  // used
			return true;
		}
	}
	return false;
}

// `with`: the item's lines; `lower` and `higher`: the same item's lines with the corruption's values lowered and raised.
// A line is the corruption's when neither has it: a line it did not touch is in at least one of them, also when a
// changed value splits or joins a group line (All Resistances, all Attributes), which only one direction can do. Each
// such line is wrapped Red ... Blue, every other byte stays as it is. False, and `with` untouched, when no line is the
// corruption's or the result would not fit in `size` bytes with its terminator.
inline auto Mark(char* with, size_t size, std::string_view lower, std::string_view higher) -> bool {
	const std::string_view text(with, strnlen(with, size));
	std::vector<std::string_view> spareLow = Split(lower), spareHigh = Split(higher);
	std::string out;
	bool marked = false, first = true;
	char code = BlueCode;
	for (const std::string_view line : Split(text)) {
		if (!first) {
			out += '\n';
		}
		first = false;
		const bool low = Take(spareLow, line), high = Take(spareHigh, line);
		if (low || high || line.empty()) {
			out += line;
		} else {
			AppendRed(out, line, code);
			marked = true;
		}
		code = LastCode(line, code);
	}
	if (!marked || out.size() + 1 > size) {
		return false;
	}
	std::memcpy(with, out.c_str(), out.size() + 1);
	return true;
}

// `text` grew from `from` bytes by one line (and its newline): that line turns red, as Mark does. False, and `text`
// untouched, when nothing was added or it would not fit in `size` bytes with its terminator.
inline auto MarkTail(char* text, size_t size, size_t from) -> bool {
	const std::string_view all(text, strnlen(text, size));
	if (from >= all.size()) {
		return false;
	}
	std::string_view line = all.substr(from);
	const bool newline = line.back() == '\n';
	if (newline) {
		line.remove_suffix(1);
	}
	if (line.empty()) {
		return false;
	}
	std::string out(all.substr(0, from));
	AppendRed(out, line, LastCode(out, BlueCode));
	if (newline) {
		out += '\n';
	}
	if (out.size() + 1 > size) {
		return false;
	}
	std::memcpy(text, out.c_str(), out.size() + 1);
	return true;
}

// A tab-separated table (an excel .txt): its header cells and its rows, blank lines skipped.
struct Table {
	std::vector<std::string_view> header;
	std::vector<std::vector<std::string_view>> rows;
	auto Column(std::string_view name) const -> int {
		for (size_t i = 0; i < header.size(); ++i) {
			if (header[i] == name) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	static auto Cell(const std::vector<std::string_view>& row, int column) -> std::string_view {
		return column >= 0 && static_cast<size_t>(column) < row.size() ? row[column] : std::string_view {};
	}
};

// `text` must outlive the table (the cells point into it)
inline auto ParseTable(std::string_view text) -> Table {
	Table t;
	if (text.substr(0, 3) == "\xEF\xBB\xBF") {
		text.remove_prefix(3);
	}
	for (std::string_view line : Split(text)) {
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1);
		}
		if (line.empty()) {
			continue;
		}
		std::vector<std::string_view> cells;
		for (size_t at = 0;;) {
			const size_t tab = line.find('\t', at);
			cells.push_back(line.substr(at, tab == std::string_view::npos ? tab : tab - at));
			if (tab == std::string_view::npos) {
				break;
			}
			at = tab + 1;
		}
		(t.header.empty() ? t.header : t.rows.emplace_back()) = std::move(cells);
	}
	return t;
}

inline auto Number(std::string_view s) -> int {
	int v = 0;
	bool neg = !s.empty() && s.front() == '-';
	for (size_t i = neg ? 1 : 0; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
		v = v * 10 + (s[i] - '0');
	}
	return neg ? -v : v;
}

}  // namespace pd2::corruption_lines
