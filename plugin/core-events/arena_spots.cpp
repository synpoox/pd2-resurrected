// Named spots of Cartographer's fixed arenas (#958, arena_spots.h). The service table is leased once, lazily; a miss is
// retried on the next call, as Cartographer may load after core-events.
#include "arena_spots.h"

#include "cartographer_arena_v1.h"
#include "log.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>

namespace {

const D2RL::PluginContext* g_context = nullptr;
D2RL::PluginCommunication::ServiceLease<carto::ArenaServiceV1> g_lease;
std::mutex g_mutex;
bool g_acquireWarned = false;

// shortcut: a missing service is asked for on every call (a refused fight is cheap), upgrade if it shows in a profile
auto Service() noexcept -> const carto::ArenaServiceV1* {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_lease && g_context != nullptr) {
		const D2RL::PluginCommunicationService* communication = nullptr;
		if (g_context->QueryService(&communication) != D2RL::ServiceQueryResult::Success ||
		    !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
			return nullptr;
		}
		const auto result = D2RL::PluginCommunication::Acquire(g_context, communication, carto::ArenaServiceProvider,
		                                                       carto::ArenaServiceName, carto::ArenaServiceVersion,
		                                                       sizeof(carto::ArenaServiceV1), &g_lease);
		if (result != D2RL::PluginCommunication::Result::Success && !g_acquireWarned) {
			g_acquireWarned = true;
			plog::Warn("arena spots: %s not available yet (Cartographer missing or older)", carto::ArenaServiceName);
		}
	}
	return g_lease.Get();
}

// Logs the first miss of each encounter and spot, so a missing arena row shows once in the log, not on every think.
auto FirstMiss(const char* encounter, const char* spot) noexcept -> bool {
	static const char* seen[32][2] = {};
	static size_t count = 0;
	for (size_t i = 0; i < count; ++i) {
		if (std::strcmp(seen[i][0], encounter) == 0 && std::strcmp(seen[i][1], spot) == 0) {
			return false;
		}
	}
	if (count < std::size(seen)) {
		seen[count][0] = encounter;
		seen[count][1] = spot;
		++count;
	}
	return true;
}

}  // namespace

auto InstallArenaSpots(const D2RL::PluginContext* context, char* report, size_t size) noexcept -> int {
	g_context = context;
	std::snprintf(report, size, "%s acquired at first use", carto::ArenaServiceName);
	return context != nullptr ? 1 : 0;
}

auto ArenaReady(int32_t level) noexcept -> bool {
	const carto::ArenaServiceV1* service = Service();
	return service != nullptr && service->isArena(level);
}

auto ArenaSpotOf(const char* encounter, const char* spot, placements::Spot* out) noexcept -> bool {
	const carto::ArenaServiceV1* service = Service();
	carto::ArenaSpotV1 found{};
	found.structSize = sizeof(found);
	if (service == nullptr || !service->findSpot(placements::LevelOf(encounter), spot, &found)) {
		if (FirstMiss(encounter, spot)) {
			plog::Warn("arena spots: no spot '%s' for '%s' (level %d): the arena is not built or the row is missing",
			           spot, encounter, placements::LevelOf(encounter));
		}
		return false;
	}
	out->x = found.subX;
	out->y = found.subY;
	return true;
}
