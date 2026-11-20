#include "immunity_tooltip.h"

#include "log.h"
#include "map_immunities.h"  // common/

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/shared_events.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace immunity_tooltip {
namespace {

namespace items = D2RL::Items;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
std::atomic<uint32_t> g_lines{0}, g_reads{0}, g_failed{0};

void __cdecl OnTooltip(const D2RL::PluginContext*, D2RL::SharedEvents::ItemTooltipEvent* event, void*) noexcept {
	if (event == nullptr || event->structSize < D2RL::SharedEvents::ItemTooltipEventRequiredSize || event->text == nullptr || g_items == nullptr) {
		return;
	}
	items::ItemDetails details {.structSize = sizeof(items::ItemDetails)};
	g_items->getItemDetails(g_context, event->item, &details, nullptr, 0);  // reads statCount (the result code is not the count)
	if (details.statCount == 0 || details.statCount > 256) {
		return;
	}
	items::ItemStatValue stats[256];
	if (g_items->getItemDetails(g_context, event->item, &details, stats, 256) != items::Result::Success) {
		g_failed.fetch_add(1);
		return;
	}
	g_reads.fetch_add(1);
	pd2::mapimm::Resists r;
	for (uint32_t i = 0; i < details.statCount && i < 256; ++i) {
		int32_t* into = stats[i].statId == pd2::mapimm::StatFire    ? &r.fire
		                : stats[i].statId == pd2::mapimm::StatCold  ? &r.cold
		                : stats[i].statId == pd2::mapimm::StatLight ? &r.light
		                : stats[i].statId == pd2::mapimm::StatPoison ? &r.poison
		                                                             : nullptr;
		if (into != nullptr) {
			*into += stats[i].value;
		}
	}
	const size_t len = pd2::mapimm::Line(r, event->text, event->capacity);
	if (len != 0) {
		event->length = static_cast<uint32_t>(len);
		g_lines.fetch_add(1);
	}
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::SharedEventService* events = nullptr;
	if (context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success || !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceDetailsFieldEnd)) {
		g_items = nullptr;
		std::snprintf(report, size, "FAILED (no item details service)");
		return;
	}
	if (context->QueryService(&events) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasSharedEventServiceField(events, D2RL::SharedEventServiceRequiredSize)) {
		g_items = nullptr;
		std::snprintf(report, size, "FAILED (no shared event service)");
		return;
	}
	const D2RL::SharedEvents::ItemTooltipListener listener {
		.structSize = D2RL::SharedEvents::ItemTooltipListenerSize,
		.region = D2RL::SharedEvents::ItemTooltipRegion::Attributes,
		.position = D2RL::SharedEvents::ItemTooltipPosition::Top,
		.anchor = D2RL::SharedEvents::ItemTooltipAnchor::None,
		.fallback = D2RL::SharedEvents::ItemTooltipFallback::RegionTop,
		.callback = OnTooltip,
	};
	D2RL::SharedEvents::ListenerHandle handle = D2RL::SharedEvents::InvalidHandle;
	const auto rc = events->registerItemTooltipListener(context, &listener, &handle);
	if (rc != D2RL::SharedEvents::Result::Success) {
		g_items = nullptr;
		std::snprintf(report, size, "FAILED (no tooltip listener, result %d: loader event service is %u bytes)", static_cast<int>(rc), events->serviceSize);
		return;
	}
	std::snprintf(report, size, "a map's tooltip lists the monster resists of 100 or more as Immunities: C130, F125");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "map immunities: lines %u, stat reads %u (failed %u)", g_lines.load(), g_reads.load(), g_failed.load());
}

}  // namespace immunity_tooltip
