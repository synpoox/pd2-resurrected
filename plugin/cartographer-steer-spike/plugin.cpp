// cartographer-steer-spike: throwaway prototype for the Cartographer DRLG steering spike (#928,
// docs/research/drlg-steering.md). NOT for merge. Standalone: links only d2rAI-toolkit's plugin_common and the SDK.
//
// For ONE maze level (config `level=`), it
//   * overrides the level's tileset (level +0x1e0) and points the level at a private LvlMaze copy (level +0x18),
//     taken from another level's row (`mazefrom=`), before D2R builds it (hook on the level build 0x3271c0);
//   * replaces the maze room walk (0x3e96d0) with a hand-written straight graph: `west=` rooms west of the first
//     room, `east=` rooms east of it, each placed by D2R's own room placer (0x3e77a0) and given its piece by D2R's
//     exit-mask piece picker (0x3eb960);
//   * makes the entrance/exit placer (0x3eb850) use the rotation of its table group that matches an existing dead
//     end, so the special rooms land on the ends of the line instead of growing a branch;
//   * logs every room (piece, rect) after the build, and an ASCII picture of the room grid.
// Config: <game>\d2rloader\config\cartographer-steer-spike.txt, `key=value` lines. No file: nothing is hooked.
// Native hooks: the SDK has no level, DRLG or room service (README, D2RLoader 1.3.1), so these four D2Common
// functions are hooked directly, each byte-checked at its entry.
#include <D2RLPlugin/api.h>

#include "build_guard.h"
#include "crash_guard.h"
#include "d2r.h"
#include "log.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* PluginName = "cartographer-steer-spike";

// --- 93847 addresses (docs/research/drlg-steering.md) ---------------------------------------------------------
constexpr uint64_t LevelBuildRva = 0x3271c0;   // (dataFlag, level): builds the level's rooms by its DRLG type
constexpr uint64_t MazeAddRoomsRva = 0x3e96d0; // (level): the maze random walk, until Rooms[difficulty] rooms
constexpr uint64_t MazeSpecialRva = 0x3eb850;  // (level, int[4] {from, to, file, dir}, int* rotation)
constexpr uint64_t MazeAddRoomRva = 0x3e77a0;  // (room, dir 0 W 1 N 2 E 3 S, merge) -> new room or null
constexpr uint64_t MazePickPieceRva = 0x3eb960; // (room, clearFixed): piece = tileset base + exit mask
constexpr uint64_t LvlMazeRecordRva = 0x32c6a0; // (dataFlag, level id) -> LvlMaze row {Level, Rooms[3], SizeX, SizeY, Merge}

// MOV [RSP+0x20],RBP / PUSH RSI / PUSH R14 / PUSH R15 / SUB RSP,0x20
constexpr uint8_t LevelBuildEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20};
// MOV [RSP+0x20],RDI / PUSH R14 / SUB RSP,0x20 / MOV R8,[RCX+0x1c8]
constexpr uint8_t MazeAddRoomsEntry[] = {0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20,
                                         0x4c, 0x8b, 0x81, 0xc8, 0x01, 0x00, 0x00};
// PUSH R14 / SUB RSP,0x30 / MOV R9D,[RDX] / MOV R14,R8 / MOV [RSP+0x40],RBX
constexpr uint8_t MazeSpecialEntry[] = {0x41, 0x56, 0x48, 0x83, 0xec, 0x30, 0x44, 0x8b, 0x0a, 0x4d, 0x8b, 0xf0,
                                        0x48, 0x89, 0x5c, 0x24, 0x40};

// --- Level and room fields -------------------------------------------------------------------------------------
constexpr int LevelDrlgType = 0x00, LevelRoomCount = 0x08, LevelFirstRoom = 0x10, LevelTypeData = 0x18;
constexpr int LevelX = 0x24, LevelY = 0x28, LevelW = 0x2c, LevelH = 0x30;
constexpr int LevelDrlg = 0x1c8, LevelTileset = 0x1e0, LevelId = 0x1f8;
constexpr int DrlgFlags = 0x110;  // bit 0: the client's DRLG
constexpr int RoomPiece = 0x40, RoomNext = 0x48, RoomX = 0x60, RoomY = 0x64, RoomW = 0x68, RoomH = 0x6c;
constexpr int PieceFlags = 0x10;  // u64; bit 1 (2): fixed, no longer changed by the piece picker

struct Config {
	int level = -1;
	int tileset = 0;
	int mazeFrom = 0;
	int west = 3;
	int east = 3;
	bool rotateSpecials = true;
};
Config g_config;
int32_t g_maze[7] = {};  // private LvlMaze row
bool g_haveMaze = false;
char g_status[300] = "off: no config";
int g_builds = 0;

