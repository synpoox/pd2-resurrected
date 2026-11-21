// Monsters remaining (#1534). PD2 shows "Monsters Remaining: N" in its screen info (BH.dll draws it from a server packet,
// ProjectDiablo.dll 0x393ff0 -> BHSetMonstersRemaining(byte, dword)). Here core-ui draws the line under the zone info.
//
// The count is the one D2R keeps: MonsterRegion.cpp holds, per level, the counted hostile monsters spawned and killed
// (*(game + 0x1d0)[level] -> +0x2cc and +0x2d0; monstats neverCount and non-hostile units are left out by D2R itself).
// remaining = spawned - killed. D2R makes a room's monsters when the room first activates (room +0x54 bit 0, after the
// populator), so the count is only exact once every room of the level has: until then the line reads "N+" (the number
// can still grow), after it plain "N". The rooms are the level's drlg room list (level +0x10, next +0x48); a room
// counts as populated when its room (+0x58, made once the room is near) has +0x54 bit 0, and is remembered, as D2R
// frees rooms the player left. Nothing is forced to populate.
//
// Native, once: no SDK service gives a plugin the game region array (LifecycleService has the death event but no
// counts), so one hook on the ledger "spawned" call (build 93847 0x509520 (regions, room, unit, skip), found by
// signature) copies `regions` and the spawning room's drlg level, which every spawn passes. The hook calls the original
// untouched. The ledger and the rooms are server data: they are read on the game thread (ThreadService::runOnGameThread,
// about every 250 ms) and published in one atomic the overlay frame reads. A TCP/IP client has no game thread: it shows
// nothing. GameLeft and GameJoined forget the region pointer (a new game has a new array).
// Drawing: the SDK OverlayService gives the per-frame callback (AfterGameUi), but on loader 1.3.1 its drawText draws
// nothing (checked in game), so the line is drawn with D2R's own text draw (text, int32 rect[4] screen px, font block,
// scale; the call TextBoxWidget::Draw makes), found by signature, from that callback. The font block is the shared
// text_font.h. When the loader's overlay text works, swap.
//
// shortcut: HD view only (legacy, key g, draws no HD UI text: no line there; add a legacy draw when wanted); single
// player and the host only; the line is always drawn at a fixed height under the native lines, not tied to the automap.
#include "monsters_remaining.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "monster_count.h"  // common/: the ledger read and its text
#include "sig.h"
#include "sig_hook.h"
#include "text_font.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
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

const D2RL::OverlayService* g_overlay = nullptr;
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
		complete = drlg != nullptr && mc::AllPopulated(Field<const uint8_t*>(drlg, LevelFirstRoom), DrlgNext, Populated);
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

constexpr textfont::Font LineFont = textfont::Make(24, 2, 0);  // right-aligned from the top

uint64_t g_drawText = 0;  // rva of D2R's text draw, by signature (0: the line is off)
using DrawTextFn = void (*)(const char*, const int32_t*, const textfont::Font*, float) noexcept;

void NativeDraw(const char* text, const D2RL::Overlay::Frame* frame) noexcept {
	const int32_t w = static_cast<int32_t>(frame->screenWidth * 0.4f);
	const int32_t rect[4] = {static_cast<int32_t>(frame->screenWidth * 0.97f) - w, static_cast<int32_t>(frame->screenHeight * 0.12f), w,
	                         static_cast<int32_t>(frame->screenHeight * 0.04f)};
	__try {
		d2r::Engine<DrawTextFn>(g_drawText)(text, rect, &LineFont, 1.0f);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_drawText = 0;  // fail safe: no line after a fault
	}
}

void OnFrame(const D2RL::PluginContext* context, const D2RL::Overlay::Frame* frame, void*) noexcept {
	if (frame == nullptr || g_drawText == 0) {
		return;
	}
	const uint64_t now = GetTickCount64();
	if (now - g_lastQueued >= RefreshMs) {
		g_lastQueued = now;
		const auto queued = g_threads->runOnGameThread(context, Refresh, nullptr);
		if (queued == D2RL::Threads::Result::Unavailable) {
			// no game thread (a TCP/IP client, or no game yet): nothing to read, nothing shown; asked again next time
			g_text.store(0);
		}
	}
	const uint64_t word = g_text.load();
	if ((word >> 63) == 0 || static_cast<int32_t>(word >> 32 & 0xffff) != (g_level.load() & 0xffff)) {
		return;
	}
	char text[48];
	mc::Text(static_cast<int32_t>(static_cast<uint32_t>(word)), (word >> 62 & 1) != 0, text, sizeof text);
	NativeDraw(text, frame);
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
	    context->QueryService(&g_overlay) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasOverlayServiceField(g_overlay, D2RL::OverlayServiceRequiredSize) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize) ||
	    context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (lifecycle, overlay, thread or data table service missing)");
		return;
	}
	if (sigscan::ScanUnique(context->exeBase,
	                        "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 70 0f 29 74 24 60 49 8b f0 0f 28 f3 48 8b fa 48 8b d9 e8 ?? ?? ?? ?? 84 c0 "
	                        "74 16 0f 28 de 4c 8b c6 48 8b d7 48 8b cb e8 ?? ?? ?? ?? e9 ?? ?? ?? ??",
	                        0, &g_drawText) != 1) {
		g_drawText = 0;
		std::snprintf(report, size, "FAILED (D2R's text draw: signature not found)");
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
	const D2RL::Overlay::CallbackRegistration frame {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                 .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                 .callback = OnFrame};
	D2RL::Overlay::CallbackHandle handle = D2RL::Overlay::InvalidCallbackHandle;
	if (g_overlay->registerFrameCallback(context, &frame, &handle) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (the frame callback was refused)");
		return;
	}
	std::snprintf(report, size, "the zone info shows Monsters remaining: N (N+ until every room of the level has made its monsters)");
}

void Counters(char* out, size_t size) noexcept {
	const uint64_t word = g_text.load();
	std::snprintf(out, size, "monsters remaining: level %d, %u ledger spawns seen, showing %s%u%s", g_level.load(), g_spawns.load(),
	              (word >> 63) != 0 ? "" : "nothing (", static_cast<uint32_t>(word), (word >> 63) != 0 ? ((word >> 62 & 1) != 0 ? "" : "+") : ")");
}

}  // namespace monsters_remaining
