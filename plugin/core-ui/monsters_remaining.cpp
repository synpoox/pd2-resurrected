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
// Drawing: the SDK OverlayService gives the per-frame callback (AfterGameUi), but on loader 1.3.1 its drawText draws
// nothing (checked in game), so the line is drawn with D2R's own text draw (text, int32 rect[4] screen px, font block,
// scale; the call TextBoxWidget::Draw makes), found by signature, from that callback. The font block is the item
// quantity's (item_quantity.cpp): D2R's defaults plus size and alignment. When the loader's overlay text works, swap.
//
// shortcut: HD view only (legacy, key g, draws no HD UI text: no line there; add a legacy draw when wanted); single
// player and the host only (a TCP/IP client has no local server and shows nothing); the line is always drawn at a fixed height under the native lines, not tied to the automap.
#include "monsters_remaining.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "monster_count.h"  // common/: the ledger read and its text
#include "sig.h"
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


// D2WinFont, 0xa4 bytes (see item_quantity.cpp for the field offsets): white, point size 24, right-aligned, top
struct Font {
	int32_t v[0x29];
};
constexpr auto MakeFont() noexcept -> Font {
	Font f {};
	f.v[1] = 24;
	for (int i = 2; i <= 9; ++i) {
		f.v[i] = 0xff;
	}
	f.v[0xa] = 2;  // h: right
	f.v[0xb] = 0;  // v: top
	f.v[0xc] = 0x3f800000;
	f.v[0xd] = 0x3f800000;
	f.v[0x10] = -1;
	f.v[0x11] = 0x100;
	f.v[0x1c] = 3;  // drop shadow offset
	f.v[0x1d] = 3;
	f.v[0x21] = 200;
	f.v[0x25] = 0xff;
	f.v[0x28] = 0xe;
	return f;
}
constexpr Font LineFont = MakeFont();

uint64_t g_drawText = 0;  // rva of D2R's text draw, by signature (0: the line is off)
using DrawTextFn = void (*)(const char*, const int32_t*, const Font*, float) noexcept;

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

void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame* frame, void*) noexcept {
	mc::Ledger ledger;
	const int32_t level = g_level.load();
	const auto* regions = g_regions.load(std::memory_order_relaxed);
	if (frame == nullptr || g_drawText == 0 || regions == nullptr || !ReadLedger(regions, level, &ledger) || !mc::Shown(ledger)) {
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
	NativeDraw(text, frame);
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
	if (sigscan::ScanUnique(context->exeBase,
	                        "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 70 0f 29 74 24 60 49 8b f0 0f 28 f3 48 8b fa 48 8b d9 e8 ?? ?? ?? ?? 84 c0 "
	                        "74 16 0f 28 de 4c 8b c6 48 8b d7 48 8b cb e8 ?? ?? ?? ?? e9 ?? ?? ?? ??",
	                        0, &g_drawText) != 1) {
		g_drawText = 0;
		std::snprintf(report, size, "FAILED (D2R's text draw: signature not found)");
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