using LevelBuildFn = void (*)(uint8_t, void*) noexcept;
using MazeAddRoomsFn = void (*)(void*) noexcept;
using MazeSpecialFn = void (*)(void*, int32_t*, uint32_t*) noexcept;
using MazeAddRoomFn = void* (*)(void*, int32_t, int32_t) noexcept;
using MazePickPieceFn = void* (*)(void*, int32_t) noexcept;
using LvlMazeRecordFn = const int32_t* (*)(uint8_t, int32_t) noexcept;
LevelBuildFn g_levelBuild = nullptr;
MazeAddRoomsFn g_addRooms = nullptr;
MazeSpecialFn g_special = nullptr;

template <typename T>
auto F(void* p, int off) noexcept -> T& {
	return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + off);
}

auto IsTarget(void* level) noexcept -> bool {
	return level != nullptr && F<int32_t>(level, LevelId) == g_config.level;
}

auto Side(void* level) noexcept -> const char* {
	void* drlg = F<void*>(level, LevelDrlg);
	return drlg != nullptr && (F<uint32_t>(drlg, DrlgFlags) & 1) != 0 ? "client" : "server";
}

void LogRooms(void* level) noexcept {
	const int lx = F<int32_t>(level, LevelX), ly = F<int32_t>(level, LevelY);
	plog::Info("[%s] level %d tileset %d: %d rooms, level rect (%d,%d %dx%d)", Side(level), F<int32_t>(level, LevelId),
	           F<int32_t>(level, LevelTileset), F<int32_t>(level, LevelRoomCount), lx, ly, F<int32_t>(level, LevelW),
	           F<int32_t>(level, LevelH));
	int minX = 1 << 30, minY = 1 << 30, cell = 0;
	for (void* r = F<void*>(level, LevelFirstRoom); r != nullptr; r = F<void*>(r, RoomNext)) {
		const int32_t* piece = F<int32_t*>(r, RoomPiece);
		plog::Info("  room piece %d file %d at (%d,%d) %dx%d", piece != nullptr ? piece[0] : -1,
		           piece != nullptr ? piece[1] : -1, F<int32_t>(r, RoomX) - lx, F<int32_t>(r, RoomY) - ly,
		           F<int32_t>(r, RoomW), F<int32_t>(r, RoomH));
		if (F<int32_t>(r, RoomX) < minX) minX = F<int32_t>(r, RoomX);
		if (F<int32_t>(r, RoomY) < minY) minY = F<int32_t>(r, RoomY);
		if (cell == 0 || F<int32_t>(r, RoomW) < cell) cell = F<int32_t>(r, RoomW);
	}
	if (cell <= 0) return;
	char grid[16][33];
	std::memset(grid, '.', sizeof(grid));
	for (auto& row : grid) row[32] = '\0';
	for (void* r = F<void*>(level, LevelFirstRoom); r != nullptr; r = F<void*>(r, RoomNext)) {
		const int gx = (F<int32_t>(r, RoomX) - minX) / cell, gy = (F<int32_t>(r, RoomY) - minY) / cell;
		if (gx >= 0 && gx < 32 && gy >= 0 && gy < 16) grid[gy][gx] = '#';
	}
	for (int y = 0; y < 16; y++) {
		if (std::strchr(grid[y], '#') != nullptr) plog::Info("  grid |%s|", grid[y]);
	}
}

void LevelBuildHook(uint8_t dataFlag, void* level) noexcept {
	if (IsTarget(level) && F<int32_t>(level, LevelDrlgType) == 1) {
		if (g_config.tileset > 0) F<int32_t>(level, LevelTileset) = g_config.tileset;
		if (g_config.mazeFrom > 0) {
			if (!g_haveMaze) {
				const int32_t* row = d2r::Engine<LvlMazeRecordFn>(LvlMazeRecordRva)(dataFlag, g_config.mazeFrom);
				if (row != nullptr && row[0] == g_config.mazeFrom) {
					std::memcpy(g_maze, row, sizeof(g_maze));
					g_maze[0] = g_config.level;
					g_haveMaze = true;
				}
			}
			if (g_haveMaze) F<void*>(level, LevelTypeData) = g_maze;
		}
		plog::Info("[%s] build level %d: tileset %d, maze row size %dx%d merge %d", Side(level), g_config.level,
		           F<int32_t>(level, LevelTileset), g_haveMaze ? g_maze[4] : -1, g_haveMaze ? g_maze[5] : -1,
		           g_haveMaze ? g_maze[6] : -1);
	}
	g_levelBuild(dataFlag, level);
	if (IsTarget(level)) {
		g_builds++;
		LogRooms(level);
	}
}

