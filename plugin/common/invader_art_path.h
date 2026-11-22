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

// The Invaders wear every base they roll (#1708, invader_dress.h), but their legacy art is the player art in the class's
// one monstats2 weapon class (BaseW), which has no DCC for some of them: such a token is drawn as the nearest that has
// one (HD shows the item's own model). tools/tests/test_invader_dress.py checks every listed token has legacy art.
struct Near {
	const char* cls;
	const char* from;
	const char* to;
};
constexpr Near Nearest[] = {
	{"AM", "lxb", "lbw"}, {"AM", "hxb", "lbw"},  // a crossbow in the bow pose: the long bow
	{"AI", "dgr", "ktr"}, {"AI", "dir", "ktr"},  // a dagger in the claw pose: the katar
	{"BA", "lax", "axe"}, {"BA", "btx", "axe"}, {"BA", "gix", "axe"}, {"BA", "hal", "axe"}, {"BA", "pax", "axe"}, {"BA", "scy", "axe"},
	{"BA", "mau", "whm"}, {"BA", "dgr", "ssd"},
	{"BA", "spr", "lsd"}, {"BA", "tri", "lsd"}, {"BA", "brn", "lsd"}, {"BA", "pik", "lsd"}, {"BA", "jav", "lsd"}, {"BA", "pil", "lsd"},
	{"BA", "glv", "lsd"},  // two-handers, spears, javelins and knives in the dual-wield pose
	{"SO", "ob1", "sst"}, {"SO", "ob3", "sst"}, {"SO", "ob4", "sst"},  // an orb in the staff pose: the short staff
};

// On a redirected DCC path (data\global\CHARS\<tok>\<part>\<tok><part><item><mode><wclass>.dcc), swaps an item token
// with no legacy art for its nearest. True when swapped. Same length, in place.
inline bool NearestArt(char* path) noexcept {
	constexpr size_t to = sizeof(CharsDir) - 1;
	if (path == nullptr || _strnicmp(path, CharsDir, to) != 0 || std::strlen(path) < to + 13) {
		return false;
	}
	const char* cls = path + to;
	char* item = path + to + 10;  // <tok>\<part>\<tok><part> is 10 characters
	if (cls[2] != '\\' || cls[5] != '\\') {
		return false;
	}
	for (const Near& n : Nearest) {
		if (_strnicmp(cls, n.cls, 2) == 0 && _strnicmp(item, n.from, 3) == 0) {
			std::memcpy(item, n.to, 3);
			return true;
		}
	}
	return false;
}

}  // namespace pd2::invader_art
