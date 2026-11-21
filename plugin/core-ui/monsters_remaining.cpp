// Monsters remaining (#1534, #1656). PD2 shows "Monsters Remaining: N" in its screen info: its server (ProjectDiablo.dll
// 0x103453f0, every 12 frames) sends packet 0xAD (flag, count) with the same ledger, spawned - killed, and BH.dll draws
// it (docs/research/monsters-remaining.md). Here the line is the last row of D2R's own zone info block.
//
// The count is the one D2R keeps: MonsterRegion.cpp holds, per level, the counted hostile monsters spawned and killed
// (*(game + 0x1d0)[level] -> +0x2cc and +0x2d0; monstats neverCount and non-hostile units are left out by D2R itself).
// remaining = spawned - killed. D2R makes a room's monsters when the room first activates (room +0x54 bit 0, after the
// populator), so the count only covers the rooms met: like PD2, it is taken as final once 90% of the level's rooms have
// made their monsters (mc::Explored); before that the line reads "N+". Over 25 it reads "25+" (mc::Text). The rooms
// are the level's drlg room list (level +0x10, next +0x48); a room counts as populated when its room (+0x58, made once
// the room is near) has +0x54 bit 0, and is remembered, as D2R frees rooms the player left. Nothing is forced to
// populate (PD2 forces nothing either). A level whose ledger counted no monster (a town) shows no line.
//
// Native, once: no SDK service gives a plugin the game region array (LifecycleService has the death event but no
// counts), so one hook on the ledger "spawned" call (build 93847 0x509520 (regions, room, unit, skip), found by
// signature) copies `regions` and the spawning room's drlg level, which every spawn passes. The hook calls the original
// untouched. The ledger and the rooms are server data: they are read on the game thread (ThreadService::runOnGameThread,
// about every 250 ms) and published in one atomic the zone info hook reads. A TCP/IP client has no game thread: it
// shows nothing. GameLeft and GameJoined forget the region pointer (a new game has a new array).
// Drawing: no SDK service adds a zone info row, so a second hook, by signature, wraps D2R's zone info builder (build
// 93847 0x14751e0 (panel): it rebuilds the panel's newline-separated text, area / difficulty / game type ..., at
// panel +0x198, and the terror-zone lines at +0x1e8). After the original, the hook appends "\nMonsters Remaining: N"
// with D2R's own string append (0x1732d0 (string, text, length)), so D2R draws it as one more row of the block: same
// font, colour and right alignment, hidden with the automap (TAB), the lines after the block below it.
//
// shortcut: single player and the host only.
#include "monsters_remaining.h"

#include "hot_log.h"
#include "log.h"
#include "monster_count.h"  // common/: the ledger read and its text
#include "sig.h"
#include "sig_hook.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include <windows.h>