void AddRoomsHook(void* level) noexcept {
	if (!IsTarget(level)) {
		g_addRooms(level);
		return;
	}
	auto add = d2r::Engine<MazeAddRoomFn>(MazeAddRoomRva);
	auto pick = d2r::Engine<MazePickPieceFn>(MazePickPieceRva);
	void* first = F<void*>(level, LevelFirstRoom);
	int placed = 0;
	const int counts[2] = {g_config.west, g_config.east};
	const int dirs[2] = {0, 2};
	for (int side = 0; side < 2; side++) {
		void* cur = first;
		for (int i = 0; i < counts[side] && cur != nullptr; i++) {
			void* next = add(cur, dirs[side], 0);  // merge off: a straight path has no loops
			if (next == nullptr) {
				plog::Warn("[%s] room %d %s did not fit", Side(level), i + 1, side == 0 ? "west" : "east");
				break;
			}
			pick(cur, 1);
			pick(next, 1);
			cur = next;
			placed++;
		}
	}
	plog::Info("[%s] guided walk placed %d rooms (graph: %d west, %d east)", Side(level), placed, g_config.west,
	           g_config.east);
}

auto FreeRoomWithPiece(void* level, int32_t piece) noexcept -> bool {
	for (void* r = F<void*>(level, LevelFirstRoom); r != nullptr; r = F<void*>(r, RoomNext)) {
		const int32_t* p = F<int32_t*>(r, RoomPiece);
		if (p != nullptr && (F<uint64_t>(const_cast<int32_t*>(p), PieceFlags) & 2) == 0 && p[0] == piece) return true;
	}
	return false;
}

void SpecialHook(void* level, int32_t* entry, uint32_t* rotation) noexcept {
	if (IsTarget(level) && g_config.rotateSpecials && rotation != nullptr && entry != nullptr &&
	    !FreeRoomWithPiece(level, entry[0])) {
		// Callers index a group of four rotations (stride 4 ints) as group + rotation; try the other three.
		int32_t* group = entry - static_cast<int32_t>(*rotation & 3) * 4;
		for (uint32_t k = 1; k < 4; k++) {
			int32_t* other = group + ((*rotation + k) & 3) * 4;
			if (FreeRoomWithPiece(level, other[0])) {
				plog::Info("[%s] special %d -> %d: rotation %u instead of %u", Side(level), other[0], other[1],
				           (*rotation + k) & 3, *rotation & 3);
				*rotation = (*rotation + k) & 3;
				g_special(level, other, rotation);
				return;
			}
		}
	}
	g_special(level, entry, rotation);
}

auto ReadConfig(const wchar_t* dir) noexcept -> bool {
	if (dir == nullptr) return false;
	wchar_t path[600];
	std::swprintf(path, 600, L"%ls\\%hs.txt", dir, PluginName);
	FILE* f = _wfopen(path, L"r");
	if (f == nullptr) return false;
	char line[200];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		int v = 0;
		if (std::sscanf(line, "level=%d", &v) == 1) g_config.level = v;
		else if (std::sscanf(line, "tileset=%d", &v) == 1) g_config.tileset = v;
		else if (std::sscanf(line, "mazefrom=%d", &v) == 1) g_config.mazeFrom = v;
		else if (std::sscanf(line, "west=%d", &v) == 1) g_config.west = v;
		else if (std::sscanf(line, "east=%d", &v) == 1) g_config.east = v;
		else if (std::sscanf(line, "rotate=%d", &v) == 1) g_config.rotateSpecials = v != 0;
	}
	std::fclose(f);
	return g_config.level >= 0;
}

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
	-> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
	char line[400];
	std::snprintf(line, sizeof(line), "%s: %s | level %d tileset %d mazefrom %d west %d east %d | builds %d",
	              PluginName, g_status, g_config.level, g_config.tileset, g_config.mazeFrom, g_config.west,
	              g_config.east, g_builds);
	command->plugin->WriteConsoleMessage(line);
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "cartographer-steer-spike",
	.name        = "Cartographer DRLG steering spike",
	.version     = "0.0.1",
	.author      = "Synpoo",
	.description = "Throwaway prototype: builds one maze level on a straight room graph with an overridden tileset.",
	.flags       = D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) return false;
	plog::Init(context, PluginName);
	crash_guard::Install("0.0.1", context->exeBase);
	if (!d2r::Bind(context->exeBase)) {
		std::snprintf(g_status, sizeof(g_status), "off: unknown game image");
	} else if (!ReadConfig(context->pluginConfigPath)) {
		std::snprintf(g_status, sizeof(g_status), "off: no %s.txt with level=", PluginName);
	} else {
		const bool a = context->InstallInlineHook(LevelBuildRva, LevelBuildEntry, sizeof(LevelBuildEntry), LevelBuildHook,
		                                          &g_levelBuild);
		const bool b = a && context->InstallInlineHook(MazeAddRoomsRva, MazeAddRoomsEntry, sizeof(MazeAddRoomsEntry),
		                                               AddRoomsHook, &g_addRooms);
		const bool c = b && context->InstallInlineHook(MazeSpecialRva, MazeSpecialEntry, sizeof(MazeSpecialEntry),
		                                               SpecialHook, &g_special);
		std::snprintf(g_status, sizeof(g_status), "%s (build %d, walk %d, specials %d)", c ? "on" : "FAILED", a, b, c);
	}
	plog::Info("%s", g_status);
	return context->RegisterConsoleCommand(PluginName, StatusCommand, "Cartographer steering spike: status.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
}
