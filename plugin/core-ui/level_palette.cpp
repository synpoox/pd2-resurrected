// No SDK service loads a palette, so this calls D2R's own act palette load (rva::LoadActPalette) from the SDK's
// LevelChanged event; no hook. It also sets D2R's palette choice to the new level's Pal, so a load screen that ends
// after the event (a `cheat goto` across acts, #441) loads the same one.
#include "level_palette.h"

#include "pd2_engine.h"
#include "log.h"

#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>

namespace level_palette {

namespace {

static_assert(!NeedsLoad(-1, 0, 4));  // the game's first level
static_assert(!NeedsLoad(109, 4, 4));  // Harrogath -> Frigid Highlands
static_assert(NeedsLoad(109, 4, 3));   // Harrogath -> Mesa (Act 5, Pal 3)

std::atomic<uint32_t> g_loads {0};
uint32_t g_changes = 0;

auto PalOf(int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	using d2r::Unit;
	if (level <= 0) {
		return -1;
	}
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	if (local == nullptr) {
		return -1;
	}
	const uint8_t flag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(UnitDataFlag)(local);
	const uint8_t* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(LevelRecord)(flag, level);
	return record != nullptr ? d2r::At<uint8_t>(record, LvlPal) : -1;
}

void __cdecl OnLevel(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	using namespace d2r::rva;
	++g_changes;
	const int32_t pal = PalOf(event->currentValue);
	const int32_t previous = PalOf(event->previousValue);
	if (pal < 0 || previous < 0 || !NeedsLoad(event->previousValue, static_cast<uint8_t>(previous), static_cast<uint8_t>(pal))) {
		return;
	}
	d2r::Engine<void (*)(uint32_t) noexcept>(SetActPalette)(static_cast<uint32_t>(pal));
	d2r::Engine<void (*)(uint8_t) noexcept>(LoadActPalette)(static_cast<uint8_t>(pal));
	++g_loads;
	plog::Info("palette: level %d -> %d, act %d palette loaded", event->previousValue, event->currentValue, pal + 1);
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (no lifecycle service)");
		return;
	}
	const D2RL::Lifecycle::GameplayEventListener listener {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                       .kind = D2RL::Lifecycle::GameplayEventKind::LevelChanged,
	                                                       .callback = OnLevel};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, size, "FAILED (no level-change listener)");
		return;
	}
	std::snprintf(report, size, "a level whose Pal differs from the last loads its own legacy palette");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "palette: level changes %u, palettes loaded %u", g_changes, g_loads.load());
}

}  // namespace level_palette
