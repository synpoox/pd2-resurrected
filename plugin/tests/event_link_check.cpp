// Host self-check of plugin/common/event_link.h (#944). The event_link.h checks always run. With -DCARTOGRAPHER_SRC the same file also runs
// the metadata checks against Cartographer's own statplan.cpp (read-only, never modified): without it those print SKIPPED.
//   cmake -S plugin -B plugin/build-checks -DCARTOGRAPHER_SRC=<d2r-tools>/plugins/cartographer/src && cmake --build plugin/build-checks --target event_link_check
#include "event_link.h"

#ifdef CARTOGRAPHER_CHECK
#include "statplan.h"
#endif

#include <cstdio>
#include <cstdlib>

namespace el = pd2::event_link;

static int g_pass = 0, g_fail = 0, g_skip = 0;
#define CHECK(name, cond) do { if (cond) { ++g_pass; std::printf("PASS  %s\n", name); } else { ++g_fail; std::printf("FAIL  %s\n", name); } } while (0)

struct Source {  // the source map's state the refusal must leave alone
	int items = 1;
	el::AreaId area{101, 1};
	el::Progress progress{};
};

int main() {
	// --- missing-capability refusal consumes nothing, source state stays stable
	el::AreaTracker tracker;
	Source src;
	tracker.Entered(src.area);
	tracker.MonsterDied(src.area);
	const el::Progress before = tracker.Get(src.area);
	const uint32_t need = el::Activation | el::AreaIdentity | el::MonsterSetup;
	{
		int consumed = 0;
		uint32_t missing = 0;
		CHECK("no provider: verdict NoProvider", el::Check(nullptr, need, &missing) == el::Verdict::NoProvider && missing == need);
		CHECK("no provider: refused, item not consumed", el::Activate(nullptr, need, [&] { ++consumed; src.items = 0; }) == el::Verdict::NoProvider && consumed == 0 && src.items == 1);
		const el::Provided partial{1, el::Activation};
		CHECK("partial provider: names the missing caps", el::Check(&partial, need, &missing) == el::Verdict::MissingCaps && missing == (el::AreaIdentity | el::MonsterSetup));
		CHECK("partial provider: refused, item not consumed", el::Activate(&partial, need, [&] { ++consumed; }) == el::Verdict::MissingCaps && consumed == 0);
		const el::Provided old{0, need};
		CHECK("old contract version: refused", el::Activate(&old, need, [&] { ++consumed; }) == el::Verdict::OldVersion && consumed == 0);
		CHECK("source state stable after every refusal", tracker.Get(src.area) == before && src.items == 1);
		const el::Provided full{el::ContractVersion + 1, need | el::Teardown};
		CHECK("newer provider supports the caller: accepted, consumed once", el::Activate(&full, need, [&] { ++consumed; }) == el::Verdict::Ok && consumed == 1);
	}
	// --- a second area is independent of the source
	{
		const el::AreaId dest{102, 1}, reused{101, 2};
		tracker.Entered(dest);
		tracker.MonsterDied(dest);
		tracker.MonsterDied(dest);
		tracker.Closed(dest);
		tracker.MonsterDied(dest);  // closed: ignored
		CHECK("destination progress counts and closes on its own", tracker.Get(dest).monstersKilled == 2 && tracker.Get(dest).closed);
		CHECK("source progress unchanged by the destination", tracker.Get(src.area) == before);
		tracker.Entered(reused);
		CHECK("same level id in a new session is a different area", tracker.Get(reused).monstersKilled == 0);
		tracker.Opened(dest);  // slot reuse: the next map opens in the same level id
		tracker.MonsterDied(dest);
		CHECK("slot reuse: a fresh open resets a closed entry", tracker.Get(dest).monstersKilled == 1 && !tracker.Get(dest).closed);
	}
	CHECK("marker layer is event metadata, layer 0 is not", el::IsEventMetadata(el::MarkerLayer) && !el::IsEventMetadata(0));

	// --- Cartographer standalone defaults and metadata exclusion (its real code)
#ifdef CARTOGRAPHER_CHECK
	namespace sp = cartographer::statplan;
	sp::StatIds ids;
	ids.loaded = true;
	ids.byName = {{"cartographer_mon_life", 900}, {"cartographer_map_level", 901}, {"fireresist", 39}, {"item_maxhp_percent", 76}, {"map_force_event", 553}};
	const auto hasPlayer = [](const sp::ItemProperties& p, const char* stat) {
		for (const sp::Property& x : p.properties) { if (x.stat == stat) return true; }
		return false;
	};
	const sp::ItemProperties naive = sp::itemStatsToProperties({{553, 0, 1}}, ids);
	std::printf("INFO  layer-0 marker -> %zu properties (stat %s)\n", naive.properties.size(), naive.properties.empty() ? "-" : naive.properties[0].stat.c_str());
	CHECK("baseline: a layer-0 event marker WOULD become a native player buff", hasPlayer(naive, "map_force_event"));
	const sp::ItemProperties layered = sp::itemStatsToProperties({{553, el::MarkerLayer, 1}}, ids);
	CHECK("layered marker is dropped by Cartographer, not applied", layered.properties.empty() && layered.dropped.size() == 1);
	std::printf("INFO  dropped: %s\n", layered.dropped.empty() ? "-" : layered.dropped[0].c_str());
	const sp::ItemProperties mixed = sp::itemStatsToProperties({{900, 0, 30}, {553, el::MarkerLayer, 1}, {39, 0, -20}}, ids);
	const sp::Plan plan = sp::buildPlan(mixed.properties, ids);
	CHECK("native properties intact beside the marker", plan.monsters.size() == 1 && plan.players.size() == 1 && plan.monsters[0].value == 30 && plan.players[0].value == -20);
	CHECK("marker absent from the player's stat list", plan.players.size() == 1 && plan.players[0].name == "fireresist");
	bool pd2policy = false;
	for (const sp::MonsterStat& m : sp::monsterStats()) {
		const std::string s = std::string(m.itemStat) + m.applied;
		pd2policy = pd2policy || s.find("map_") != std::string::npos || s.find("tier") != std::string::npos || s.find("pd2") != std::string::npos;
	}
	CHECK("Cartographer carries no PD2 map-stat or tier policy", !pd2policy);
	const sp::MapLevelPick pick = sp::mapLevelFromItem({}, ids, 85);
	CHECK("standalone default: item without a level stat opens at maps.txt Level", pick.level == 85 && !pick.fromItem);
	const sp::MapLevelPick pick2 = sp::mapLevelFromItem({{901, 0, 91}, {553, el::MarkerLayer, 1}}, ids, 85);
	CHECK("standalone default: the item's map level wins, marker ignored", pick2.level == 91 && pick2.fromItem);
#else
	g_skip = 7;
	std::printf("SKIPPED  7 Cartographer metadata and standalone-default checks (no -DCARTOGRAPHER_SRC)\n");
#endif
	std::printf("\n%d passed, %d failed, %d skipped\n", g_pass, g_fail, g_skip);
	return g_fail == 0 ? 0 : 1;
}