namespace monsters_remaining {

namespace {

namespace mc = pd2::monstercount;

constexpr size_t RegionSpawned = 0x2cc, RegionKilled = 0x2d0;  // MonsterRegion: counted hostile monsters spawned, killed
// Room layout, build 93847 (the same offsets core-events/map_events.cpp reads): room +0x18 drlg room, drlg room +0x90
// its level, level +0x1f8 id and +0x10 first drlg room, drlg room +0x48 next, +0x58 the room made for it (+0x50 flag
// 0x100000 once made), room +0x54 bit 0 set after the populator ran.
constexpr size_t RoomDrlg = 0x18, DrlgLevel = 0x90, LevelId = 0x1f8, LevelFirstRoom = 0x10, DrlgNext = 0x48, DrlgFlags = 0x50,
                 DrlgRoom = 0x58, RoomFlags = 0x54;
constexpr uint32_t DrlgRoomMade = 0x100000;
constexpr uint64_t RefreshMs = 250;


const D2RL::ThreadService* g_threads = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
std::atomic<const uint8_t* const*> g_regions {nullptr};    // *(game + 0x1d0): MonsterRegion*[level id]
std::atomic<const uint8_t*> g_drlgLevel[mc::MaxLevel] {};  // by level id: the drlg level a spawn came from
std::atomic<int32_t> g_level {-1};
std::atomic<uint32_t> g_epoch {0};  // bumped by GameLeft / GameJoined: the game thread drops what it remembers
std::atomic<uint32_t> g_spawns {0}, g_changes {0};
std::atomic<uint64_t> g_text {0};         // published: see Pack
uint64_t g_lastQueued = 0;                // UI thread only

// The published line: valid | complete | level | remaining, one word so the overlay never reads half an update
constexpr auto Pack(int32_t level, int32_t remaining, bool complete) noexcept -> uint64_t {
	return 1ull << 63 | (complete ? 1ull << 62 : 0) | static_cast<uint64_t>(level & 0xffff) << 32 | static_cast<uint32_t>(remaining);
}

// (regions, room, unit, skip): ++spawned unless skip. The first 15 bytes: push rbx / sub rsp,0x20 / mov rax,r8 /
// mov rbx,rcx / test r9d,r9d.
using SpawnedFn = void (*)(const uint8_t* const*, void*, void*, int) noexcept;
SpawnedFn g_spawned = nullptr;
constexpr uint8_t SpawnedEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x49, 0x8b, 0xc0, 0x48, 0x8b, 0xd9, 0x45, 0x85, 0xc9};

template <class T>
auto Field(const void* base, size_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof value);
	return value;
}

