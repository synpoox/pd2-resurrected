// Monsters remaining (#1534). PD2 shows "Monsters Remaining: N" in its screen info (BH.dll draws it from a server packet,
// ProjectDiablo.dll 0x393ff0 -> BHSetMonstersRemaining(byte, dword)). Here core-ui draws the line under the zone info.
//
// The count is the one D2R keeps: MonsterRegion.cpp holds, per level, the counted hostile monsters spawned and killed
// (*(game + 0x1d0)[level] -> +0x2cc and +0x2d0; monstats neverCount and non-hostile units are left out by D2R itself).
// remaining = spawned - killed. D2R fills a room when the player first comes near it, so the number is the monsters
// created so far and grows while the level is explored.
//
// Native, once: no SDK service gives a plugin the game region array (LifecycleService has the death event but no
// counts), so one hook on the ledger "spawned" call (build 93847 0x509520 (regions, room, unit, skip), found by
// signature) copies `regions`, which every spawn passes. The hook calls the original untouched. The UI thread reads the
// ledger with a guarded read; GameLeft and GameJoined forget the pointer (a new game has a new array).
// Drawing is the SDK OverlayService: AfterGameUi text, placed from Frame::screenWidth and measureText.
//
// shortcut: single player and the host only (a TCP/IP client has no local server and shows nothing); the line is always
// drawn at a fixed height under the native lines, not tied to the automap.
#include "monsters_remaining.h"

#include "hot_log.h"
#include "log.h"
#include "monster_count.h"  // common/: the ledger read and its text
#include "sig_hook.h"

#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>

#include <atomic>
#include <cstdio>

#include <windows.h>

namespace monsters_remaining {

namespace {

namespace mc = pd2::monstercount;

constexpr size_t RegionSpawned = 0x2cc, RegionKilled = 0x2d0;  // MonsterRegion: counted hostile monsters spawned, killed

const D2RL::OverlayService* g_overlay = nullptr;
std::atomic<const uint8_t* const*> g_regions {nullptr};  // *(game + 0x1d0): MonsterRegion*[level id]
std::atomic<int32_t> g_level {-1};
std::atomic<uint32_t> g_spawns {0}, g_changes {0};
std::atomic<int32_t> g_last {-1};  // the remaining count last drawn (-1: none)

// (regions, room, unit, skip): ++spawned unless skip. The first 15 bytes: push rbx / sub rsp,0x20 / mov rax,r8 /
// mov rbx,rcx / test r9d,r9d.
using SpawnedFn = void (*)(const uint8_t* const*, void*, void*, int) noexcept;
SpawnedFn g_spawned = nullptr;
constexpr uint8_t SpawnedEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x49, 0x8b, 0xc0, 0x48, 0x8b, 0xd9, 0x45, 0x85, 0xc9};

void OnSpawned(const uint8_t* const* regions, void* room, void* unit, int skip) noexcept {
	g_regions.store(regions, std::memory_order_relaxed);
	++g_spawns;
	g_spawned(regions, room, unit, skip);
}

// UI thread. The array belongs to the server game: a read can meet a freed one, so it is guarded.
auto ReadLedger(const uint8_t* const* regions, int32_t level, mc::Ledger* out) noexcept -> bool {
	__try {
		return mc::Read(regions, level, RegionSpawned, RegionKilled, out);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void Forget(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	g_regions.store(nullptr);
	g_level.store(-1);
	g_last.store(-1);
}

void OnLevel(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (event != nullptr) {
		g_level.store(event->currentValue);
		g_last.store(-1);
	}
}

void OnFrame(const D2RL::PluginContext* context, const D2RL::Overlay::Frame* frame, void*) noexcept {
	mc::Ledger ledger;
	const int32_t level = g_level.load();
	const auto* regions = g_regions.load(std::memory_order_relaxed);
	if (frame == nullptr || regions == nullptr || !ReadLedger(regions, level, &ledger) || !mc::Shown(ledger)) {
		g_last.store(-1);
		return;
	}
	char text[48];
	const int length = mc::Text(ledger, text, sizeof text);
	if (length <= 0) {
		return;
	}
	const int32_t remaining = mc::Remaining(ledger);
	if (g_last.exchange(remaining) != remaining && pd2::HotSample(g_changes)) {
		plog::Info("monsters remaining: level %d spawned %d killed %d -> %d", level, ledger.spawned, ledger.killed, remaining);
	}
	D2RL::Overlay::TextMetrics size {.structSize = D2RL::Overlay::TextMetricsSize};
	const D2RL::Overlay::MeasureTextRequest measure {.structSize = D2RL::Overlay::MeasureTextRequestSize,
	                                                 .canvas = frame->canvas,
	                                                 .text = text,
	                                                 .textLength = static_cast<uint32_t>(length)};
	if (g_overlay->measureText(context, &measure, &size) != D2RL::Overlay::Result::Success) {
		size.width = 0;
	}
	const D2RL::Overlay::TextRequest draw {.structSize = D2RL::Overlay::TextRequestSize,
	                                       .canvas = frame->canvas,
	                                       .position = {frame->screenWidth - size.width - 0.03f * frame->screenWidth,
	                                                    0.12f * frame->screenHeight},
	                                       .color = {1.0f, 1.0f, 1.0f, 1.0f},
	                                       .text = text,
	                                       .textLength = static_cast<uint32_t>(length)};
	g_overlay->drawText(context, &draw);
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
	    context->QueryService(&g_overlay) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasOverlayServiceField(g_overlay, D2RL::OverlayServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (lifecycle or overlay service missing)");
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
	std::snprintf(report, size, "the zone info shows Monsters remaining: N (hostile monsters spawned minus killed in the level)");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "monsters remaining: level %d, %u ledger spawns seen, showing %d", g_level.load(), g_spawns.load(),
	              g_last.load());
}

}  // namespace monsters_remaining