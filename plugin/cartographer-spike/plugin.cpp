// Cartographer spike (#927): erase a level in a running game and rebuild it with a new seed.
//
// Test build only, kept on the spike branch. Standalone: it links d2rAI-toolkit's plugin_common and the PluginSDK, and
// never plugin/common/pd2_engine.h. Every address below was found in the 93847 Ghidra project for this spike
// (docs/research/level-regeneration.md has the evidence).
//
// The SDK has no level, DRLG or room service, so this needs native code ("SDK before native hooks": no service fits).
//
// How it works. D2 already frees and rebuilds levels while a game runs: every 11 server frames and 13 client frames,
// "DungeonRemovePastLevels" (0x2efff0 -> 0x327370) frees each built level nobody is near (0x327e70 says so) with
// 0x327c40(flag, level, 1). The level record stays in the act's list; the next visit builds it again in 0x3271c0, which
// re-seeds the level from drlg +0x860 + the level id, so it comes back the same. A rebuild with a new layout is that
// same native unload plus two changes:
//  1. the saved per-room bits (level +0x258, kept by the unload so a rebuilt level does not repopulate) are dropped,
//     so the new rooms populate afresh;
//  2. a hook on the level build (0x3271c0) and on the level get-or-make (0x3267c0) puts a per-level seed in drlg +0x860
//     for that call only. The override is keyed by the act's seed (drlg +0x840) and the level id, so the server's and
//     the local client's copies of the act both build the same new layout.
//
// Console: "cartographer" (status), "cartographer regen <levelId>".
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/threads.h>

#include "build_guard.h"
#include "crash_guard.h"
#include "d2r.h"
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CARTO_PLUGIN_NAME
#define CARTO_PLUGIN_NAME "cartographer-spike"
#endif
#ifndef CARTO_PLUGIN_VERSION
#define CARTO_PLUGIN_VERSION "0.0.1"
#endif

namespace {

// --- Engine facts (93847) -----------------------------------------------------------------------------------------
namespace rv {
// D2Common\src\Drlg\Drlg.cpp
constexpr uint64_t LevelGetOrMake = 0x3267c0;  // (u8 flag, drlg, levelId) -> level: walks drlg +0x868, else allocates
constexpr uint64_t LevelBuild = 0x3271c0;      // (u8 flag, level): seeds level +0x1e4 from drlg +0x860 + id, builds rooms
constexpr uint64_t LevelFree = 0x327c40;       // (u8 flag, level, keep): frees the rooms; keep 1 saves room bits at +0x258
constexpr uint64_t LevelRemovable = 0x327e70;  // (u8 flag, level) -> 1 when every room (and the rooms bordering it) is
                                               // unused: room +0x70 status >= 4 and no +0x50 bit 0x100000
// Allocator D2Common frees level arrays with: 0xa20bb0(); p = 0xa20c80(); p->vtbl[4](p, ptr)
constexpr uint64_t AllocInit = 0xa20bb0;
constexpr uint64_t Allocator = 0xa20c80;
// Server: the game from a game id
constexpr uint64_t GameKey = 0x4061e0;   // (u16 id) -> key, 0 for an empty slot
constexpr uint64_t GameById = 0x4062f0;  // (key) -> game or null
constexpr uint64_t ClientUnit = 0x484c90;  // (client, 0) -> player
constexpr uint64_t UnitRoom = 0x34b440;    // (unit) -> room
constexpr uint64_t RoomLevelId = 0x2efc10;  // (room) -> level id
// Client: its act (what 0x94b40 passes to the past-level remover) and data flag
constexpr uint64_t ClientAct = 0x2a233b0;    // u64 global
constexpr uint64_t ClientFlagBase = 0x2a210a0;  // u8 at +0x9cf
constexpr int ClientFlagOffset = 0x9cf;
}  // namespace rv
// Game
constexpr int GameDataFlag = 0x106, GameClients = 0x138, ClientNext = 0x628, GameActs = 0x190, GameActCount = 0x198;
// Act (0x90 bytes, 0x2ef1c0): +0x14 seed, +0x70 drlg
constexpr int ActDrlg = 0x70;
// Drlg (0x2a9a6d0 pool, 0x326ba0): +0x840 act seed, +0x860 level seed base, +0x868 level list
constexpr int DrlgActSeed = 0x840, DrlgSeedBase = 0x860, DrlgLevels = 0x868;
// Level (0x2a9a810 pool): +0x0 drlg type (1 maze, 2 preset, 3 outdoor), +0x8 room count, +0x10 first room,
// +0x1b8 next level, +0x1c8 drlg, +0x1e4 seed, +0x1f8 id, +0x1fc unload timer, +0x258 saved room bits
constexpr int LevelType = 0x0, LevelRoomCount = 0x8, LevelRooms = 0x10, LevelNext = 0x1b8, LevelDrlg = 0x1c8,
              LevelSeed = 0x1e4, LevelId = 0x1f8, LevelSavedBits = 0x258;

// MOV [RSP+0x20],RBP / PUSH RSI / PUSH R14 / PUSH R15 / SUB RSP,0x20
constexpr uint8_t BuildEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20};
// MOV [RSP+0x10],RBP / MOV [RSP+0x18],RSI / PUSH RDI / SUB RSP,0x280
constexpr uint8_t GetEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                0x57, 0x48, 0x81, 0xec, 0x80, 0x02, 0x00, 0x00};

