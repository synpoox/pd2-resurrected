// cartographer-outdoor-spike: throwaway prototype for Cartographer's own outdoor level builder, phases S0 and S1
// (issue #938, plan docs/plans/cartographer-outdoor-builder.md on docs/map-kit-plan). NOT for merge. Standalone: links
// only d2rAI-toolkit's plugin_common and the PluginSDK, never plugin/common/pd2_engine.h.
//
// SDK first: the SDK (D2RLoader 1.3.1) has no level, DRLG or room service, so the build hooks below are native hooks
// (SDK InstallInlineHook, byte-checked at the entry). The byte checks go through the SDK's DiagnosticsService.
//
// S0, "cartographer probe-outdoor":
//   * byte checks of the entries of 0x3271c0, 0x3ee320, 0x3fe2e0, 0x3ecfc0, 0x3ee780, 0x3fa7f0 (and 0x3fac10, the
//     thunk 0x3e2b5ce): once at load, before any hook of ours, and again live through DiagnosticsService;
//   * which module holds the pointer [0x3e2ac38] that the outdoor thunk 0x3e2b5ce jumps through;
//   * call counts of 0x3fac10 (Act 1 outdoor builder), 0x3ee320 (D2R's outdoor entry) and 0x3ee780 (grid to rooms),
//     per level: enter the Blood Moor and ask again;
//   * the slot level is built once with the fixed jungle line, and every room is logged on both drlgs.
// S1: the slot level (levels.txt row, Act 5, DrlgType 3) is built by this plugin:
//   0x3271c0 hook -> write the level rectangle -> 0x3ee320 (D2R's outdoor entry; its Act 5 branch 0x3fe2e0 is our hook,
//   which stamps the jungle blocks with 0x3ecfc0) -> 0x3ee780 (D2R makes the rooms) -> the native tail of 0x3271c0 run
//   with the drlg type set to 0 for the call, so D2RCore's GenerateOutdoorLevel is never reached for the slot.
// Config: <game>\d2rloader\config\cartographer-outdoor-spike.txt, `key=value` lines. No file: nothing is hooked.
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/diagnostics.h>

#include "crash_guard.h"
#include "d2r.h"
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr const char* PluginName = "cartographer-outdoor-spike";
constexpr const char* PluginVersion = "0.0.1";

// --- 93847 addresses (docs/research/drlg-steering.md, docs/plans/cartographer-outdoor-builder.md) ---------------------
constexpr uint64_t LevelBuildRva = 0x3271c0;     // (dataFlag, level): reseeds, builds the rooms by DRLG type, native tail
constexpr uint64_t OutdoorEntryRva = 0x3ee320;   // (dataFlag, level): grid, outline, act builder, then 0x3ee780
constexpr uint64_t Act5BuilderRva = 0x3fe2e0;    // (dataFlag, level): D2R's Act 5 outdoor builder (OutSiege.cpp)
constexpr uint64_t Act1BuilderRva = 0x3fac10;    // (dataFlag, level): D2R's Act 1 outdoor builder (Blood Moor ...)
constexpr uint64_t StampRva = 0x3ecfc0;          // (dataFlag, level, cellX, cellY, lvlprest, file or -1, flag)
constexpr uint64_t RoomsRva = 0x3ee780;          // (dataFlag, level): grid cells -> preset rooms and fill rooms
constexpr uint64_t GridAllocRva = 0x3fa7f0;      // (grid, w, h)
constexpr uint64_t GridReadRva = 0x3fa790;       // (grid, x, y) -> cell value
constexpr uint64_t ReseedRva = 0x367110;         // (seed*, value)
constexpr uint64_t LevelDefRva = 0x32c200;       // (dataFlag, level id) -> levels.txt record
constexpr uint64_t ActLookupThunkRva = 0x3e2b5c8;  // D2RLoader thunk into D2RCore: (dataFlag, level id) -> act (0-4)
constexpr uint64_t OutdoorThunkRva = 0x3e2b5ce;    // D2RLoader thunk: JMP [0x3e2ac38], D2RCore's GenerateOutdoorLevel
constexpr uint64_t OutdoorPointerRva = 0x3e2ac38;  // the pointer the outdoor thunk jumps through

