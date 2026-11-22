// The Invaders' legacy art (#1215): PD2 gives each Invader class its player token (monstats Code AM AI BA DZ NE PA SO) and
// loads the art from data\global\monsters\<tok>\; D2R keeps those files under data\global\chars\<tok>\ (the player
// path), already in every install's CASC. Rewrites one built file path in place: a COF or DCC path under monsters/<tok>/
// of one of the 7 tokens becomes the same path under chars/<tok>/. The path only gets shorter, so it never overflows.
#pragma once

#include <cctype>
#include <cstring>

namespace pd2::invader_art {

constexpr const char* Tokens[] = {"AM", "AI", "BA", "DZ", "NE", "PA", "SO"};
constexpr const char MonstersDir[] = "data\\global\\monsters\\";
constexpr const char CharsDir[] = "data\\global\\CHARS\\";  // as D2R's own player paths are spelled

// True when `path` was rewritten
inline bool Redirect(char* path) noexcept {
	constexpr size_t from = sizeof(MonstersDir) - 1, to = sizeof(CharsDir) - 1;
	if (path == nullptr || _strnicmp(path, MonstersDir, from) != 0) {
		return false;
	}
	const char* tok = path + from;
	if (tok[0] == '\0' || tok[1] == '\0' || tok[2] != '\\') {
		return false;
	}
	for (const char* t : Tokens) {
		if (std::toupper(static_cast<unsigned char>(tok[0])) == t[0] && std::toupper(static_cast<unsigned char>(tok[1])) == t[1]) {
			std::memmove(path + to, tok, std::strlen(tok) + 1);
			std::memcpy(path, CharsDir, to);
			return true;
		}
	}
	return false;
}

}  // namespace pd2::invader_art