using LevelBuildFn = void (*)(uint64_t flag, uint8_t* level) noexcept;
using LevelGetFn = uint8_t* (*)(uint64_t flag, uint8_t* drlg, int32_t levelId) noexcept;
using LevelFreeFn = void (*)(uint64_t flag, uint8_t* level, int32_t keep) noexcept;
using LevelRemovableFn = uint64_t (*)(uint64_t flag, uint8_t* level) noexcept;
using VoidFn = void (*)() noexcept;
using AllocatorFn = void** (*)() noexcept;
using FreeFn = void (*)(void* allocator, void* ptr) noexcept;
using GameKeyFn = uint64_t (*)(uint16_t id) noexcept;
using GameByIdFn = uint8_t* (*)(uint64_t key) noexcept;
using ClientUnitFn = uint8_t* (*)(uint8_t* client, int32_t zero) noexcept;
using UnitRoomFn = uint8_t* (*)(uint8_t* unit) noexcept;
using RoomLevelIdFn = int32_t (*)(uint8_t* room) noexcept;

constexpr const char* PluginName = CARTO_PLUGIN_NAME;
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
LevelBuildFn g_build = nullptr;
LevelGetFn g_get = nullptr;
bool g_hooks = false;

template <typename T>
auto F(const void* p, int off) noexcept -> T& {
	return *reinterpret_cast<T*>(const_cast<uint8_t*>(static_cast<const uint8_t*>(p)) + off);
}

// --- Seed overrides -----------------------------------------------------------------------------------------------
struct Override {
	std::atomic<uint32_t> actSeed{0};
	std::atomic<int32_t> levelId{-1};
	std::atomic<uint32_t> seed{0};
	std::atomic<uint32_t> generation{0};
};
Override g_overrides[16];
SRWLOCK g_overrideLock = SRWLOCK_INIT;
std::atomic<uint32_t> g_builds{0}, g_overrideBuilds{0};
std::atomic<int32_t> g_watch{-1};  // a level whose builds log their layout ("cartographer watch <id>")

auto FindOverride(uint32_t actSeed, int32_t levelId, uint32_t* seed) noexcept -> bool {
	AcquireSRWLockShared(&g_overrideLock);
	bool found = false;
	for (const Override& o : g_overrides) {
		if (o.levelId.load() == levelId && o.actSeed.load() == actSeed) {
			*seed = o.seed.load();
			found = true;
			break;
		}
	}
	ReleaseSRWLockShared(&g_overrideLock);
	return found;
}

auto SetOverride(uint32_t actSeed, int32_t levelId, uint32_t seed) noexcept -> uint32_t {
	AcquireSRWLockExclusive(&g_overrideLock);
	Override* slot = nullptr;
	for (Override& o : g_overrides) {
		if (o.levelId.load() == levelId && o.actSeed.load() == actSeed) {
			slot = &o;
			break;
		}
	}
	for (Override& o : g_overrides) {
		if (slot == nullptr && o.levelId.load() < 0) {
			slot = &o;
		}
	}
	if (slot == nullptr) {
		slot = &g_overrides[0];
	}
	slot->actSeed = actSeed;
	slot->seed = seed;
	slot->levelId = levelId;
	const uint32_t gen = slot->generation.fetch_add(1) + 1;
	ReleaseSRWLockExclusive(&g_overrideLock);
	return gen;
}