// Entry bytes in the 93847 dump (docs/research/*): hooks need an instruction boundary at the end.
constexpr uint8_t LevelBuildEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20};
constexpr uint8_t OutdoorEntryBytes[] = {0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41,
                                         0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50};
constexpr uint8_t Act5BuilderEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xd9};
constexpr uint8_t Act1BuilderEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb6, 0xf9, 0x48, 0x8b, 0xda};
constexpr uint8_t StampEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x44, 0x89, 0x4c, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55};
constexpr uint8_t RoomsEntry[] = {0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xec, 0x98, 0x00, 0x00, 0x00};
constexpr uint8_t GridAllocEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr uint8_t ThunkBytes[] = {0xff, 0x25, 0x64, 0xf6, 0xff, 0xff};  // JMP [RIP-0x99c] -> 0x3e2ac38

struct Probe {
	const char* name;
	uint64_t rva;
	const uint8_t* bytes;
	uint32_t size;
};
constexpr Probe Probes[] = {
	{"0x3271c0 level build", LevelBuildRva, LevelBuildEntry, sizeof(LevelBuildEntry)},
	{"0x3ee320 outdoor entry", OutdoorEntryRva, OutdoorEntryBytes, sizeof(OutdoorEntryBytes)},
	{"0x3fe2e0 act 5 builder", Act5BuilderRva, Act5BuilderEntry, sizeof(Act5BuilderEntry)},
	{"0x3ecfc0 piece stamp", StampRva, StampEntry, sizeof(StampEntry)},
	{"0x3ee780 grid to rooms", RoomsRva, RoomsEntry, sizeof(RoomsEntry)},
	{"0x3fa7f0 grid alloc", GridAllocRva, GridAllocEntry, sizeof(GridAllocEntry)},
	{"0x3fac10 act 1 builder", Act1BuilderRva, Act1BuilderEntry, sizeof(Act1BuilderEntry)},
	{"0x3e2b5ce outdoor thunk", OutdoorThunkRva, ThunkBytes, sizeof(ThunkBytes)},
};
constexpr int ProbeCount = static_cast<int>(sizeof(Probes) / sizeof(Probes[0]));

// --- Level and room fields --------------------------------------------------------------------------------------
constexpr int LevelDrlgType = 0x00, LevelRoomCount = 0x08, LevelFirstRoom = 0x10, LevelGrid = 0x18;
constexpr int LevelX = 0x24, LevelY = 0x28, LevelW = 0x2c, LevelH = 0x30;
constexpr int LevelDrlg = 0x1c8, LevelTileset = 0x1e0, LevelSeed = 0x1e4, LevelId = 0x1f8;
constexpr int DrlgFlags = 0x110, DrlgDifficulty = 0x830, DrlgSeedBase = 0x860;  // flags bit 0: the client's DRLG
constexpr int GridCellsW = 0xb0, GridCellsH = 0xb4, GridNeighbours = 0x338, GridPieces = 0x08, GridFlags = 0x58;
constexpr int RoomPiece = 0x40, RoomNext = 0x48, RoomX = 0x60, RoomY = 0x64, RoomW = 0x68, RoomH = 0x6c;
constexpr int DefOffsetX = 0x24, DefOffsetY = 0x28;

// --- Config ---------------------------------------------------------------------------------------------------------
struct Config {
	int level = -1;            // levels.txt id of the slot row
	char line[64] = "E,EW,EW,EW,W";  // the fixed jungle line, left to right
	int offsetX = 6000;        // used when the slot row's OffsetX is not a fixed value
	int offsetY = 2400;
	bool stamp = true;         // false: leave the Act 5 branch to D2R (control run)
};
Config g_config;
char g_status[300] = "off: no config";
uint8_t g_mask[32];
int g_blocks = 0;