// The spawning room's drlg level, kept by its level id: the game thread walks that level's rooms later
void RememberLevel(const void* room) noexcept {
	__try {
		const auto* drlg = Field<const uint8_t*>(room, RoomDrlg);
		const auto* level = drlg != nullptr ? Field<const uint8_t*>(drlg, DrlgLevel) : nullptr;
		const int32_t id = level != nullptr ? Field<int32_t>(level, LevelId) : -1;
		if (id >= 0 && id < mc::MaxLevel) {
			g_drlgLevel[id].store(level, std::memory_order_relaxed);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

void OnSpawned(const uint8_t* const* regions, void* room, void* unit, int skip) noexcept {
	g_regions.store(regions, std::memory_order_relaxed);
	if (room != nullptr) {
		RememberLevel(room);
	}
	++g_spawns;
	g_spawned(regions, room, unit, skip);
}

// Game thread only. The drlg rooms already seen populated, per level (D2R frees rooms the player left).
mc::SeenRooms g_seen;
int32_t g_seenLevel = -1;
uint32_t g_seenEpoch = 0;
int32_t g_lastRemaining = -1;

auto Populated(const uint8_t* drlgRoom) noexcept -> bool {
	if (g_seen.Has(g_seenLevel, drlgRoom)) {
		return true;
	}
	bool done = false;
	__try {
		const auto* room = Field<const uint8_t*>(drlgRoom, DrlgRoom);
		done = (Field<uint32_t>(drlgRoom, DrlgFlags) & DrlgRoomMade) != 0 && room != nullptr &&
		       (Field<uint32_t>(room, RoomFlags) & 1) != 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	if (done) {
		g_seen.Add(g_seenLevel, drlgRoom);
	}
	return done;
}

auto LevelCount(const D2RL::PluginContext* context) noexcept -> int32_t {
	D2RL::DataTables::TableView view {.structSize = D2RL::DataTables::TableViewSize};
	// the bank only picks the compiled copy; every bank has the same levels
	if (g_tables->getTable(context, D2RL::DataTables::Bank::Rotw, D2RL::DataTables::TableId::Levels, &view) !=
	    D2RL::DataTables::Result::Success) {
		return 0;
	}
	return static_cast<int32_t>(view.rowCount);
}

// On the game thread: read the ledger and the level's rooms, publish the line
void __cdecl Refresh(const D2RL::PluginContext* context, void*) noexcept {
	const int32_t level = g_level.load();
	const auto* regions = g_regions.load(std::memory_order_relaxed);
	const uint32_t epoch = g_epoch.load();
	if (epoch != g_seenEpoch) {
		g_seen.byLevel.clear(); // a new game; a level change keeps its rooms
	}
	if (epoch != g_seenEpoch || level != g_seenLevel) {
		g_seenEpoch = epoch;
		g_seenLevel = level;
		g_lastRemaining = -1;
	}
	mc::Ledger ledger;
	bool read = false;
	__try {
		read = regions != nullptr && mc::Read(regions, level, LevelCount(context), RegionSpawned, RegionKilled, &ledger);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	if (!read || !mc::Shown(ledger)) {
		g_text.store(0);
		return;
	}
	const uint8_t* drlg = g_drlgLevel[level].load(std::memory_order_relaxed);
	bool complete = false;
	__try {
		complete = drlg != nullptr && mc::Explored(Field<const uint8_t*>(drlg, LevelFirstRoom), DrlgNext, Populated);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	const int32_t remaining = mc::Remaining(ledger);
	g_text.store(Pack(level, remaining, complete));
	if (g_lastRemaining != remaining && pd2::HotSample(g_changes)) {
		plog::Info("monsters remaining: level %d spawned %d killed %d, %zu rooms seen populated, %s -> %d%s", level, ledger.spawned,
		           ledger.killed, g_seen.Count(level), complete ? "all rooms populated" : "rooms still unmet", remaining, complete ? "" : "+");
	}
	g_lastRemaining = remaining;
}

void Forget(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	g_regions.store(nullptr);
	g_epoch.fetch_add(1);
	g_text.store(0);
}

void OnLevel(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (event != nullptr) {
		g_level.store(event->currentValue);
		g_text.store(0);
	}
}

// D2R's zone info builder (panel) and its string append (string, text, length). The builder's entry: mov r11,rsp /
// push rbp / push rbx / push rdi / lea rbp,[r11 - frame]; the pattern then needs lea rdi,[rcx + text] (the block's
// string, its offset read from the match) and lea r12,[rcx + 0x1e8] (the terror-zone lines).
using BuildFn = void (*)(uint8_t*) noexcept;
using AppendFn = void* (*)(void*, const char*, size_t) noexcept;
BuildFn g_build = nullptr;
AppendFn g_append = nullptr;
int32_t g_infoText = 0;  // panel offset of the block's string (93847: 0x198)
constexpr uint32_t BuildEntrySize = 13;
constexpr int BuildTextDisp = 44;  // the disp32 of lea rdi,[rcx + text] in the match
const D2RL::PluginContext* g_context = nullptr;
std::atomic<uint32_t> g_builds {0}, g_appended {0};

void QueueRefresh() noexcept {
	const uint64_t now = GetTickCount64();
	if (now - g_lastQueued >= RefreshMs) {
		g_lastQueued = now;
		const auto queued = g_threads->runOnGameThread(g_context, Refresh, nullptr);
		if (queued == D2RL::Threads::Result::Unavailable) {
			// no game thread (a TCP/IP client, or no game yet): nothing to read, nothing shown; asked again next time
			g_text.store(0);
		}
	}
}

// The original rebuilds the block; then one more row
void OnBuild(uint8_t* panel) noexcept {
	g_build(panel);
	++g_builds;
	QueueRefresh();
	const uint64_t word = g_text.load();
	if ((word >> 63) == 0 || static_cast<int32_t>(word >> 32 & 0xffff) != (g_level.load() & 0xffff)) {
		return;
	}
	char text[48] = "\n";
	mc::Text(static_cast<int32_t>(static_cast<uint32_t>(word)), (word >> 62 & 1) != 0, text + 1, sizeof text - 1);
	__try {
		void* block = panel + g_infoText;
		const char* line = Field<size_t>(block, 8) == 0 ? text + 1 : text;  // string {data, length, ...}: an empty block gets no newline
		g_append(block, line, std::strlen(line));
		++g_appended;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize) ||
	    context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (lifecycle, thread or data table service missing)");
		return;
	}
	g_context = context;
	uint64_t append = 0, build = 0;
	if (sigscan::ScanUnique(context->exeBase,
	                        "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 20 48 8b 79 08 48 8b f2 48 2b 31 "
	                        "49 03 f8 48 8b ea 4d 8b f0 48 8b d7 48 8b d9 e8 ?? ?? ?? ?? 48 8b 0b 48 8b 43 08 4d",
	                        0, &append) != 1 ||
	    sigscan::ScanUnique(context->exeBase,
	                        "4c 8b dc 55 53 57 49 8d ab ?? ?? ?? ?? 48 81 ec ?? ?? ?? ?? 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 85 ?? ?? ?? ?? "
	                        "49 89 73 10 48 8d b9 ?? ?? 00 00 48 8b 07 33 f6 4d 89 63 18 4c 8d a1 ?? ?? 00 00",
	                        0, &build) != 1) {
		std::snprintf(report, size, "FAILED (D2R's zone info builder or string append: signature not found)");
		return;
	}
	g_append = reinterpret_cast<AppendFn>(context->exeBase + append);
	const auto* buildAt = reinterpret_cast<const uint8_t*>(context->exeBase + build);
	g_infoText = Field<int32_t>(buildAt, BuildTextDisp);
	uint8_t buildEntry[BuildEntrySize];  // the frame size varies by build; the pattern fixed the rest
	std::memcpy(buildEntry, buildAt, sizeof buildEntry);
	plog::Info("monsters remaining: zone info builder at 0x%llx (text +0x%x), string append at 0x%llx", static_cast<unsigned long long>(build),
	           g_infoText, static_cast<unsigned long long>(append));
	if (!context->InstallInlineHook(build, buildEntry, sizeof buildEntry, OnBuild, &g_build)) {
		std::snprintf(report, size, "FAILED (the zone info builder hook was refused)");
		return;
	}
	if (!sigscan::HookBySignature(context, "MonsterRegionSpawned",
	                              "40 53 48 83 ec 20 49 8b c0 48 8b d9 45 85 c9 74 16 ba 02 00 00 00 48 8b c8 44 8d 42 ff "
	                              "48 83 c4 20 5b e9 ?? ?? ?? ?? 48 8b ca e8 ?? ?? ?? ?? 48 63 c8 48 8b 04 cb ff 80 cc 02 00 00",
	                              0, SpawnedEntry, sizeof SpawnedEntry, OnSpawned, &g_spawned)) {
		std::snprintf(report, size, "FAILED (the monster ledger spawn call: signature or bytes differ)");
		return;
	}
	using Kind = D2RL::Lifecycle::GameplayEventKind;
	const D2RL::Lifecycle::GameplayEventListener listeners[] = {
		{.structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = Kind::LevelChanged, .callback = OnLevel},
		{.structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = Kind::GameLeft, .callback = Forget},
		{.structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = Kind::GameJoined, .callback = Forget},
	};
	for (const auto& listener : listeners) {
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
			std::snprintf(report, size, "FAILED (a lifecycle listener was refused)");
			return;
		}
	}
	std::snprintf(report, size, "the zone info ends with Monsters Remaining: N (25+ over 25, N+ until 90%% of the level's rooms made their monsters)");
}

void Counters(char* out, size_t size) noexcept {
	const uint64_t word = g_text.load();
	std::snprintf(out, size, "monsters remaining: level %d, %u ledger spawns seen, %u zone info builds, %u rows added, showing %s%u%s",
	              g_level.load(), g_spawns.load(), g_builds.load(), g_appended.load(),
	              (word >> 63) != 0 ? "" : "nothing (", static_cast<uint32_t>(word), (word >> 63) != 0 ? ((word >> 62 & 1) != 0 ? "" : "+") : ")");
}

}  // namespace monsters_remaining