// The seed window: while a build or get runs with an override, drlg +0x860 holds the override's base. A nested build of
// another level of the same drlg (the preset builder 0x3dd3c0 makes linked levels) must see the true base again.
struct Window {
	uint8_t* drlg;
	uint32_t trueBase;
};
thread_local Window t_windows[8];
thread_local int t_depth = 0;

auto TrueBase(uint8_t* drlg, bool* inWindow) noexcept -> uint32_t {
	for (int i = t_depth - 1; i >= 0; --i) {
		if (t_windows[i].drlg == drlg) {
			*inWindow = true;
			return t_windows[i].trueBase;
		}
	}
	*inWindow = false;
	return F<uint32_t>(drlg, DrlgSeedBase);
}

// Runs fn with drlg +0x860 set for levelId (an override's base, or the true base inside another override's window).
template <typename Fn>
void WithSeed(uint8_t* drlg, int32_t levelId, Fn fn) noexcept {
	bool inWindow = false;
	const uint32_t trueBase = TrueBase(drlg, &inWindow);
	uint32_t seed = 0;
	const bool over = FindOverride(F<uint32_t>(drlg, DrlgActSeed), levelId, &seed);
	if ((!over && !inWindow) || t_depth >= 8) {
		fn();
		return;
	}
	const uint32_t prev = F<uint32_t>(drlg, DrlgSeedBase);
	F<uint32_t>(drlg, DrlgSeedBase) = over ? seed - static_cast<uint32_t>(levelId) : trueBase;
	t_windows[t_depth++] = {drlg, trueBase};
	fn();
	--t_depth;
	F<uint32_t>(drlg, DrlgSeedBase) = prev;
}

void FreeBlock(void* p) noexcept {
	d2r::Engine<VoidFn>(rv::AllocInit)();
	void** alloc = d2r::Engine<AllocatorFn>(rv::Allocator)();
	reinterpret_cast<FreeFn>((*reinterpret_cast<void***>(alloc))[4])(alloc, p);
}

// Which layout seed each level copy was last built with (server and client copies are separate level records).
struct BuiltSeed {
	uint8_t* level;
	uint32_t seed;
};
BuiltSeed g_builtSeeds[64];
SRWLOCK g_builtLock = SRWLOCK_INIT;

// True when the level was last built with this seed; records the seed either way.
auto BitsMatch(uint8_t* level, uint32_t seed) noexcept -> bool {
	AcquireSRWLockExclusive(&g_builtLock);
	BuiltSeed* slot = nullptr;
	for (BuiltSeed& b : g_builtSeeds) {
		if (b.level == level) {
			slot = &b;
			break;
		}
	}
	for (BuiltSeed& b : g_builtSeeds) {
		if (slot == nullptr && b.level == nullptr) {
			slot = &b;
		}
	}
	if (slot == nullptr) {
		slot = &g_builtSeeds[reinterpret_cast<uintptr_t>(level) % 64];
		slot->level = nullptr;
	}
	const bool same = slot->level == level && slot->seed == seed;
	slot->level = level;
	slot->seed = seed;
	ReleaseSRWLockExclusive(&g_builtLock);
	return same;
}