// --- State ----------------------------------------------------------------------------------------------------------
using LevelBuildFn = void (*)(uint8_t, void*) noexcept;
using BuilderFn = void (*)(uint8_t, void*) noexcept;
using StampFn = void (*)(uint64_t, void*, int32_t, int32_t, int32_t, int32_t, char) noexcept;
using GridReadFn = uint64_t (*)(void*, int32_t, int32_t) noexcept;
using ReseedFn = void (*)(void*, int32_t) noexcept;
using LevelDefFn = const uint8_t* (*)(uint64_t, int32_t) noexcept;
using ActLookupFn = int8_t (*)(uint8_t, int32_t) noexcept;

const D2RL::PluginContext* g_context = nullptr;
LevelBuildFn g_levelBuild = nullptr;
BuilderFn g_outdoorEntry = nullptr, g_act5 = nullptr, g_act1 = nullptr, g_rooms = nullptr;
bool g_hookBuild = false, g_hookAct5 = false;

// Byte checks done at load, before our own hooks (so a changed entry is never our own hook).
int g_preState[ProbeCount];  // 0 match, 1 differs, 2 unreadable
uint8_t g_preBytes[ProbeCount][24];

std::atomic<uint32_t> g_act1Calls{0}, g_entryCalls{0}, g_roomsCalls{0}, g_slotBuilds{0};
struct LevelCount {
	std::atomic<int32_t> id{-1};
	std::atomic<uint32_t> act1{0}, entry{0}, rooms{0};
};
LevelCount g_levels[48];
thread_local void* t_slotLevel = nullptr;  // the slot level this thread is building right now

template <typename T>
auto F(const void* p, int off) noexcept -> T& {
	return *reinterpret_cast<T*>(const_cast<uint8_t*>(static_cast<const uint8_t*>(p)) + off);
}

auto CountFor(int32_t id) noexcept -> LevelCount* {
	for (LevelCount& c : g_levels) {
		const int32_t cur = c.id.load();
		if (cur == id) return &c;
		if (cur < 0) {
			int32_t expect = -1;
			if (c.id.compare_exchange_strong(expect, id) || expect == id) return &c;
		}
	}
	return nullptr;
}

auto IsSlot(const void* level) noexcept -> bool {
	return level != nullptr && g_config.level >= 0 && F<int32_t>(level, LevelId) == g_config.level;
}

auto Side(const void* level) noexcept -> const char* {
	const void* drlg = F<void*>(level, LevelDrlg);
	return drlg != nullptr && (F<uint32_t>(drlg, DrlgFlags) & 1) != 0 ? "client" : "server";
}

