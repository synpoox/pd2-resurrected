// PD2's hireling rules (UR3, #201):
//
// - Tyrael hires the Ascendant (hireling ids 39 to 44, seller tyrael2). D2R gives his menu Talk, Travel To Harrogath
//   and Cancel, with no Hire, so before his menu opens Hire goes in after Talk, with the Hire handler Kashya's menu
//   uses. NpcMenuOpen then puts Resurrect before it when the merc is dead, as for Kashya. The server's NPC table
//   marks which NPCs hire; Tyrael's mark is set, so his hire list (hireling rows whose Seller is tyrael2) is sent.
// - Expanded merc inventory (#1627): every hireling wears an amulet, two rings, gloves, a belt and boots. D2R's "can a
//   hireling equip this" checks, the server's (D2Game ItemMode.cpp, 0x472b10, which the save load and the equip message
//   both reach) and the client's (0x159e60, the panel's), let a hireling wear any body armor (itemtypes row 3, tors)
//   or helm (37) and only the hireling's own weapon types otherwise. No SDK service covers this check, so the `mov
//   edx,3` of both armor tests becomes itemtypes row merc, a parent type of tors, ring, amul, glov, boot and belt
//   (files/data itemtypes Equiv2). The rest of each check (identified, not broken, class items) is D2R's own. The
//   server also refuses a body slot whose inventory.txt rect is -1, so the Hireling rows carry the players' rects.
// - Light Ascendant portrait (#1953): D2R's pet icon picker (D2Client UI/Widgets/HireablesPanel.cpp, 0x1e6560 on 93847,
//   by signature) matches the pet's monstats row against its pettype row's mclass1-4 and loads micon1-4, else baseicon,
//   into a sprite cache indexed pet type * 5 + slot. Hireable (pettype row 7, D2R's literal) uses all four slots, so the
//   Light Ascendant's icon has a pettype row of its own that no unit has: mclass1 the Light Ascendant, micon1
//   ruff_a4_light (found by that icon when the tables load). For a hireable the picker asks that row first and keeps its
//   answer when mclass1 matched; every other pet, and every other hireable, gets D2R's own answer. No SDK service picks
//   pet icons, and the DataTables views are read-only, so the picker is wrapped.
#include "merc_rules.h"

#include "pd2_engine.h"
#include "hot_log.h"
#include "log.h"
#include "sig.h"

#include "sig_hook.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/mutations.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
	++g_hireAdded;
}

auto MenuOpen(int32_t record, int32_t resurrect) noexcept -> uint64_t {
	if (record == d2r::rva::NpcMenuTyrael) {
		AddTyraelHire();
	}
	return g_menuOpenOriginal(record, resurrect);
}

// The pet icon picker (panel, pet type, unit id, bool* flag out, int* slot out) -> its sprite cache index, or -1 with no
// pettype row. The entry is whole instructions (push rbx..r15 / sub rsp,0x148); the pattern matches once on 93847.
constexpr char PetIconPattern[] =
	"40 53 55 56 57 41 56 41 57 48 81 ec 48 01 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 84 24 30 01 00 00 4c 8b b4 24 a0 01 00 00 49 8b d9";
constexpr uint8_t PetIconEntry[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x48, 0x01, 0x00, 0x00};
constexpr int32_t PetTypeHireable = 7;
constexpr char LightIcon[] = "ruff_a4_light";
// pettype record (field table 0x3019a0): micon1 char[32] at +0x33, mclass1 uint16 at +0xb6
constexpr uint32_t PetMIcon1 = 0x33, PetMClass1 = 0xb6;

using PetIconFn = int32_t (*)(void* panel, int32_t petType, int32_t unitId, bool* flag, int32_t* slot) noexcept;
PetIconFn g_petIconOriginal = nullptr;
const D2RL::PluginContext* g_context = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
std::atomic<int32_t> g_lightRow {-1};
std::atomic<uint32_t> g_lightIcons {0};

// Game thread, after each table load: the pettype row whose micon1 is the Light Ascendant's icon, the same row in every
// bank that has the table, else -1 (the picker is then D2R's alone).
void __cdecl FindLightRow(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	namespace dt = D2RL::DataTables;
	int32_t found = -1;
	bool agree = true;
	for (const dt::Bank bank : {dt::Bank::Classic, dt::Bank::Lod, dt::Bank::Rotw}) {
		dt::TableView view {.structSize = dt::TableViewSize};
		if (g_tables->getTable(g_context, bank, dt::TableId::PetType, &view) != dt::Result::Success ||
		    !dt::HasTableViewField(&view, dt::TableViewRequiredSize) || view.rows == nullptr || view.rowSize <= PetMClass1) {
			continue;
		}
		int32_t row = -1;
		for (uint32_t i = 0; i < view.rowCount && row < 0; ++i) {
			const auto* rec = static_cast<const uint8_t*>(view.rows) + static_cast<size_t>(i) * view.rowSize;
			if (static_cast<int32_t>(i) != PetTypeHireable && At<uint16_t>(rec, PetMClass1) != 0 &&
			    std::strncmp(reinterpret_cast<const char*>(rec + PetMIcon1), LightIcon, sizeof(LightIcon)) == 0) {
				row = static_cast<int32_t>(i);
			}
		}
		agree = agree && row >= 0 && (found < 0 || found == row);
		found = row;
	}
	g_lightRow.store(agree ? found : -1, std::memory_order_relaxed);
	plog::Info(agree && found >= 0 ? "light ascendant icon: pettype row %d" : "light ascendant icon: no pettype row (last %d), D2R's icon",
	           found);
}

