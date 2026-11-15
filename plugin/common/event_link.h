// The optional link between PD2's retained map events/ubers (core-events) and a generated-map provider such as
// Cartographer (#944, epic #942). Pure C++ with no engine or SDK headers, so a host check exercises it (plugin/tests).
//
// Contract (docs/event-link.md): the provider is optional and PD2-neutral. A consumer asks it for capabilities through
// D2RLoader's PluginCommunicationService and refuses before it consumes anything when one is missing. Cartographer
// publishes nothing yet, so today every Check says NoProvider and every consumer runs observe-only, from the SDK's own
// LifecycleService (LevelChanged, MonsterDeath). No Cartographer change is needed or made.
#pragma once

#include <cstdint>
#include <map>

namespace pd2::event_link {

inline constexpr uint32_t ContractVersion = 1;  // a provider's table must support older callers; new fields go at the end

enum Cap : uint32_t {
	Activation = 1u << 0,    // refuse or accept a map open before the item is consumed
	AreaIdentity = 1u << 1,  // which generated area a level id belongs to
	RoomProgress = 1u << 2,  // rooms activated / cleared in a generated area
	MonsterSetup = 1u << 3,  // a monster of a generated area, before it is shown
	Teardown = 1u << 4,      // the area closes
};

struct Provided {  // what a provider publishes; nullptr to Check = nothing published
	uint32_t version;
	uint32_t caps;
};

enum class Verdict { Ok, NoProvider, OldVersion, MissingCaps };

inline auto Check(const Provided* p, uint32_t need, uint32_t* missing = nullptr) noexcept -> Verdict {
	if (missing != nullptr) {
		*missing = need;
	}
	if (p == nullptr) {
		return Verdict::NoProvider;
	}
	if (p->version < ContractVersion) {
		return Verdict::OldVersion;
	}
	if (missing != nullptr) {
		*missing = need & ~p->caps;
	}
	return (need & ~p->caps) != 0 ? Verdict::MissingCaps : Verdict::Ok;
}

// Runs `consume` (take the item, charge the cost) only when every capability is there; otherwise nothing is touched.
template <typename Consume>
auto Activate(const Provided* p, uint32_t need, Consume&& consume) -> Verdict {
	const Verdict v = Check(p, need);
	if (v == Verdict::Ok) {
		consume();
	}
	return v;
}

// A generated area is its level id in one game session (the id is reused by the next map; the session generation tells
// them apart). Both come from the SDK's LevelChanged event.
struct AreaId {
	int32_t levelId;
	uint64_t session;
	friend auto operator<(const AreaId& a, const AreaId& b) noexcept -> bool {
		return a.session != b.session ? a.session < b.session : a.levelId < b.levelId;
	}
};

// Event metadata (the Catalyst's forced-event marker) must never reach a provider's native stat buffs. Cartographer turns
// every layer-0 item stat into a player buff and drops a stat that has a layer, so the marker lives on a layered stat.
inline constexpr int32_t MarkerLayer = 1;
inline auto IsEventMetadata(int32_t layer) noexcept -> bool { return layer == MarkerLayer; }

// Per-area progress, kept apart per area: opening or leaving a second area never edits the source's entry.
struct Progress {
	uint32_t monstersKilled = 0;
	bool closed = false;
	friend auto operator==(const Progress& a, const Progress& b) noexcept -> bool {
		return a.monstersKilled == b.monstersKilled && a.closed == b.closed;
	}
};

class AreaTracker {
public:
	void Entered(AreaId a) { areas_.try_emplace(a); }  // re-entering keeps the progress
	// A fresh open of a map: resets the entry. Cartographer reuses a slot's level id within one game, so without the
	// AreaIdentity capability the consumer must call this on every open, or map 2 inherits map 1's closed entry.
	void Opened(AreaId a) { areas_[a] = Progress{}; }
	void MonsterDied(AreaId a) {
		const auto it = areas_.find(a);
		if (it != areas_.end() && !it->second.closed) {
			++it->second.monstersKilled;
		}
	}
	void Closed(AreaId a) {
		const auto it = areas_.find(a);
		if (it != areas_.end()) {
			it->second.closed = true;
		}
	}
	auto Get(AreaId a) const -> Progress {
		const auto it = areas_.find(a);
		return it != areas_.end() ? it->second : Progress{};
	}

private:
	std::map<AreaId, Progress> areas_;
};

}  // namespace pd2::event_link