void BuildHook(uint64_t flag, uint8_t* level) noexcept {
	g_builds.fetch_add(1);
	if (level == nullptr) {
		g_build(flag, level);
		return;
	}
	uint8_t* drlg = F<uint8_t*>(level, LevelDrlg);
	const int32_t id = F<int32_t>(level, LevelId);
	uint32_t seed = 0;
	if (drlg != nullptr && FindOverride(F<uint32_t>(drlg, DrlgActSeed), id, &seed)) {
		g_overrideBuilds.fetch_add(1);
		plog::Info("build level %d (drlg %p, act seed %08x) with seed %08x", id, static_cast<void*>(drlg),
		           F<uint32_t>(drlg, DrlgActSeed), seed);
		// Saved room bits belong to one layout: the native unload (0x327c40 keep 1) writes one int per room into the
		// array it made for the first layout, never resizing it. A layout with more rooms overruns it (the crash this
		// spike hit). Drop the array whenever this level's layout seed changed since the bits were saved.
		if (!BitsMatch(level, seed) && F<void*>(level, LevelSavedBits) != nullptr) {
			plog::Info("level %d: saved room bits are from another layout, dropped", id);
			FreeBlock(F<void*>(level, LevelSavedBits));
			F<void*>(level, LevelSavedBits) = nullptr;
		}
	}
	if (drlg == nullptr) {
		g_build(flag, level);
		return;
	}
	WithSeed(drlg, id, [&] { g_build(flag, level); });
	if (seed != 0 || id == g_watch.load()) {
		plog::Info("built level %d: %d rooms, level seed now %08x", id, F<int32_t>(level, LevelRoomCount),
		           F<uint32_t>(level, LevelSeed));
		// The layout, for the spike's pictures: "layout <level> <seed> <drlg>: x,y,w,h ..." in tiles, 16 rooms a line
		char line[600];
		int len = 0, n = 0;
		for (uint8_t* r = F<uint8_t*>(level, LevelRooms); r != nullptr && n < 4096; r = F<uint8_t*>(r, 0x48), ++n) {
			if (len == 0) {
				len = std::snprintf(line, sizeof(line), "layout %d %08x %p:", id, seed, static_cast<void*>(drlg));
			}
			len += std::snprintf(line + len, sizeof(line) - len, " %d,%d,%d,%d", F<int32_t>(r, 0x60), F<int32_t>(r, 0x64),
			                     F<int32_t>(r, 0x68), F<int32_t>(r, 0x6c));
			if (n % 16 == 15) {
				plog::Info("%s", line);
				len = 0;
			}
		}
		if (len > 0) {
			plog::Info("%s", line);
		}
	}
}

auto GetHook(uint64_t flag, uint8_t* drlg, int32_t levelId) noexcept -> uint8_t* {
	if (drlg == nullptr) {
		return g_get(flag, drlg, levelId);
	}
	uint8_t* level = nullptr;
	WithSeed(drlg, levelId, [&] { level = g_get(flag, drlg, levelId); });
	return level;
}

// --- Erase -------------------------------------------------------------------------------------------------------
auto FindLevel(uint8_t* drlg, int32_t levelId) noexcept -> uint8_t* {
	for (uint8_t* l = drlg != nullptr ? F<uint8_t*>(drlg, DrlgLevels) : nullptr; l != nullptr; l = F<uint8_t*>(l, LevelNext)) {
		if (F<int32_t>(l, LevelId) == levelId) {
			return l;
		}
	}
	return nullptr;
}

auto CountRooms(uint8_t* level) noexcept -> int {
	int n = 0;
	for (uint8_t* r = F<uint8_t*>(level, LevelRooms); r != nullptr && n < 100000; r = F<uint8_t*>(r, 0x48)) {
		++n;
	}
	return n;
}

// Frees a built level as the native past-level unload does, then drops its saved room bits. The bits go even when the
// level is not built: the native unload (keep 1) left them, and the next build would apply them by room index to the
// new rooms (rooms marked populated stay empty; more rooms than before would read past the array).
// Returns 0 freed, 1 not built (nothing to free), -1 in use.
auto EraseLevel(uint64_t flag, uint8_t* level) noexcept -> int {
	int result = 1;
	if (F<uint8_t*>(level, LevelRooms) != nullptr) {
		if (d2r::Engine<LevelRemovableFn>(rv::LevelRemovable)(flag, level) == 0) {
			return -1;
		}
		d2r::Engine<LevelFreeFn>(rv::LevelFree)(flag, level, 1);
		result = 0;
	}
	if (F<void*>(level, LevelSavedBits) != nullptr) {
		plog::Info("level %d: dropping %d saved room bits", F<int32_t>(level, LevelId), F<int32_t>(level, LevelRoomCount));
	}
	if (F<void*>(level, LevelSavedBits) != nullptr) {
		FreeBlock(F<void*>(level, LevelSavedBits));
		F<void*>(level, LevelSavedBits) = nullptr;
	}
	return result;
}

// --- Inactive units (D2Game\src\Unit\SUnitInactive.cpp) ------------------------------------------------------------
// When a server room deactivates, D2Game saves its monsters, objects and items in a per-act list keyed by the room's
// coordinates, and 0x503790 / 0x505d70 restore them when a room at the same coordinates activates again. A new layout
// can put a room at old coordinates, so the old level's entries must go. List: game +0x1b0 (per-act heads, count
// +0x1b8), sorted by x descending. Node (0x28): +0 x, +4 y, +8 list (next +0), +0x10 monsters (next +0xd0; +0x20 list
// freed by 0x4a5240, +0x28 by 0x3dbaf0), +0x18 units (next +0x30), +0x20 next. 0x503e10 frees them all at game end.
constexpr int GameInactive = 0x1b0, GameInactiveCount = 0x1b8;
constexpr uint64_t InactiveListFree = 0x4a5240;  // (game, list)
constexpr uint64_t InactivePathFree = 0x3dbaf0;  // (block)
using ListFreeFn = void (*)(uint8_t* game, void* list) noexcept;
using BlockFreeFn = void (*)(void* block) noexcept;
// RoomEx: +0x48 next, +0x60 tile x, +0x64 tile y, +0x68 w, +0x6c h, +0x70 status byte, +0x90 level
constexpr int RoomNext = 0x48, RoomX = 0x60, RoomY = 0x64;

