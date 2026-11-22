// Property numbers by code (#1371). D2R numbers properties.txt by ROW, skipping the "Expansion" row (the *Id column is only
// a comment), so a plugin that hard-codes a number breaks when the table is edited (#1355). Init reads the mod's
// properties.txt once at load (the way Cartographer's parsePropertyId does); Resolve returns -1 and logs when a code is missing.
#pragma once

#include <D2RLPlugin/context.h>

#include "log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace propids {

// The row number of each code in the tab-separated text; blank lines are skipped; a row with an empty code is counted (it is a row to the game); the Expansion row is not.
inline auto Parse(std::string_view text) -> std::map<std::string, int, std::less<>> {
	std::map<std::string, int, std::less<>> ids;
	if (text.substr(0, 3) == "\xEF\xBB\xBF") {
		text.remove_prefix(3);  // a UTF-8 BOM
	}
	int codeColumn = -1, ordinal = 0;
	bool header = true;
	while (!text.empty()) {
		size_t eol = text.find('\n');
		std::string_view line = text.substr(0, eol);
		text = eol == std::string_view::npos ? std::string_view {} : text.substr(eol + 1);
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1);
		}
		if (line.empty()) {
			continue;
		}
		std::vector<std::string_view> cells;
		for (size_t at = 0;;) {
			size_t tab = line.find('\t', at);
			cells.push_back(line.substr(at, tab == std::string_view::npos ? tab : tab - at));
			if (tab == std::string_view::npos) {
				break;
			}
			at = tab + 1;
		}
		if (header) {
			header = false;
			for (size_t i = 0; i < cells.size(); ++i) {
				if (cells[i] == "code") {
					codeColumn = static_cast<int>(i);
				}
			}
			if (codeColumn < 0) {
				return ids;
			}
			continue;
		}
		if (static_cast<size_t>(codeColumn) >= cells.size() || cells[codeColumn] == "Expansion") {
			continue;
		}
		ids.emplace(std::string(cells[codeColumn]), ordinal++);
	}
	return ids;
}

inline std::map<std::string, int, std::less<>> g_ids;

// Call once from D2RLoaderLoadPlugin, after plog::Init. Both mod-folder layouts are probed, as Cartographer does.
inline void Init(const D2RL::PluginContext* context) {
	g_ids.clear();
	if (context->modDirectory == nullptr || context->modDirectory[0] == L'\0' || context->activeMod == nullptr) {
		plog::Info("property ids: no mod folder, properties.txt not read");
		return;
	}
	const std::wstring tail = L"\\data\\global\\excel\\properties.txt";
	const std::wstring dir = context->modDirectory;
	std::wstring mod;
	for (const char* c = context->activeMod; *c != '\0'; ++c) {
		mod += static_cast<wchar_t>(static_cast<unsigned char>(*c));
	}
	for (const std::wstring& path : {dir + L"\\" + mod + L".mpq" + tail, dir + tail}) {
		FILE* f = _wfopen(path.c_str(), L"rb");
		if (f == nullptr) {
			continue;
		}
		std::string text;
		char buf[8192];
		for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) {
			text.append(buf, n);
		}
		fclose(f);
		g_ids = Parse(text);
		plog::Info("property ids: %zu codes read from properties.txt", g_ids.size());
		return;
	}
	plog::Info("property ids: properties.txt not found under the mod folder %ls (mod %s)", context->modDirectory, context->activeMod);
}

// The property number of `code`, or -1 (logged as an error) when properties.txt has no such row.
inline auto Resolve(const char* code) -> int {
	const auto it = g_ids.find(std::string_view(code));
	if (it == g_ids.end()) {
		static std::atomic<uint32_t> failures {0};  // a caller on a hot path can ask again for a missing code
		if (plog::Sample(failures)) {
			plog::Error("property ids: no properties.txt row for code \"%s\"; the feature that needs it is off", code);
		}
		return -1;
	}
	return it->second;
}

}  // namespace propids
