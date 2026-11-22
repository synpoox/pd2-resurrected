// Monsters remaining (#1534, #1656). PD2 shows "Monsters Remaining: N" in its screen info: its server (ProjectDiablo.dll
// 0x103453f0, every 12 frames) sends packet 0xAD (flag, count) with the same ledger, spawned - killed, and BH.dll draws
// it (docs/research/monsters-remaining.md). Here the line is the last row of D2R's own zone info block.
//
// The count is the one D2R keeps: MonsterRegion.cpp holds, per level, the counted hostile monsters spawned and killed
// (*(game + 0x1d0)[level] -> +0x2cc and +0x2d0; monstats neverCount and non-hostile units are left out by D2R itself).
// remaining = spawned - killed. D2R makes a room's monsters when the player first comes near the room, so the count
// covers the rooms met so far; nothing is forced to populate (PD2 forces nothing either). PD2's rule: the count is
// final once rooms made x 100 / rooms >= 90 (mc::Explored); until then the line reads "50+" (PD2: "100+"), after it
// the number, "50+" over 50 (mc::Text). The rooms are the region's own, as PD2 reads them: +0x04 made, +0x0c counted
// (mc::ReadRooms; not the drlg room list, whose outdoor rooms never all make monsters, #1729). A town (mc::IsTown, PD2's
// own five ids) or a level whose ledger counted no monster shows no line.
//
// Native, once: no SDK service gives a plugin the game region array (LifecycleService has the death event but no
// counts), so one hook on the ledger "spawned" call (build 93847 0x509520 (regions, room, unit, skip), found by
// signature) copies `regions`. The hook calls the original untouched. The ledger is server
// data: it is read on the game thread (ThreadService::runOnGameThread, about every 250 ms) and published in one atomic
// the text box hook reads. A TCP/IP client has no game thread: it shows nothing. GameLeft and GameJoined forget the
// region pointer (a new game has a new array).
// Drawing: no SDK service adds a zone info row, so two more hooks, by signature. D2R's zone info builder (93847
// 0x14751e0 (panel)) rebuilds the panel's newline-separated text (area / difficulty / game type ... at panel +0x198,
// the terror-zone lines at +0x1e8); its hook only learns the panel. The panel update (0x1476290) then hands that text,
// with the loader's mod name row added, to the panel's text box (+0x170) through TextBox::SetText (0x86dd50 (box,
// {text, length}*), which copies it). That hook adds "\nMonsters Remaining: N" for the panel's box only, so D2R draws
// it as the block's last row: same font, colour and right alignment, hidden with the automap (TAB), the terror-zone
// lines below it.
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
constexpr size_t RegionRoomsMade = 0x04, RegionRooms = 0x0c;  // MonsterRegion: rooms that made their monsters, rooms counted
constexpr uint64_t RefreshMs = 250;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
std::atomic<const uint8_t* const*> g_regions {nullptr};  // *(game + 0x1d0): MonsterRegion*[level id]
std::atomic<int32_t> g_level {-1};
std::atomic<uint32_t> g_spawns {0}, g_changes {0}, g_builds {0}, g_appended {0};
std::atomic<uint64_t> g_text {0};  // published: see Pack
std::atomic<uint64_t> g_lastQueued {0};  // the builder (UI thread) and spawns (game thread)