void FreeInactiveNode(uint8_t* game, uint8_t* node) noexcept {
	for (uint8_t* u = F<uint8_t*>(node, 0x18); u != nullptr;) {
		uint8_t* next = F<uint8_t*>(u, 0x30);
		FreeBlock(u);
		u = next;
	}
	for (uint8_t* u = F<uint8_t*>(node, 0x8); u != nullptr;) {
		uint8_t* next = F<uint8_t*>(u, 0);
		FreeBlock(u);
		u = next;
	}
	for (uint8_t* m = F<uint8_t*>(node, 0x10); m != nullptr;) {
		uint8_t* next = F<uint8_t*>(m, 0xd0);
		d2r::Engine<ListFreeFn>(InactiveListFree)(game, F<void*>(m, 0x20));
		if (F<void*>(m, 0x28) != nullptr) {
			d2r::Engine<BlockFreeFn>(InactivePathFree)(F<void*>(m, 0x28));
		}
		FreeBlock(m);
		m = next;
	}
	FreeBlock(node);
}

struct RoomKey {
	int32_t x, y;
};
RoomKey g_keys[4096];

// Collects the level's room keys (call before the rooms are freed).
auto CollectKeys(uint8_t* level) noexcept -> int {
	int n = 0;
	for (uint8_t* r = F<uint8_t*>(level, LevelRooms); r != nullptr && n < 4096; r = F<uint8_t*>(r, RoomNext)) {
		g_keys[n++] = {F<int32_t>(r, RoomX), F<int32_t>(r, RoomY)};
	}
	return n;
}

// Drops the act's inactive entries whose key is one of the level's rooms. Returns how many; *others counts the act's
// remaining entries (for the log).
// Level rectangle in tiles: +0x24 x, +0x28 y, +0x2c w, +0x30 h (set when the act lays out its levels; kept by unloads).
struct Rect {
	int32_t x, y, w, h;
};
auto LevelRect(uint8_t* level) noexcept -> Rect {
	return {F<int32_t>(level, 0x24), F<int32_t>(level, 0x28), F<int32_t>(level, 0x2c), F<int32_t>(level, 0x30)};
}
Rect g_rect{};

auto PurgeInactive(uint8_t* game, uint64_t act, int keys, int* others) noexcept -> int {
	*others = 0;
	if (act >= F<uint64_t>(game, GameInactiveCount)) {
		return 0;
	}
	uint8_t** head = &F<uint8_t**>(game, GameInactive)[act];
	int purged = 0;
	for (uint8_t** link = head; *link != nullptr;) {
		uint8_t* node = *link;
		const int32_t x = F<int32_t>(node, 0), y = F<int32_t>(node, 4);
		// Keyed by the room's subtile corner (0x2efb40 reads the active room's +0x80): the drlg room's tile corner x 5
		bool match = false;
		for (int i = 0; i < keys && !match; ++i) {
			match = g_keys[i].x * 5 == x && g_keys[i].y * 5 == y;
		}
		// Also every entry inside the level's rectangle: once the level was unloaded natively its rooms (keys) are gone
		const Rect& rc = g_rect;
		const bool inRect = rc.w > 0 && rc.h > 0 && rc.w < 4000 && rc.h < 4000 && x >= rc.x * 5 && x < (rc.x + rc.w) * 5 &&
		                    y >= rc.y * 5 && y < (rc.y + rc.h) * 5;
		if (inRect && !match) {
			plog::Info("inactive entry x %d y %d: inside the level rectangle, not a current room", x, y);
		}
		match = match || inRect;
		if (match) {
			*link = F<uint8_t*>(node, 0x20);
			FreeInactiveNode(game, node);
			++purged;
		} else {
			if (*others < 6) {
				plog::Info("inactive entry kept: act %llu x %d y %d", static_cast<unsigned long long>(act), x, y);
			}
			++*others;
			link = &F<uint8_t*>(node, 0x20);
		}
	}
	return purged;
}

