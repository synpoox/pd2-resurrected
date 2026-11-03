// Off-file reader (off_switch.h).
#include "off_switch.h"

#define NOMINMAX
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace off_switch {

namespace {
char g_words[512] = "";
}  // namespace

void Read(const wchar_t* configPath) noexcept {
	g_words[0] = '\0';
	if (configPath == nullptr || configPath[0] == L'\0') {
		return;
	}
	// <dir>\<name>.toml -> <dir>\<name>-off.txt
	wchar_t path[MAX_PATH] = L"";
	wcsncpy_s(path, configPath, _TRUNCATE);
	wchar_t* dot = wcsrchr(path, L'.');
	wchar_t* slash = wcsrchr(path, L'\\');
	if (dot == nullptr || slash == nullptr || dot < slash || static_cast<size_t>(dot - path) + 9 >= MAX_PATH) {
		return;
	}
	wcscpy_s(dot, MAX_PATH - static_cast<size_t>(dot - path), L"-off.txt");
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"rb") != 0 || f == nullptr) {
		return;
	}
	const size_t n = std::fread(g_words, 1, sizeof(g_words) - 1, f);
	g_words[n] = '\0';
	std::fclose(f);
	for (char* c = g_words; *c != '\0'; ++c) {
		const auto u = static_cast<unsigned char>(*c);
		*c = std::isalnum(u) ? static_cast<char>(std::tolower(u)) : ' ';
	}
	// one space between words, none at the ends
	size_t out = 0;
	for (size_t in = 0; g_words[in] != '\0'; ++in) {
		if (g_words[in] != ' ' || (out > 0 && g_words[out - 1] != ' ')) {
			g_words[out++] = g_words[in];
		}
	}
	if (out > 0 && g_words[out - 1] == ' ') {
		--out;
	}
	g_words[out] = '\0';
}

auto Off(const char* word) noexcept -> bool {
	const size_t len = std::strlen(word);
	if (len == 0) {
		return false;
	}
	for (const char* p = std::strstr(g_words, word); p != nullptr; p = std::strstr(p + 1, word)) {
		if ((p == g_words || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0')) {
			return true;
		}
	}
	return false;
}

auto Words() noexcept -> const char* {
	return g_words;
}

}  // namespace off_switch
