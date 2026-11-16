// Per-encounter level bindings and placements (#947, docs/cartographer-contracts.md "Encounter placements").
// The one place AI, server/client skills, stage travel, chests and re-entry read an encounter's level and its
// coordinates from. The data is one file per encounter in plugin/placements/ (built into core-events and core-rules);
// a private author changes that file only, never a consumer. Header only: each DLL holds its own copy of the same data.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace placements {

// Where an encounter's level comes from. Existing: the PD2 area the port has now; a private author flips `level` to
// `target` (a reserved private id) when the new area is ready. Vanilla and D2RTristram are D2R's own areas and are
// never replaced (no arena is reserved for them).
enum class Source : uint8_t { Vanilla, D2RTristram, Existing, Private };

struct Binding {
	const char* encounter;
	int32_t level;   // the level the consumers read now
	int32_t target;  // the frozen destination: equals `level` once the encounter is done
	Source source;
	bool uber;        // an uber fight level (PD2 0x1037d840: portal entry rules, area rules)
	bool noWaypoint;  // waypoints are removed on arrival
};

// A point (w = h = 0) or the region [x, x + w) x [y, y + h) of the encounter's level, in subtiles. `arg` is a
// per-place number (a monstats class id for a boss spot).
struct Place {
	const char* encounter;
	const char* id;
	int32_t x, y, w, h, arg;
};

struct Spot {
	int32_t x, y;
};

constexpr size_t MaxRegs = 48;
struct Registry {
	const Binding* bindings[MaxRegs];
	size_t bindingCounts[MaxRegs];
	const Place* places[MaxRegs];
	size_t placeCounts[MaxRegs];
	size_t regs = 0;
	bool overflowed = false;
	uint32_t missing = 0;  // distinct placements a consumer asked for that no file holds (each counted once)
	const char* missingEnc[16] = {};
	const char* missingId[16] = {};
};
inline auto Reg_() noexcept -> Registry& {
	static Registry r;
	return r;
}

// One encounter's file: `static const placements::Reg reg(bindings, std::size(bindings), places, std::size(places));`
struct Reg {
	Reg(const Binding* b, size_t nb) noexcept : Reg(b, nb, nullptr, 0) {}  // bindings only (D2R's own areas)
	Reg(const Binding* b, size_t nb, const Place* p, size_t np) noexcept {
		Registry& r = Reg_();
		if (r.regs >= MaxRegs) {
			r.overflowed = true;
			return;
		}
		r.bindings[r.regs] = b;
		r.bindingCounts[r.regs] = nb;
		r.places[r.regs] = p;
		r.placeCounts[r.regs] = np;
		++r.regs;
	}
};

inline auto FindBinding(const char* encounter) noexcept -> const Binding* {
	const Registry& r = Reg_();
	for (size_t i = 0; i < r.regs; ++i) {
		for (size_t j = 0; j < r.bindingCounts[i]; ++j) {
			if (std::strcmp(r.bindings[i][j].encounter, encounter) == 0) {
				return &r.bindings[i][j];
			}
		}
	}
	return nullptr;
}
// The level of an encounter, -1 when no file binds it
inline auto LevelOf(const char* encounter) noexcept -> int32_t {
	const Binding* b = FindBinding(encounter);
	return b != nullptr ? b->level : -1;
}
// The encounter bound to `level` (null: none; the first when two share a level)
inline auto BindingAt(int32_t level) noexcept -> const Binding* {
	const Registry& r = Reg_();
	for (size_t i = 0; i < r.regs; ++i) {
		for (size_t j = 0; j < r.bindingCounts[i]; ++j) {
			if (r.bindings[i][j].level == level) {
				return &r.bindings[i][j];
			}
		}
	}
	return nullptr;
}
// An Existing source is PD2's own level, whose DS1 and DT1 files the build no longer carries (#965): not finished, so
// a key for it is refused instead of opening a level with no preset (a crash in the open path).
inline auto Finished(int32_t level) noexcept -> bool {
	const Binding* b = BindingAt(level);
	return b != nullptr && b->source != Source::Existing;
}
inline auto IsUberLevel(int32_t level) noexcept -> bool {
	const Binding* b = BindingAt(level);
	return b != nullptr && b->uber && b->source != Source::Existing;
}
inline auto NoWaypointLevel(int32_t level) noexcept -> bool {
	const Binding* b = BindingAt(level);
	return b != nullptr && b->noWaypoint;
}

// A placement by encounter and id, null when absent
inline auto Find(const char* encounter, const char* id) noexcept -> const Place* {
	const Registry& r = Reg_();
	for (size_t i = 0; i < r.regs; ++i) {
		for (size_t j = 0; j < r.placeCounts[i]; ++j) {
			const Place& p = r.places[i][j];
			if (std::strcmp(p.encounter, encounter) == 0 && std::strcmp(p.id, id) == 0) {
				return &p;
			}
		}
	}
	return nullptr;
}
// The placement a consumer cannot run without: absent counts as missing (the status line shows it) and reads as 0, 0
inline auto Need(const char* encounter, const char* id) noexcept -> const Place& {
	static const Place none{"", "", 0, 0, 0, 0, 0};
	const Place* p = Find(encounter, id);
	if (p == nullptr) {
		Registry& r = Reg_();
		bool seen = false;
		for (uint32_t i = 0; i < r.missing && i < 16; ++i) {
			seen |= std::strcmp(r.missingEnc[i], encounter) == 0 && std::strcmp(r.missingId[i], id) == 0;
		}
		if (!seen) {
			if (r.missing < 16) {
				r.missingEnc[r.missing] = encounter;
				r.missingId[r.missing] = id;
			}
			++r.missing;
		}
		return none;
	}
	return *p;
}
inline auto SpotOf(const char* encounter, const char* id) noexcept -> Spot {
	const Place& p = Need(encounter, id);
	return {p.x, p.y};
}

}  // namespace placements