// --- Guarded reads (a bad pointer in a log must not crash the game) ---------------------------------------------------
auto ReadBytes(const void* from, void* to, size_t n) noexcept -> bool {
	__try {
		std::memcpy(to, from, n);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Piece record of a preset room: {lvlprest id, file index, ...}, +0x18/+0x1c position, +0x20/+0x24 size.
struct PieceInfo {
	int32_t id = -1, file = -1, x = 0, y = 0, w = 0, h = 0;
};
auto ReadPiece(const void* room, PieceInfo* out) noexcept -> bool {
	const uint8_t* piece = nullptr;
	if (!ReadBytes(static_cast<const uint8_t*>(room) + RoomPiece, &piece, sizeof(piece)) || piece == nullptr) return false;
	int32_t v[10];
	if (!ReadBytes(piece, v, sizeof(v))) return false;
	out->id = v[0];
	out->file = v[1];
	out->x = v[6];
	out->y = v[7];
	out->w = v[8];
	out->h = v[9];
	return true;
}

void LogRooms(const void* level, const char* when) noexcept {
	const int lx = F<int32_t>(level, LevelX), ly = F<int32_t>(level, LevelY);
	plog::Info("[%s] %s: level %d tileset %d type %d: %d rooms, level rect (%d,%d %dx%d)", Side(level), when,
	           F<int32_t>(level, LevelId), F<int32_t>(level, LevelTileset), F<int32_t>(level, LevelDrlgType),
	           F<int32_t>(level, LevelRoomCount), lx, ly, F<int32_t>(level, LevelW), F<int32_t>(level, LevelH));
	int n = 0;
	for (void* r = F<void*>(level, LevelFirstRoom); r != nullptr && n < 4000; r = F<void*>(r, RoomNext), n++) {
		PieceInfo p;
		const bool havePiece = ReadPiece(r, &p);
		plog::Info("  room %s%d file %d at (%d,%d) %dx%d", havePiece ? "piece " : "fill ", havePiece ? p.id : -1,
		           havePiece ? p.file : -1, F<int32_t>(r, RoomX) - lx, F<int32_t>(r, RoomY) - ly, F<int32_t>(r, RoomW),
		           F<int32_t>(r, RoomH));
	}
}

// The piece grid (grid block +0x08): one character per 8x8-tile cell, a letter per distinct piece origin.
void LogGrid(const void* level, uint8_t dataFlag) noexcept {
	const uint8_t* grid = F<uint8_t*>(level, LevelGrid);
	if (grid == nullptr) return;
	const int w = F<int32_t>(grid, GridCellsW), h = F<int32_t>(grid, GridCellsH);
	plog::Info("[%s] grid block %p: %dx%d cells, neighbours %p", Side(level), static_cast<const void*>(grid), w, h,
	           F<void*>(grid, GridNeighbours));
	if (w <= 0 || h <= 0 || w > 200 || h > 64) return;
	auto read = d2r::Engine<GridReadFn>(GridReadRva);
	for (int y = 0; y < h; y++) {
		char row[256];
		int used = 0;
		for (int x = 0; x < w && used < 250; x++) {
			const uint64_t flags = read(const_cast<uint8_t*>(grid) + GridFlags, x, y);
			const uint64_t piece = read(const_cast<uint8_t*>(grid) + GridPieces, x, y);
			row[used++] = piece != 0 ? '#' : ((flags & 0x200) != 0 ? '+' : ((flags & 0x100) != 0 ? ' ' : '.'));
		}
		row[used] = '\0';
		plog::Info("  grid %2d |%s|", y, row);
	}
	(void)dataFlag;
}

// --- The S1 build -------------------------------------------------------------------------------------------------------
void ParseLine(const char* text) noexcept {
	g_blocks = 0;
	std::memset(g_mask, 0, sizeof(g_mask));
	uint8_t mask = 0;
	bool any = false;
	for (const char* c = text;; c++) {
		if (*c == ',' || *c == '\0') {
			if (any && g_blocks < 32) g_mask[g_blocks++] = mask;
			mask = 0;
			any = false;
			if (*c == '\0') break;
			continue;
		}
		any = true;
		switch (*c) {  // the native jungle's bits: W 1, E 2, S 4, N 8
			case 'W': mask |= 1; break;
			case 'E': mask |= 2; break;
			case 'S': mask |= 4; break;
			case 'N': mask |= 8; break;
			default: break;
		}
	}
}

// 0x3fe2e0 for the slot: stamp the jungle line, piece = 0x211 + mask (LvlPrest 530-544), one 32x32-tile block = 4x4 cells.
void StampLine(uint8_t dataFlag, void* level) noexcept {
	auto stamp = d2r::Engine<StampFn>(StampRva);
	for (int i = 0; i < g_blocks; i++) {
		if (g_mask[i] == 0) {
			plog::Warn("[%s] block %d has mask 0 (LvlPrest 529, Act 3 Town): skipped", Side(level), i);
			continue;
		}
		stamp(dataFlag, level, i * 4, 0, 0x211 + g_mask[i], -1, 0);
	}
	plog::Info("[%s] stamped %d jungle blocks on level %d", Side(level), g_blocks, F<int32_t>(level, LevelId));
}

void Act5Hook(uint8_t dataFlag, void* level) noexcept {
	if (IsSlot(level) && t_slotLevel == level && g_config.stamp) {
		StampLine(dataFlag, level);
		return;
	}
	g_act5(dataFlag, level);
}

void OutdoorEntryHook(uint8_t dataFlag, void* level) noexcept {
	g_entryCalls.fetch_add(1);
	if (level != nullptr) {
		if (LevelCount* c = CountFor(F<int32_t>(level, LevelId))) c->entry.fetch_add(1);
	}
	g_outdoorEntry(dataFlag, level);
}

void Act1Hook(uint8_t dataFlag, void* level) noexcept {
	g_act1Calls.fetch_add(1);
	if (level != nullptr) {
		if (LevelCount* c = CountFor(F<int32_t>(level, LevelId))) c->act1.fetch_add(1);
		plog::Info("[%s] 0x3fac10 (act 1 outdoor builder) called for level %d", Side(level), F<int32_t>(level, LevelId));
	}
	g_act1(dataFlag, level);
}

void RoomsHook(uint8_t dataFlag, void* level) noexcept {
	g_roomsCalls.fetch_add(1);
	if (level != nullptr) {
		if (LevelCount* c = CountFor(F<int32_t>(level, LevelId))) c->rooms.fetch_add(1);
	}
	g_rooms(dataFlag, level);
}

void BuildSlot(uint8_t dataFlag, void* level) noexcept {
	const int id = F<int32_t>(level, LevelId);
	uint8_t* drlg = F<uint8_t*>(level, LevelDrlg);
	g_slotBuilds.fetch_add(1);
	const int8_t act = d2r::Engine<ActLookupFn>(ActLookupThunkRva)(dataFlag, id);
	plog::Info("[%s] BUILD slot level %d (dataFlag %u, drlg %p, level type %d, drlg type %d): D2RCore act lookup says %d",
	           Side(level), id, dataFlag, static_cast<void*>(drlg), F<int32_t>(level, LevelTileset),
	           F<int32_t>(level, LevelDrlgType), act);

	// The level rectangle: act start-up places outdoor levels, never a slot, so +0x24..+0x30 are 0 here. Origin from the
	// row's fixed OffsetX/OffsetY, size from the line.
	const uint8_t* def = d2r::Engine<LevelDefFn>(LevelDefRva)(dataFlag, id);
	int ox = def != nullptr ? F<int32_t>(def, DefOffsetX) : -1, oy = def != nullptr ? F<int32_t>(def, DefOffsetY) : -1;
	if (ox < 0 || oy < 0) {
		ox = g_config.offsetX;
		oy = g_config.offsetY;
	}
	F<int32_t>(level, LevelX) = ox;
	F<int32_t>(level, LevelY) = oy;
	F<int32_t>(level, LevelW) = g_blocks * 32;
	F<int32_t>(level, LevelH) = 32;

	// The same seed the original would draw: the level RNG decides each piece's file.
	d2r::Engine<ReseedFn>(ReseedRva)(static_cast<uint8_t*>(level) + LevelSeed,
	                                 static_cast<int32_t>(F<uint32_t>(drlg, DrlgSeedBase)) + id);

	t_slotLevel = level;
	g_outdoorEntry(dataFlag, level);  // our Act 5 branch stamps the line, then D2R makes the rooms
	t_slotLevel = nullptr;

	// Native tail of 0x3271c0 (saved room bits, warp-spot scan): drlg type 0 matches no branch of its dispatch, so the
	// tail runs and D2RCore's GenerateOutdoorLevel does not.
	F<int32_t>(level, LevelDrlgType) = 0;
	g_levelBuild(dataFlag, level);
	F<int32_t>(level, LevelDrlgType) = 3;

	LogGrid(level, dataFlag);
	LogRooms(level, "after build");
}

void LevelBuildHook(uint8_t dataFlag, void* level) noexcept {
	if (IsSlot(level) && F<int32_t>(level, LevelDrlgType) == 3 && g_blocks > 0 && t_slotLevel == nullptr) {
		BuildSlot(dataFlag, level);
		return;
	}
	g_levelBuild(dataFlag, level);
}

// --- Config file -----------------------------------------------------------------------------------------------------
auto ReadConfig(const wchar_t* dir) noexcept -> bool {
	if (dir == nullptr) return false;
	// pluginConfigPath names the plugin's own config file (<game>\d2rloader\config\<id>.toml): use its folder
	wchar_t path[600];
	std::swprintf(path, 600, L"%ls", dir);
	wchar_t* slash = std::wcsrchr(path, L'\\');
	if (slash != nullptr && std::wcschr(slash, L'.') != nullptr) *slash = L'\0';
	const size_t used = std::wcslen(path);
	std::swprintf(path + used, 600 - used, L"\\%hs.txt", PluginName);
	plog::Info("config %ls", path);
	FILE* f = _wfopen(path, L"r");
	if (f == nullptr) return false;
	char text[200];
	while (std::fgets(text, sizeof(text), f) != nullptr) {
		int v = 0;
		char s[64];
		if (std::sscanf(text, "level=%d", &v) == 1) g_config.level = v;
		else if (std::sscanf(text, "line=%63s", s) == 1) std::snprintf(g_config.line, sizeof(g_config.line), "%s", s);
		else if (std::sscanf(text, "offsetx=%d", &v) == 1) g_config.offsetX = v;
		else if (std::sscanf(text, "offsety=%d", &v) == 1) g_config.offsetY = v;
		else if (std::sscanf(text, "stamp=%d", &v) == 1) g_config.stamp = v != 0;
	}
	std::fclose(f);
	return g_config.level >= 0;
}

// --- S0 probe --------------------------------------------------------------------------------------------------------
void Say(const D2RL::PluginContext* ctx, const char* fmt, ...) noexcept {
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	plog::Info("%s", line);
	if (ctx != nullptr) ctx->WriteConsoleMessage(line);
}

void PreCheck(uint64_t exeBase) noexcept {
	for (int i = 0; i < ProbeCount; i++) {
		std::memset(g_preBytes[i], 0, sizeof(g_preBytes[i]));
		const uint32_t n = Probes[i].size < sizeof(g_preBytes[i]) ? Probes[i].size : sizeof(g_preBytes[i]);
		if (!ReadBytes(reinterpret_cast<const void*>(exeBase + Probes[i].rva), g_preBytes[i], n)) {
			g_preState[i] = 2;
		} else {
			g_preState[i] = std::memcmp(g_preBytes[i], Probes[i].bytes, n) == 0 ? 0 : 1;
		}
		char hex[80];
		int used = 0;
		for (uint32_t k = 0; k < n && used < 70; k++) used += std::snprintf(hex + used, sizeof(hex) - used, "%02x ", g_preBytes[i][k]);
		plog::Info("byte check (load, before our hooks) %-26s %s: %s", Probes[i].name,
		           g_preState[i] == 0 ? "MATCH" : (g_preState[i] == 1 ? "DIFFERS" : "UNREADABLE"), hex);
	}
}

auto AllPreChecksPass() noexcept -> bool {
	for (int i = 0; i < ProbeCount; i++)
		if (g_preState[i] != 0) return false;
	return true;
}

const char* StateName(D2RL::Diagnostics::ModificationState s) noexcept {
	using D2RL::Diagnostics::ModificationState;
	return s == ModificationState::Unchanged ? "unchanged" : (s == ModificationState::Tracked ? "tracked (a hook or patch)" : "UNTRACKED change");
}

// Which module owns an address; D2RCore is packed on disk and its code may sit in a heap block.
void DescribeAddress(const D2RL::PluginContext* ctx, uint64_t addr) noexcept {
	MEMORY_BASIC_INFORMATION mbi {};
	if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0) {
		Say(ctx, "  VirtualQuery failed for %p", reinterpret_cast<void*>(addr));
		return;
	}
	const char* type = mbi.Type == MEM_IMAGE ? "image" : (mbi.Type == MEM_PRIVATE ? "private (heap/VirtualAlloc)" : "mapped");
	Say(ctx, "  target %p: region base %p, allocation base %p, size 0x%llx, %s, protect 0x%lx", reinterpret_cast<void*>(addr),
	    mbi.BaseAddress, mbi.AllocationBase, static_cast<unsigned long long>(mbi.RegionSize), type, mbi.Protect);
	HMODULE mod = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCWSTR>(addr), &mod) && mod != nullptr) {
		wchar_t name[MAX_PATH] = L"";
		GetModuleFileNameW(mod, name, MAX_PATH);
		Say(ctx, "  module containing it: %ls (base %p)", name, static_cast<void*>(mod));
	} else {
		Say(ctx, "  no loaded module contains it");
	}
	// Any loaded module whose name mentions D2RCore, and whether the target sits inside it.
	HMODULE mods[512];
	DWORD needed = 0;
	if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed) != 0) {
		const DWORD count = needed / sizeof(HMODULE);
		for (DWORD i = 0; i < count && i < 512; i++) {
			wchar_t name[MAX_PATH] = L"";
			GetModuleFileNameW(mods[i], name, MAX_PATH);
			if (std::wcsstr(name, L"D2RCore") == nullptr && std::wcsstr(name, L"d2rcore") == nullptr) continue;
			MODULEINFO_LITE:
			{
				const uint8_t* base = reinterpret_cast<const uint8_t*>(mods[i]);
				IMAGE_DOS_HEADER dos {};
				IMAGE_NT_HEADERS64 nt {};
				uint64_t size = 0;
				if (ReadBytes(base, &dos, sizeof(dos)) && ReadBytes(base + dos.e_lfanew, &nt, sizeof(nt))) size = nt.OptionalHeader.SizeOfImage;
				Say(ctx, "  loaded module %ls base %p size 0x%llx; target %s it", name, static_cast<const void*>(base),
				    static_cast<unsigned long long>(size),
				    addr >= reinterpret_cast<uint64_t>(base) && addr < reinterpret_cast<uint64_t>(base) + size ? "IS INSIDE" : "is not inside");
			}
		}
	}
}

