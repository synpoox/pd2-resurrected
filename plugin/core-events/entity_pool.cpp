// tlsf_add_pool on D2R's Entity System Pool and Renderer Pool, written the way 0x1215a30 adds the first pool (pd2_engine.h).
#include "entity_pool.h"

#include "pd2_engine.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t BlockOverhead = 0x10;  // tlsf: the pool's first block header and the sentinel's size word

struct Grown {
	const char* name;
	uint64_t rva;
	uint64_t extraBytes;
	bool spinLock;  // +0x28 is a spinlock (the Jobs pool), not a lock the engine's lock functions take
	char state[100];
};

Grown g_pools[] = {
	{"entity", d2r::rva::EntityPool, 2048ull << 20, false, "waiting"},     // D2R's own: 70 MiB
	{"renderer", d2r::rva::RendererPool, 512ull << 20, false, "waiting"},  // D2R's own: 12 MiB
	{"json", d2r::rva::JsonPool, 1024ull << 20, false, "waiting"},         // D2R's own: 40 MiB
	{"object", d2r::rva::ObjectPool, 512ull << 20, false, "waiting"},      // D2R's own: 50 MiB
	{"jobs", d2r::rva::JobsPool, 512ull << 20, true, "waiting"},           // D2R's own: 10 MiB
	{"texture", d2r::rva::TexturePool, 512ull << 20, false, "waiting"},    // D2R's own: 24 MiB
};
char g_summary[400] = "";

auto Lock(uint8_t* pool, bool spin) noexcept -> void {
	if (!spin) {
		d2r::Engine<void (*)(void*) noexcept>(d2r::rva::EntityPoolLock)(pool + 0x28);
		return;
	}
	auto* lock = reinterpret_cast<volatile LONG64*>(pool + 0x28);
	while (InterlockedCompareExchange64(lock, reinterpret_cast<LONG64>(lock), 0) != 0) {
		YieldProcessor();
	}
}

auto Unlock(uint8_t* pool, bool spin) noexcept -> void {
	if (!spin) {
		d2r::Engine<void (*)(void*) noexcept>(d2r::rva::EntityPoolUnlock)(pool + 0x28);
		return;
	}
	InterlockedExchange64(reinterpret_cast<volatile LONG64*>(pool + 0x28), 0);
}

auto AddPool(uint8_t* pool, void* control, uint64_t extraBytes, bool spin, char* state, size_t stateSize) noexcept
	-> void {
	auto* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, extraBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (mem == nullptr) {
		std::snprintf(state, stateSize, "off: VirtualAlloc of %llu MiB failed", extraBytes >> 20);
		return;
	}
	const uint64_t size = (extraBytes - BlockOverhead) & ~7ull;
	uint8_t* block = mem - 8;  // tlsf blocks start at prev_phys, 8 bytes before the size word (never read: no block before)
	Lock(pool, spin);
	*reinterpret_cast<uint64_t*>(mem) = size | 1;  // free; the block before it (none) counts as used
	d2r::Engine<void (*)(void*, void*) noexcept>(d2r::rva::TlsfBlockInsert)(control, block);
	*reinterpret_cast<uint8_t**>(mem + size) = block;  // sentinel: prev_phys, then size 0 with prev-free set
	*reinterpret_cast<uint64_t*>(mem + size + 8) = 2;
	Unlock(pool, spin);
	std::snprintf(state, stateSize, "on: +%llu MiB", extraBytes >> 20);
}

// The plugin loads before D2R makes its pools: one thread per pool waits for its control pointer (set after the
// first pool is in).
auto WINAPI Waiter(void* arg) -> DWORD {
	Grown& g = *static_cast<Grown*>(arg);
	auto* pool = reinterpret_cast<uint8_t*>(d2r::imageBase + g.rva);
	auto* const volatile* control = reinterpret_cast<void* const volatile*>(pool + 0x20);
	for (int i = 0; i < 6000 && *control == nullptr; ++i) {
		Sleep(50);
	}
	if (*control == nullptr) {
		std::snprintf(g.state, sizeof(g.state), "off: not made after 5 min");
	} else {
		AddPool(pool, *control, g.extraBytes, g.spinLock, g.state, sizeof(g.state));
	}
	return 0;
}

}  // namespace

auto InstallEntityPool(char* report, size_t reportSize) noexcept -> void {
	int started = 0;
	for (Grown& g : g_pools) {
		HANDLE thread = CreateThread(nullptr, 0, Waiter, &g, 0, nullptr);
		if (thread == nullptr) {
			std::snprintf(g.state, sizeof(g.state), "off: CreateThread failed");
			continue;
		}
		CloseHandle(thread);
		++started;
	}
	std::snprintf(report, reportSize, "%d waiting", started);
}

auto EntityPoolState() noexcept -> const char* {
	int n = 0;
	for (const Grown& g : g_pools) {
		n += std::snprintf(g_summary + n, sizeof(g_summary) - n, "%s%s %s", n ? ", " : "", g.name, g.state);
	}
	return g_summary;
}
