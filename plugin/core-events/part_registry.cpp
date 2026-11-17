// The parts registry (part_registry.h). Function-local statics: registration runs in static initialisers, in no fixed order.
#include "part_registry.h"

#include <utility>

namespace {

constexpr size_t Capacity = 64;

struct Counter {
	int order;
	CountersFn fn;
};

struct Registry {
	parts::Part parts[Capacity] = {};
	int partOrder[Capacity] = {};
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
		for (size_t j = i; j > 0 && r.partOrder[j - 1] > r.partOrder[j]; --j) {
			std::swap(r.parts[j], r.parts[j - 1]);
			std::swap(r.partOrder[j], r.partOrder[j - 1]);
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

PartReg::PartReg(int order, const char* word, parts::InstallFn install, std::span<sigscan::Entry* const> uses) noexcept {
	Registry& r = Reg();
	if (r.partCount == Capacity) {
		r.overflow = true;
		return;
	}
	// engine: every part reads game addresses; with `uses` the entries gate it, without it the image does
	r.partOrder[r.partCount] = order;
	r.parts[r.partCount++] = parts::Part {word, true, install, nullptr, uses};
}

CountersReg::CountersReg(int order, CountersFn fn) noexcept {
	Registry& r = Reg();
	if (r.counterCount == Capacity) {
		r.overflow = true;
		return;
	}
	r.counters[r.counterCount++] = Counter {order, fn};
}

auto Parts(size_t* count) noexcept -> parts::Part* {
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
