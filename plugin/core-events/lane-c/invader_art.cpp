// The Invaders' legacy art (#1215): a monstats Code of a player token (AM AI BA DZ NE PA SO, as PD2) makes D2R's legacy
// renderer ask for data\global\monsters\<tok>\..., but the player art is under data\global\chars\<tok>\ in every install's
// CASC. No file is copied or shipped: this rewrites the path D2R builds (invader_art_path.h).
//
// Why a native hook: the PluginSDK's ResourceService only serves d2rloader/<plugin-id>/ paths, and no other service sees
// or changes a game file path. The three path builders (D2R build 93847, found by their entry bytes, never an RVA):
//   DCC  FUN_14145a140 (the "%s\%s\%s\%s.dcc" builder; unit type 1 = monsters, base "data\global\monsters")
//   COF  FUN_140154120 and FUN_1403b6700 (the "%s\%s\COF\%s%s%s.COF" builders)
// Each writes the path into a caller buffer; the hook lets D2R write it, then rewrites it in place.
#include "invader_art.h"

#include "invader_art_path.h"
#include "log.h"
#include "sig_hook.h"

#include <atomic>
#include <cstdio>

namespace {

// Entry bytes: whole instructions (mov [rsp+..], rbx/rbp/rsi/rdi, push, sub rsp), also the scan pattern
constexpr uint8_t DccEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56,
                                0x41, 0x57, 0x48, 0x83, 0xec, 0x50, 0x41, 0x80, 0x39, 0x00};
constexpr uint8_t CofEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0xb4, 0x24, 0x80};
constexpr uint8_t CofUnitEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57, 0x41, 0x55, 0x41,
                                    0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x60};
// scan patterns: the entry plus the bytes after it that make the match unique
constexpr char DccPattern[] = "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 48 83 ec 50 41 80 39 00 49 8b e9 4d 8b f0 48 8b fa 48 8b d9";
constexpr char CofPattern[] = "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 50 48 8b b4 24 80 00 00 00 41 8b d0 41 8b d9 41 8b f8";
constexpr char CofUnitPattern[] = "48 89 5c 24 10 48 89 74 24 18 55 57 41 55 41 56 41 57 48 8b ec 48 83 ec 60 41 8b d8 44 8b f2 4c 8b e9";

using DccFn = uint64_t (*)(void* art, char* out, uint64_t size, char* name) noexcept;
using CofFn = void (*)(uint64_t, void*, uint32_t, int32_t, void*, char* out, uint64_t size) noexcept;
using CofUnitFn = void (*)(void*, uint64_t, int32_t, int32_t, void*, char* out, uint64_t size, int32_t*, int32_t, int32_t, uint8_t) noexcept;

DccFn g_dcc = nullptr;
CofFn g_cof = nullptr;
CofUnitFn g_cofUnit = nullptr;
std::atomic<uint32_t> g_redirected[3] {};

void Note(int which, const char* path) noexcept {
	if (g_redirected[which].fetch_add(1, std::memory_order_relaxed) == 0) {
		static const char* const Name[] = {"dcc", "cof", "cof (unit)"};
		plog::Info("invader art: first %s path redirected to the player art: %s", Name[which], path);
	}
}

auto DccHook(void* art, char* out, uint64_t size, char* name) noexcept -> uint64_t {
	const uint64_t r = g_dcc(art, out, size, name);
	if (pd2::invader_art::Redirect(out)) {
		Note(0, out);
	}
	return r;
}

void CofHook(uint64_t a, void* b, uint32_t c, int32_t d, void* e, char* out, uint64_t size) noexcept {
	g_cof(a, b, c, d, e, out, size);
	if (pd2::invader_art::Redirect(out)) {
		Note(1, out);
	}
}

void CofUnitHook(void* a, uint64_t b, int32_t c, int32_t d, void* e, char* out, uint64_t size, int32_t* h, int32_t i, int32_t j,
                 uint8_t k) noexcept {
	g_cofUnit(a, b, c, d, e, out, size, h, i, j, k);
	if (pd2::invader_art::Redirect(out)) {
		Note(2, out);
	}
}

}  // namespace

auto InstallInvaderArt(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	int on = 0;
	on += sigscan::HookBySignature(context, "invader art dcc path", DccPattern, 0, DccEntry, sizeof(DccEntry), DccHook, &g_dcc);
	on += sigscan::HookBySignature(context, "invader art cof path", CofPattern, 0, CofEntry, sizeof(CofEntry), CofHook, &g_cof);
	on += sigscan::HookBySignature(context, "invader art cof unit path", CofUnitPattern, 0, CofUnitEntry, sizeof(CofUnitEntry),
	                               CofUnitHook, &g_cofUnit);
	if (on == 0) {
		std::snprintf(report, reportSize, "off: no art path builder found");
		return 0;
	}
	std::snprintf(report, reportSize, "%d/3 art path builders hooked: monsters/<tok> -> chars/<tok> for AM AI BA DZ NE PA SO", on);
	if (on < 3) {
		plog::Warn("invader art: only %d/3 path builders installed, some Invader art may stay monster art", on);
	}
	plog::Info("invader art: redirect installed (%d/3 path builders)", on);
	return 1;
}

auto InvaderArtCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "invader art: %u dcc, %u cof, %u cof (unit) paths redirected", g_redirected[0].load(), g_redirected[1].load(),
	              g_redirected[2].load());
}
