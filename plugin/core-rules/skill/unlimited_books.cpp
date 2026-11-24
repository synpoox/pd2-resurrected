// Navigator/Almanac skill picker (#2124). D2R's item-skill resolvers reject the books' ubru type before consulting
// books.txt. Adding a scroll/book type changes the item save layout and hides existing characters, so leave the
// data alone. The SDK has no inventory-item skill grant/use service: extend the two native resolvers by item code.
// Server 93847 0x475840 feeds 0x475e00 (inventory add/remove/load, one level for a non-book with zero quantity).
// Client 0x36edd0 feeds packet 0x3f's inventory search (0x1c7a30), which sends the existing pSpell use message.
// All other items retain D2R's resolvers; existing spells 17/18 keep the unlimited books after use.
#include "common.h"
#include "registry.h"

#include "sig_hook.h"

#include <D2RLPlugin/item.h>

#include <cstdio>

namespace {

using ResolveFn = uint64_t (*)(d2r::Unit*) noexcept;
ResolveFn g_server = nullptr, g_client = nullptr;

constexpr auto UnlimitedSkill(uint32_t code) noexcept -> int32_t {
	return code == D2RL::Items::MakeItemCode("rtp") ? 583 :
	       code == D2RL::Items::MakeItemCode("rid") ? 582 : -1;
}
static_assert(UnlimitedSkill(D2RL::Items::MakeItemCode("rtp")) == 583);
static_assert(UnlimitedSkill(D2RL::Items::MakeItemCode("rid")) == 582);
static_assert(UnlimitedSkill(D2RL::Items::MakeItemCode("tbk")) == -1);
static_assert(UnlimitedSkill(D2RL::Items::MakeItemCode("isc")) == -1 && UnlimitedSkill(0) == -1);

auto ItemSkill(d2r::Unit* item) noexcept -> int32_t {
	return item ? UnlimitedSkill(d2r::Engine<uint32_t (*)(d2r::Unit*) noexcept>(d2r::sig::ItemCode)(item)) : -1;
}

auto ServerSkill(d2r::Unit* item) noexcept -> uint64_t {
	const int32_t skill = ItemSkill(item);
	return skill >= 0 ? skill : g_server(item);
}

auto ClientSkill(d2r::Unit* item) noexcept -> uint64_t {
	const int32_t skill = ItemSkill(item);
	return skill >= 0 ? skill : g_client(item);
}

constexpr const char* ServerPattern =
    "40 57 48 83 ec 20 48 8b f9 48 85 c9 75 13 88 4c 24 30 48 8d 4c 24 30 e8 ?? ?? ?? ?? 84 c0 74 01 cc ba 16 00 00 00";
constexpr uint8_t ServerEntry[] = {0x40, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x48, 0x85, 0xc9};
constexpr const char* ClientPattern =
    "48 89 5c 24 08 57 48 83 ec 20 ba 12 00 00 00 48 8b d9 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 48 85 db";
constexpr uint8_t ClientEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20,
                                    0xba, 0x12, 0x00, 0x00, 0x00};

}  // namespace

auto InstallUnlimitedBooks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool code = d2r::sig::ItemCode.state == sigscan::State::Ok;
	const bool client = code && sigscan::HookBySignature(context, "UnlimitedBookClientSkill", ClientPattern, 0,
	    ClientEntry, sizeof(ClientEntry), &ClientSkill, &g_client);
	// Enable grants only after skill-bar use is ready.
	const bool server = client && sigscan::HookBySignature(context, "UnlimitedBookServerSkill", ServerPattern, 0,
	    ServerEntry, sizeof(ServerEntry), &ServerSkill, &g_server);
	plog::Info("unlimited-books: inventory skill grant %s, skill-bar use %s", server ? "on" : "FAILED",
	           client ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; unlimited-books %s", server && client ? "on" : "FAILED");
	}
	return server && client;
}

PD2R_RULE(InstallUnlimitedBooks, 290)
