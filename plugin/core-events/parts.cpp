// The parts registry (parts.h). Function-local statics: registration runs in static initialisers, in no fixed order.
#include "parts.h"

#include "off_switch.h"

#include <cstdio>

namespace {

constexpr size_t Capacity = 64;

struct Counter {
	int order;
	CountersFn fn;
};

struct Registry {
	Part parts[Capacity] = {};
	size_t partCount = 0;
	Counter counters[Capacity] = {};
	size_t counterCount = 0;
	CountersFn sorted[Capacity] = {};
	bool overflow = false;
	bool isSorted = false;
};

auto Reg() noexcept -> Registry& {
	static Registry r;
	return r;
}

void Sort() noexcept {
	Registry& r = Reg();
	if (r.isSorted) {
		return;
	}
	for (size_t i = 1; i < r.partCount; ++i) {  // insertion sort: stable, tiny
		for (size_t j = i; j > 0 && r.parts[j - 1].order > r.parts[j].order; --j) {
			const Part t = r.parts[j];
			r.parts[j] = r.parts[j - 1];
			r.parts[j - 1] = t;
		}
	}
	for (size_t i = 1; i < r.counterCount; ++i) {
		for (size_t j = i; j > 0 && r.counters[j - 1].order > r.counters[j].order; --j) {
			const Counter t = r.counters[j];
			r.counters[j] = r.counters[j - 1];
			r.counters[j - 1] = t;
		}
	}
	for (size_t i = 0; i < r.counterCount; ++i) {
		r.sorted[i] = r.counters[i].fn;
	}
	r.isSorted = true;
}

}  // namespace

PartReg::PartReg(int order, const char* word, InstallFn install, std::span<sigscan::Entry* const> uses) noexcept {
	Registry& r = Reg();
	if (r.partCount == Capacity) {
		r.overflow = true;
		return;
	}
	r.parts[r.partCount++] = Part {word, install, "", order, uses};
}

// The same rule as plugin-common's parts::SkipReason (its parts.h cannot be included here: the name clashes with this
// file's parts.h): the off word, then every entry of `uses` must have resolved.
auto UsesSkipReason(const Part& part, const char* pluginName, char* out, size_t size) noexcept -> bool {
	if (off_switch::Off(part.word)) {
		std::snprintf(out, size, "off: left out by %s-off.txt", pluginName);
		return true;
	}
	for (const sigscan::Entry* e : part.uses) {
		if (e == nullptr || e->state != sigscan::State::Ok) {
			std::snprintf(out, size, "off: engine address %s %s on this game image", e != nullptr ? e->name : "(null)",
			              sigscan::StateText(e != nullptr ? e->state : sigscan::State::Unresolved));
			return true;
		}
	}
	return false;
}

CountersReg::CountersReg(int order, CountersFn fn) noexcept {
	Registry& r = Reg();
	if (r.counterCount == Capacity) {
		r.overflow = true;
		return;
	}
	r.counters[r.counterCount++] = Counter {order, fn};
}

auto Parts(size_t* count) noexcept -> Part* {
	Sort();
	*count = Reg().partCount;
	return Reg().parts;
}

auto Counters(size_t* count) noexcept -> const CountersFn* {
	Sort();
	*count = Reg().counterCount;
	return Reg().sorted;
}

auto RegistryOverflowed() noexcept -> bool {
	return Reg().overflow;
}