// The published line: valid | explored | level | remaining, one word so the text box hook never reads half an update
constexpr auto Pack(int32_t level, int32_t remaining, bool explored) noexcept -> uint64_t {
	return 1ull << 63 | (explored ? 1ull << 62 : 0) | static_cast<uint64_t>(level & 0xffff) << 32 | static_cast<uint32_t>(remaining);
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

void QueueRefresh() noexcept;

void OnSpawned(const uint8_t* const* regions, void* room, void* unit, int skip) noexcept {
	g_regions.store(regions, std::memory_order_relaxed);
	++g_spawns;
	g_spawned(regions, room, unit, skip);
	QueueRefresh();
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
int32_t g_lastRemaining = -1, g_lastLevel = -1, g_lastMade = -1;  // game thread only
void __cdecl Refresh(const D2RL::PluginContext* context, void*) noexcept {
	const int32_t level = g_level.load();
	const auto* regions = g_regions.load(std::memory_order_relaxed);
	mc::Ledger ledger;
	mc::Rooms rooms;
	bool read = false;
	__try {
		read = regions != nullptr && mc::Read(regions, level, LevelCount(context), RegionSpawned, RegionKilled, &ledger);
		rooms = read ? mc::ReadRooms(regions[level], RegionRoomsMade, RegionRooms) : mc::Rooms {};
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	if (level != g_lastLevel && pd2::HotSample(g_changes)) {
		plog::Info("monsters remaining: level %d region %p town %d spawned %d killed %d", level, read ? regions[level] : nullptr,
		           mc::IsTown(level) ? 1 : 0, ledger.spawned, ledger.killed);
	}
	g_lastLevel = level;
	if (!read || mc::IsTown(level) || !mc::Shown(ledger)) {
		g_text.store(0);
		g_lastRemaining = -1;
		return;
	}
	const int32_t remaining = mc::Remaining(ledger);
	const bool explored = mc::Explored(rooms);
	g_text.store(Pack(level, remaining, explored));
	if ((g_lastRemaining != remaining || g_lastMade != rooms.made) && pd2::HotLogsOn()) {
		plog::Info("monsters remaining: level %d spawned %d killed %d, rooms %d of %d made (%d%%) -> %d%s", level, ledger.spawned,
		           ledger.killed, rooms.made, rooms.total, rooms.total > 0 ? rooms.made * 100 / rooms.total : 0, remaining,
		           explored ? "" : ", not explored: 50+");
	}
	g_lastRemaining = remaining;
	g_lastMade = rooms.made;
}

void Forget(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	g_regions.store(nullptr);
	g_text.store(0);
}

void OnLevel(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (event != nullptr) {
		g_level.store(event->currentValue);
		g_text.store(0);
	}
}

// D2R's zone info builder (panel). Its entry: mov r11,rsp / push rbp / push rbx / push rdi / lea rbp,[r11 - frame]; the
// pattern then needs lea rdi,[rcx + 0x198] (the block's string) and lea r12,[rcx + 0x1e8] (the terror-zone lines).
using BuildFn = void (*)(uint8_t*) noexcept;
BuildFn g_build = nullptr;
constexpr uint32_t BuildEntrySize = 13;
std::atomic<uint8_t*> g_panel {nullptr};
constexpr size_t PanelTextBox = 0x170;  // the panel's text box for the block (93847: the panel update 0x1476290 sets it)

// TextBox::SetText (box, {text, length}*): copies the text. Entry: push rbx / sub rsp,0x20 / mov r8,[rdx+8] / mov rbx,rcx.
struct TextView {
	const char* text;
	size_t length;
};
using SetTextFn = void (*)(void*, const TextView*) noexcept;
SetTextFn g_setText = nullptr;
constexpr uint8_t SetTextEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x8b, 0x42, 0x08, 0x48, 0x8b, 0xd9};
constexpr size_t MaxBlock = 2048;  // a longer block is passed on untouched

void QueueRefresh() noexcept {
	const uint64_t now = GetTickCount64();
	uint64_t last = g_lastQueued.load();
	if (now - last >= RefreshMs && g_lastQueued.compare_exchange_strong(last, now)) {
		const auto queued = g_threads->runOnGameThread(g_context, Refresh, nullptr);
		if (queued == D2RL::Threads::Result::Unavailable) {
			// no game thread (a TCP/IP client, or no game yet): nothing to read, nothing shown; asked again next time
			g_text.store(0);
		}
	}
}

// The builder only tells which panel is the zone info (and paces the refresh)
void OnBuild(uint8_t* panel) noexcept {
	g_build(panel);
	g_panel.store(panel, std::memory_order_relaxed);
	++g_builds;
	QueueRefresh();
}

// The block's text reaches its text box last, after anything else added rows (the loader adds the mod name after the
// builder returns), so the row goes on here: the panel's box gets text + "\n" + the row, in a copy (SetText copies).
void OnSetText(void* box, const TextView* view) noexcept {
	const uint64_t word = g_text.load();
	uint8_t* panel = g_panel.load(std::memory_order_relaxed);
	bool ours = false;
	__try {
		ours = panel != nullptr && box == Field<void*>(panel, PanelTextBox) && view != nullptr && view->length < MaxBlock;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	if (!ours || (word >> 63) == 0 || static_cast<int32_t>(word >> 32 & 0xffff) != (g_level.load() & 0xffff)) {
		g_setText(box, view);
		return;
	}
	static char block[MaxBlock + 64];  // UI thread only
	std::memcpy(block, view->text, view->length);
	size_t n = view->length;
	// the loader's mod name row ends with a newline (the gap before the terror-zone lines): the row goes before it
	const bool trailing = n != 0 && block[n - 1] == '\n';
	if (n != 0 && !trailing) {
		block[n++] = '\n';
	}
	mc::Text(static_cast<int32_t>(static_cast<uint32_t>(word)), (word >> 62 & 1) != 0, block + n, sizeof block - n - 1);
	n += std::strlen(block + n);
	if (trailing) {
		block[n++] = '\n';
	}
	const TextView longer {block, n};
	++g_appended;
	g_setText(box, &longer);
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
	uint64_t build = 0;
	if (sigscan::ScanUnique(context->exeBase,
	                        "4c 8b dc 55 53 57 49 8d ab ?? ?? ?? ?? 48 81 ec ?? ?? ?? ?? 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 85 ?? ?? ?? ?? "
	                        "49 89 73 10 48 8d b9 98 01 00 00 48 8b 07 33 f6 4d 89 63 18 4c 8d a1 e8 01 00 00",
	                        0, &build) != 1) {
		std::snprintf(report, size, "FAILED (D2R's zone info builder: signature not found)");
		return;
	}
	uint8_t buildEntry[BuildEntrySize];  // the frame size varies by build; the pattern fixed the rest
	std::memcpy(buildEntry, reinterpret_cast<const uint8_t*>(context->exeBase + build), sizeof buildEntry);
	plog::Info("monsters remaining: zone info builder at 0x%llx", static_cast<unsigned long long>(build));
	if (!context->InstallInlineHook(build, buildEntry, sizeof buildEntry, OnBuild, &g_build) ||
	    !sigscan::HookBySignature(context, "TextBoxSetText",
	                              "40 53 48 83 ec 20 4c 8b 42 08 48 8b d9 48 8b 12 48 81 c1 88 00 00 00 e8 ?? ?? ?? ?? 48 8b cb 48 83 c4 20 5b e9",
	                              0, SetTextEntry, sizeof SetTextEntry, OnSetText, &g_setText)) {
		std::snprintf(report, size, "FAILED (the zone info builder or text box hook was refused)");
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
	std::snprintf(report, size, "the zone info ends with Monsters Remaining: N (50+ over 50 or under 90%% of the rooms made)");
}

void Counters(char* out, size_t size) noexcept {
	const uint64_t word = g_text.load();
	std::snprintf(out, size, "monsters remaining: level %d, %u ledger spawns seen, %u zone info builds, %u rows added, showing %s%u%s",
	              g_level.load(), g_spawns.load(), g_builds.load(), g_appended.load(), (word >> 63) != 0 ? "" : "nothing (",
	              static_cast<uint32_t>(word), (word >> 63) != 0 ? "" : ")");
}

}  // namespace monsters_remaining