// --- The command's steps ------------------------------------------------------------------------------------------
struct Job {
	int32_t levelId;
	uint32_t seed;
	char message[600];
	bool ok;
};
Job g_job{};
std::atomic<bool> g_busy{false};

void Say(const char* text) noexcept {
	plog::Info("%s", text);
	if (g_context != nullptr) {
		g_context->WriteConsoleMessage(text);
	}
}

auto ClientFlag() noexcept -> uint64_t {
	return *reinterpret_cast<const uint8_t*>(d2r::imageBase + rv::ClientFlagBase + rv::ClientFlagOffset);
}
auto ClientDrlg() noexcept -> uint8_t* {
	uint8_t* act = *reinterpret_cast<uint8_t**>(d2r::imageBase + rv::ClientAct);
	return act != nullptr ? F<uint8_t*>(act, ActDrlg) : nullptr;
}

// Step 3, UI thread: free the local client's copy, then report.
void ClientFree(const D2RL::PluginContext*, void*) noexcept {
	char line[700];
	if (g_job.ok) {
		uint8_t* drlg = ClientDrlg();
		uint8_t* level = FindLevel(drlg, g_job.levelId);
		const char* client = "client: no copy";
		if (level != nullptr) {
			const int rooms = CountRooms(level);
			const int r = EraseLevel(ClientFlag(), level);
			client = r == 0 ? "client: freed its copy" : r == 1 ? "client: copy not built" : "client: copy IN USE, not freed (desync risk)";
			std::snprintf(line, sizeof(line), "%s (%d rooms, act seed %08x)", client, rooms, F<uint32_t>(drlg, DrlgActSeed));
			Say(line);
		} else {
			Say(client);
		}
	}
	Say(g_job.message);
	g_busy = false;
}

// Step 2, game thread: refuse if a player is in the level; else free the server's copy and set the new seed.
void ServerRegen(const D2RL::PluginContext* context, void*) noexcept {
	g_job.ok = false;
	uint8_t* game = nullptr;
	for (uint16_t id = 1; id <= 0x400 && game == nullptr; ++id) {
		const uint64_t key = d2r::Engine<GameKeyFn>(rv::GameKey)(id);
		if (key != 0) {
			game = d2r::Engine<GameByIdFn>(rv::GameById)(key);
		}
	}
	if (game == nullptr) {
		std::snprintf(g_job.message, sizeof(g_job.message), "regen %d: no server game", g_job.levelId);
	} else {
		const uint64_t flag = F<uint8_t>(game, GameDataFlag);
		int players = 0, inside = 0;
		for (uint8_t* c = F<uint8_t*>(game, GameClients); c != nullptr && players < 64; c = F<uint8_t*>(c, ClientNext)) {
			uint8_t* unit = d2r::Engine<ClientUnitFn>(rv::ClientUnit)(c, 0);
			uint8_t* room = unit != nullptr ? d2r::Engine<UnitRoomFn>(rv::UnitRoom)(unit) : nullptr;
			++players;
			if (room != nullptr && d2r::Engine<RoomLevelIdFn>(rv::RoomLevelId)(room) == g_job.levelId) {
				++inside;
			}
		}
		uint8_t* level = nullptr;
		uint8_t* drlg = nullptr;
		void** acts = F<void**>(game, GameActs);
		const uint64_t actCount = F<uint64_t>(game, GameActCount);
		uint64_t actIndex = 0;
		for (uint64_t a = 0; a < actCount && level == nullptr && acts != nullptr; ++a) {
			uint8_t* act = static_cast<uint8_t*>(acts[a]);
			drlg = act != nullptr ? F<uint8_t*>(act, ActDrlg) : nullptr;
			level = FindLevel(drlg, g_job.levelId);
			actIndex = a;
		}
		if (inside > 0) {
			std::snprintf(g_job.message, sizeof(g_job.message), "regen %d refused: %d player(s) inside", g_job.levelId, inside);
		} else if (level == nullptr) {
			// Never made in this game: its first build takes the override; the act is found when it is made.
			std::snprintf(g_job.message, sizeof(g_job.message), "regen %d: level not made yet in this game", g_job.levelId);
		} else {
			const int rooms = CountRooms(level);
			const uint32_t oldSeed = F<uint32_t>(level, LevelSeed);
			const int keys = CollectKeys(level);
			g_rect = LevelRect(level);
			plog::Info("level %d rectangle: x %d y %d w %d h %d (tiles)", g_job.levelId, g_rect.x, g_rect.y, g_rect.w, g_rect.h);
			if (keys > 0) {
				plog::Info("level %d room keys: first (%d,%d) last (%d,%d), %d rooms", g_job.levelId, g_keys[0].x, g_keys[0].y,
				           g_keys[keys - 1].x, g_keys[keys - 1].y, keys);
			}
			const int r = EraseLevel(flag, level);
			int purged = 0, kept = 0;
			if (r >= 0) {
				purged = PurgeInactive(game, actIndex, keys, &kept);
				plog::Info("inactive entries: %d purged, %d kept in act %llu", purged, kept,
				           static_cast<unsigned long long>(actIndex));
			}
			if (r < 0) {
				std::snprintf(g_job.message, sizeof(g_job.message),
				              "regen %d refused: the level is still in use (a client is near or a room is held)", g_job.levelId);
			} else {
				const uint32_t gen = SetOverride(F<uint32_t>(drlg, DrlgActSeed), g_job.levelId, g_job.seed);
				g_job.ok = true;
				std::snprintf(g_job.message, sizeof(g_job.message),
				              "regen %d #%u: server %s (%d rooms, type %d, old level seed %08x), %d saved room entries purged "
				              "(%d others kept), new seed %08x, act seed %08x, %d player(s) in game",
				              g_job.levelId, gen, r == 0 ? "freed" : "not built", rooms, F<int32_t>(level, LevelType), oldSeed,
				              purged, kept, g_job.seed, F<uint32_t>(drlg, DrlgActSeed), players);
			}
		}
	}
	plog::Info("%s", g_job.message);
	if (g_threads->runOnUiThread(context, ClientFree, nullptr) != D2RL::Threads::Result::Success) {
		g_busy = false;
	}
}