auto ProbeCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
	const D2RL::PluginContext* ctx = command->plugin;
	const char* args = command->args != nullptr ? command->args : "";
	while (*args == ' ') args++;
	const uint64_t base = d2r::imageBase;
	if (std::strncmp(args, "reset", 5) == 0) {
		for (LevelCount& c : g_levels) {
			c.id = -1;
			c.act1 = c.entry = c.rooms = 0;
		}
		g_act1Calls = g_entryCalls = g_roomsCalls = 0;
		Say(ctx, "%s: counters reset", PluginName);
		return D2RL::ConsoleCommandResult::Handled;
	}
	Say(ctx, "%s %s: %s", PluginName, PluginVersion, g_status);
	Say(ctx, "S0 byte checks at load (before our hooks): %s", AllPreChecksPass() ? "ALL MATCH" : "SOME DIFFER");
	const D2RL::DiagnosticsService* diag = nullptr;
	const bool haveDiag = ctx->QueryService(&diag) == D2RL::ServiceQueryResult::Success && D2RL::HasDiagnosticsServiceField(diag, D2RL::DiagnosticsServiceRequiredSize);
	for (int i = 0; i < ProbeCount; i++) {
		char owner[96] = "";
		const char* live = "n/a";
		if (haveDiag) {
			D2RL::Diagnostics::HookQuery q {.structSize = D2RL::Diagnostics::HookQuerySize, .rva = Probes[i].rva, .expected = Probes[i].bytes, .expectedSize = Probes[i].size};
			D2RL::Diagnostics::HookStatus st {.structSize = D2RL::Diagnostics::HookStatusSize};
			if (diag->queryHookStatus(ctx, &q, &st) == D2RL::Diagnostics::Result::Success) {
				live = StateName(st.state);
				if (st.ownerCount > 0) std::snprintf(owner, sizeof(owner), ", owner %s (%u)", st.ownerPluginId, st.ownerCount);
			} else {
				live = "query failed";
			}
		}
		Say(ctx, "  %-26s load: %s | live: %s%s", Probes[i].name, g_preState[i] == 0 ? "match" : (g_preState[i] == 1 ? "DIFFERS" : "unreadable"), live, owner);
	}
	uint64_t ptr = 0;
	if (ReadBytes(reinterpret_cast<const void*>(base + OutdoorPointerRva), &ptr, sizeof(ptr))) {
		Say(ctx, "[0x3e2ac38] = %p (the pointer behind the outdoor thunk 0x3e2b5ce)", reinterpret_cast<void*>(ptr));
		DescribeAddress(ctx, ptr);
		uint8_t first[16] = {};
		if (ReadBytes(reinterpret_cast<void*>(ptr), first, sizeof(first))) {
			char hex[64];
			int u = 0;
			for (uint8_t b : first) u += std::snprintf(hex + u, sizeof(hex) - u, "%02x ", b);
			Say(ctx, "  first bytes there: %s", hex);
		}
	} else {
		Say(ctx, "[0x3e2ac38] unreadable");
	}
	Say(ctx, "call counts since load or reset: 0x3fac10 = %u, 0x3ee320 = %u, 0x3ee780 = %u, slot builds = %u",
	    g_act1Calls.load(), g_entryCalls.load(), g_roomsCalls.load(), g_slotBuilds.load());
	for (LevelCount& c : g_levels) {
		const int32_t id = c.id.load();
		if (id >= 0) Say(ctx, "  level %d: 0x3fac10 x%u, 0x3ee320 x%u, 0x3ee780 x%u", id, c.act1.load(), c.entry.load(), c.rooms.load());
	}
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "cartographer-outdoor-spike",
	.name        = "Cartographer outdoor spike",
	.version     = "0.0.1",
	.author      = "Synpoo",
	.description = "Throwaway prototype: S0 probe and S1 jungle line for Cartographer's outdoor level builder.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) return false;
	g_context = context;
	plog::Init(context, PluginName);
	crash_guard::Install(PluginVersion, context->exeBase);
	if (!d2r::Bind(context->exeBase)) {
		std::snprintf(g_status, sizeof(g_status), "off: unknown game image");
	} else {
		PreCheck(context->exeBase);
		const bool haveConfig = ReadConfig(context->pluginConfigPath);
		if (haveConfig) ParseLine(g_config.line);
		if (!AllPreChecksPass()) {
			std::snprintf(g_status, sizeof(g_status), "off: byte check failed (see log)");
		} else if (!haveConfig) {
			std::snprintf(g_status, sizeof(g_status), "probe only: no %s.txt with level=", PluginName);
			// Counting hooks still go in: the Blood Moor count needs no slot level.
			g_hookBuild = false;
			context->InstallInlineHook(Act1BuilderRva, Act1BuilderEntry, sizeof(Act1BuilderEntry), Act1Hook, &g_act1);
			context->InstallInlineHook(OutdoorEntryRva, OutdoorEntryBytes, sizeof(OutdoorEntryBytes), OutdoorEntryHook, &g_outdoorEntry);
			context->InstallInlineHook(RoomsRva, RoomsEntry, sizeof(RoomsEntry), RoomsHook, &g_rooms);
		} else {
			const bool a = context->InstallInlineHook(LevelBuildRva, LevelBuildEntry, sizeof(LevelBuildEntry), LevelBuildHook, &g_levelBuild);
			const bool b = context->InstallInlineHook(OutdoorEntryRva, OutdoorEntryBytes, sizeof(OutdoorEntryBytes), OutdoorEntryHook, &g_outdoorEntry);
			const bool c = context->InstallInlineHook(Act5BuilderRva, Act5BuilderEntry, sizeof(Act5BuilderEntry), Act5Hook, &g_act5);
			const bool d = context->InstallInlineHook(Act1BuilderRva, Act1BuilderEntry, sizeof(Act1BuilderEntry), Act1Hook, &g_act1);
			const bool e = context->InstallInlineHook(RoomsRva, RoomsEntry, sizeof(RoomsEntry), RoomsHook, &g_rooms);
			g_hookBuild = a;
			g_hookAct5 = c;
			std::snprintf(g_status, sizeof(g_status), "%s: slot level %d, line %s (%d blocks); hooks build %d, entry %d, act5 %d, act1 %d, rooms %d",
			              (a && b && c && d && e) ? "on" : "FAILED", g_config.level, g_config.line, g_blocks, a, b, c, d, e);
		}
	}
	plog::Info("%s", g_status);
	return context->RegisterConsoleCommand("cartographer", ProbeCommand,
	                                       "Cartographer outdoor spike: `cartographer probe-outdoor` (S0 report), `... reset`.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
}