auto PetIcon(void* panel, int32_t petType, int32_t unitId, bool* flag, int32_t* slot) noexcept -> int32_t {
	const int32_t light = g_lightRow.load(std::memory_order_relaxed);
	if (petType == PetTypeHireable && light >= 0) {
		bool lightFlag = false;
		int32_t lightSlot = 0;
		const int32_t index = g_petIconOriginal(panel, light, unitId, &lightFlag, &lightSlot);
		if (index >= 0 && lightSlot == 1) {  // mclass1 matched: the Light Ascendant
			if (flag != nullptr) {
				*flag = lightFlag;
			}
			if (slot != nullptr) {
				*slot = lightSlot;
			}
			++g_lightIcons;
			return index;
		}
	}
	return g_petIconOriginal(panel, petType, unitId, flag, slot);
}

auto InstallLightIcon(const D2RL::PluginContext* context) noexcept -> bool {
	g_context = context;
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::Lifecycle::DataTablesLoadedListener listener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                          .callback = FindLightRow};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	// The listener goes in first: with no hook, the row it finds is never read.
	return context->QueryService(&g_tables) == D2RL::ServiceQueryResult::Success &&
	       D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize) &&
	       context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success &&
	       D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) &&
	       lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success &&
	       sigscan::HookBySignature(context, "PetIcon", PetIconPattern, 0, PetIconEntry, sizeof(PetIconEntry), &PetIcon,
	                                &g_petIconOriginal);
}

// The armor test of each check: mov edx,3 / mov rcx,rdi / call ItemIsType / test eax,eax / jnz / lea edx,[rax+0x25]
// (the helm test). The entry points at the mov; the bytes before it pin the function (one match each on 93847).
sigscan::Entry ServerArmorTest {"MercEquipServer", "mercs", sigscan::Kind::Code, 10, 0,
                                "45 84 f6 4c 8b 74 24 50 74 31 ba 03 00 00 00 48 8b cf e8 ?? ?? ?? ?? 85 c0 75 0f 8d 50 25 48 8b cf e8"};
sigscan::Entry ClientArmorTest {"MercEquipClient", "mercs", sigscan::Kind::Code, 10, 0,
                                "3b c3 74 04 32 db eb 02 b3 01 ba 03 00 00 00 48 8b cf e8 ?? ?? ?? ?? 85 c0 0f 85 ?? ?? ?? ?? 8d 50 25 48 8b cf e8"};
constexpr uint8_t ArmorTest[] = {0xba, 0x03, 0x00, 0x00, 0x00};  // mov edx,3
constexpr uint8_t MercGearTest[] = {0xba, static_cast<uint8_t>(d2r::rva::ItemTypeMercGear), 0x00, 0x00, 0x00};
static_assert(d2r::rva::ItemTypeMercGear > 0 && d2r::rva::ItemTypeMercGear < 256);

// Both sites or neither (one MutationService transaction): a client that lets the drop through to a server that
// refuses it, or the reverse, would only confuse.
auto PatchMercGear(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Entry* const own[] = {&ServerArmorTest, &ClientArmorTest};
	sigscan::Resolve(own, 2, context->exeBase, nullptr, 0);
	for (const sigscan::Entry* entry : own) {
		if (entry->state != sigscan::State::Ok) {
			plog::Warn("skipped %s: %s", entry->name, sigscan::StateText(entry->state));
			return false;
		}
		plog::Info("found %s at 0x%llx", entry->name, static_cast<unsigned long long>(entry->rva));
	}
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	bool staged = true;
	for (const sigscan::Entry* entry : own) {
		const D2RL::Mutations::BytePatchRequest patch {.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                               .rva = entry->rva,
		                                               .expected = ArmorTest,
		                                               .expectedSize = sizeof(ArmorTest),
		                                               .bytes = MercGearTest,
		                                               .size = sizeof(MercGearTest)};
		D2RL::Mutations::OperationHandle operation = 0;
		staged = staged && mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	}
	D2RL::Mutations::CommitResult result {.structSize = D2RL::Mutations::CommitResultSize};
	if (!staged || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
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
	const bool icon = InstallLightIcon(context);
	std::snprintf(report, reportSize, "tyrael hire%s, light ascendant icon%s",
	              !table ? " FAILED (npc table differs)" : !hook ? " FAILED (bytes differ)"
	                                                     : seller ? "" : " FAILED (npc table write)",
	              icon ? "" : " FAILED (service, listener or signature)");
	return (seller ? 1 : 0) + (icon ? 1 : 0);
}

auto InstallMercGear(const D2RL::PluginContext* context) noexcept -> void {
	const bool gear = PatchMercGear(context);
	if (!gear) {
		// Without the patch D2R's load check deletes every ring, amulet, glove, belt and boot a merc wears (#1627
		// game check), and the next save makes that permanent. Stop before any character loads instead.
		plog::Error("merc gear patch missing: the game is closed so no save loses its mercenary's items");
		MessageBoxA(nullptr,
		            "PD2 Resurrected could not enable mercenary rings, amulets, gloves, belts and boots on this D2R "
		            "version.\n\nLoading a character now would delete those items from your mercenary, so the game "
		            "will close. Update PD2 Resurrected, or report this D2R version.",
		            "PD2 Resurrected", MB_OK | MB_ICONERROR | MB_TOPMOST);
		TerminateProcess(GetCurrentProcess(), 1);  // not ExitProcess: no DLL detach code runs while plugins load
	}
	plog::Info("merc gear installed");
}

auto MercRuleCount() noexcept -> int {
	return 2;
}

auto MercRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "tyrael hire added %u, light ascendant icon %u", g_hireAdded.load(), g_lightIcons.load());
}
