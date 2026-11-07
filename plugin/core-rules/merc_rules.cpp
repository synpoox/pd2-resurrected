// PD2's hireling rules (UR3, #201):
//
// - Tyrael hires the Ascendant (hireling ids 39 to 44, seller tyrael2). D2R gives his menu Talk, Travel To Harrogath
//   and Cancel, with no Hire, so before his menu opens Hire goes in after Talk, with the Hire handler Kashya's menu
//   uses. NpcMenuOpen then puts Resurrect before it when the merc is dead, as for Kashya. The server's NPC table
//   marks which NPCs hire; Tyrael's mark is set, so his hire list (hireling rows whose Seller is tyrael2) is sent.
#include "merc_rules.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::At;

std::atomic<uint32_t> g_hireAdded {0};

using MenuOpenFn = uint64_t (*)(int32_t record, int32_t resurrect) noexcept;
MenuOpenFn g_menuOpenOriginal = nullptr;

// Puts Hire after Talk in Tyrael's record, unless it has Hire already or no room. The per-NPC setter rewrites his
// record before each open, so this runs every time.
auto AddTyraelHire() noexcept -> void {
	using namespace d2r::rva;
	auto* menus = *reinterpret_cast<uint8_t**>(d2r::imageBase + NpcMenus);
	if (menus == nullptr || *reinterpret_cast<uint64_t*>(d2r::imageBase + NpcMenuCount) <= NpcMenuTyrael) {
		return;
	}
	uint8_t* rec = menus + NpcMenuTyrael * NpcMenuSize;
	auto& count = *reinterpret_cast<int32_t*>(rec + NpcMenuEntries);
	auto* strings = reinterpret_cast<uint16_t*>(rec + NpcMenuStrings);
	auto* handlers = reinterpret_cast<uint64_t*>(rec + NpcMenuHandlers);
	const uint64_t hire = d2r::imageBase + NpcMenuHire;
	if (count < 1 || count >= NpcMenuMaxEntries) {
		return;
	}
	for (int i = 0; i < count; ++i) {
		if (handlers[i] == hire) {
			return;
		}
	}
	const int at = count > 1 ? 1 : 0;  // after Talk, which npc.txt gives Tyrael
	for (int i = count - 1; i >= at; --i) {
		strings[i + 1] = strings[i];
		handlers[i + 1] = handlers[i];
	}
	strings[at] = StrNpcMenuHire;
	handlers[at] = hire;
	++count;
	strings[count - 1] = 0;  // Cancel, as npc.txt's records leave it
	handlers[count - 1] = 0;
	if (plog::Sample(g_hireAdded)) {
		plog::Info("tyrael hire: menu has %d entries, %u so far", count, g_hireAdded.load());
	}
}

auto MenuOpen(int32_t record, int32_t resurrect) noexcept -> uint64_t {
	if (record == d2r::rva::NpcMenuTyrael) {
		AddTyraelHire();
	}
	return g_menuOpenOriginal(record, resurrect);
}

}  // namespace

auto InstallMercRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const uint64_t tyrael = NpcTable + static_cast<uint64_t>(NpcTableTyrael) * NpcTableSize;
	static constexpr uint8_t NotHiring[] = {0};  // the byte D2R has; 1 is written
	const bool table = At<uint16_t>(reinterpret_cast<const void*>(d2r::imageBase + tyrael), 0) == MonTyrael2;
	// The hook goes in first: if it fails, Tyrael is left as D2R has him, not marked as hiring with no Hire entry.
	const bool hook = table && context->InstallInlineHook(NpcMenuOpen, NpcMenuOpenEntry, sizeof(NpcMenuOpenEntry),
	                                                      MenuOpen, &g_menuOpenOriginal);
	const bool seller = hook && context->PatchWriteU8(tyrael + NpcTableHires, NotHiring, sizeof(NotHiring), 1);
	std::snprintf(report, reportSize, "tyrael hire%s",
	              !table ? " FAILED (npc table differs)" : !hook ? " FAILED (bytes differ)"
	                                                     : seller ? "" : " FAILED (npc table write)");
	return seller ? 1 : 0;
}

auto MercRuleCount() noexcept -> int {
	return 1;
}

auto MercRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "tyrael hire added %u", g_hireAdded.load());
}