// Step 1, UI thread: refuse if the local client still caches the level's rooms.
void ClientCheck(const D2RL::PluginContext* context, void*) noexcept {
	uint8_t* drlg = ClientDrlg();
	uint8_t* level = FindLevel(drlg, g_job.levelId);
	if (level != nullptr && F<uint8_t*>(level, LevelRooms) != nullptr &&
	    d2r::Engine<LevelRemovableFn>(rv::LevelRemovable)(ClientFlag(), level) == 0) {
		char line[200];
		std::snprintf(line, sizeof(line), "regen %d refused: the client still has rooms of it in use", g_job.levelId);
		Say(line);
		g_busy = false;
		return;
	}
	if (g_threads->runOnGameThread(context, ServerRegen, nullptr) != D2RL::Threads::Result::Success) {
		Say("regen: no local game thread (a remote client cannot regenerate)");
		g_busy = false;
	}
}

// Diagnostic: room status histogram (+0x70 byte) and held rooms (+0x50 bit 0x100000) of a level copy.
void DescribeRooms(const char* who, uint8_t* drlg, int32_t levelId) noexcept {
	char line[400];
	uint8_t* level = FindLevel(drlg, levelId);
	if (level == nullptr) {
		std::snprintf(line, sizeof(line), "%s: level %d not made", who, levelId);
		Say(line);
		return;
	}
	int status[8] = {}, held = 0, n = 0;
	for (uint8_t* r = F<uint8_t*>(level, LevelRooms); r != nullptr && n < 4096; r = F<uint8_t*>(r, RoomNext), ++n) {
		++status[F<uint8_t>(r, 0x70) & 7];
		held += (F<uint32_t>(r, 0x50) & 0x100000) != 0;
	}
	std::snprintf(line, sizeof(line), "%s: level %d %d rooms, status 0..4: %d %d %d %d %d, held %d, unload timer %d, bits %p",
	              who, levelId, n, status[0], status[1], status[2], status[3], status[4], held, F<int32_t>(level, 0x1fc),
	              F<void*>(level, LevelSavedBits));
	Say(line);
}

void ServerRooms(const D2RL::PluginContext*, void* arg) noexcept {
	const int32_t levelId = static_cast<int32_t>(reinterpret_cast<intptr_t>(arg));
	for (uint16_t id = 1; id <= 0x400; ++id) {
		const uint64_t key = d2r::Engine<GameKeyFn>(rv::GameKey)(id);
		uint8_t* game = key != 0 ? d2r::Engine<GameByIdFn>(rv::GameById)(key) : nullptr;
		if (game == nullptr) {
			continue;
		}
		void** acts = F<void**>(game, GameActs);
		for (uint64_t a = 0; a < F<uint64_t>(game, GameActCount) && acts != nullptr; ++a) {
			uint8_t* act = static_cast<uint8_t*>(acts[a]);
			if (act != nullptr && FindLevel(F<uint8_t*>(act, ActDrlg), levelId) != nullptr) {
				DescribeRooms("server", F<uint8_t*>(act, ActDrlg), levelId);
			}
		}
		return;
	}
}

void ClientRooms(const D2RL::PluginContext* context, void* arg) noexcept {
	DescribeRooms("client", ClientDrlg(), static_cast<int32_t>(reinterpret_cast<intptr_t>(arg)));
	g_threads->runOnGameThread(context, ServerRooms, arg);
}

auto Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	const char* args = command->args != nullptr ? command->args : "";
	while (*args == ' ') {
		++args;
	}
	char line[300];
	if (std::strncmp(args, "rooms", 5) == 0 && g_threads != nullptr) {
		g_threads->runOnUiThread(g_context, ClientRooms, reinterpret_cast<void*>(static_cast<intptr_t>(std::atoi(args + 5))));
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (std::strncmp(args, "watch", 5) == 0) {
		g_watch = std::atoi(args + 5);
		std::snprintf(line, sizeof(line), "watching level %d's builds", g_watch.load());
		Say(line);
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (std::strncmp(args, "regen", 5) == 0) {
		if (!g_hooks || g_threads == nullptr) {
			Say("regen: hooks or thread service not installed");
			return D2RL::ConsoleCommandResult::Handled;
		}
		const int levelId = std::atoi(args + 5);
		if (levelId <= 0 || levelId > 255) {
			Say("usage: cartographer regen <levelId>");
			return D2RL::ConsoleCommandResult::Handled;
		}
		bool expected = false;
		if (!g_busy.compare_exchange_strong(expected, true)) {
			Say("regen: busy");
			return D2RL::ConsoleCommandResult::Handled;
		}
		g_job.levelId = levelId;
		g_job.seed = static_cast<uint32_t>(__rdtsc() * 0x6ac690c5ULL >> 7) | 1;
		if (g_threads->runOnUiThread(g_context, ClientCheck, nullptr) != D2RL::Threads::Result::Success) {
			g_busy = false;
			Say("regen: could not queue the UI step");
		}
		return D2RL::ConsoleCommandResult::Handled;
	}
	std::snprintf(line, sizeof(line), "%s %s: hooks %s, builds %u (%u with a new seed)", PluginName, CARTO_PLUGIN_VERSION,
	              g_hooks ? "on" : "off", g_builds.load(), g_overrideBuilds.load());
	Say(line);
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
	.infoSize = D2RL::PluginInfoSize,
	.abiVersion = D2RL_PLUGIN_ABI_VERSION,
	.id = CARTO_PLUGIN_NAME,
	.name = CARTO_PLUGIN_NAME,
	.version = CARTO_PLUGIN_VERSION,
	.author = "pd2-resurrected",
	.description = "Cartographer spike: rebuild a level with a new seed in a running game.",
	.flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;
	plog::Init(context, PluginName);
	crash_guard::Install(CARTO_PLUGIN_VERSION, context->exeBase);
	if (!d2r::Bind(context->exeBase)) {
		plog::Warn("not the expected game image: nothing installed");
	} else {
		if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
		    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
			g_threads = nullptr;
			plog::Warn("no thread service");
		}
		// Native hooks: the SDK has no level/DRLG service. Each checks its entry bytes and refuses on a mismatch.
		const bool build = context->InstallInlineHook(rv::LevelBuild, BuildEntry, sizeof(BuildEntry), BuildHook, &g_build);
		const bool get = build && context->InstallInlineHook(rv::LevelGetOrMake, GetEntry, sizeof(GetEntry), GetHook, &g_get);
		g_hooks = build && get;
		plog::Info("level build hook %s, level get hook %s", build ? "on" : "refused", get ? "on" : "refused");
	}
	return context->RegisterConsoleCommand("cartographer", Command, "Cartographer spike: status, or regen <levelId>.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
