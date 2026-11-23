// Monster curse cast (#2003): PD2's srvdo 112 (ProjectDiablo.dll 0x103b02d0, PD2.gpr; 0x103b0340 installed; Oblivion
// Knights and the void knights) picks unit rand & 3 from {MonAmplifyDamage, MonWeaken, MonLowerRes, MonDecrepify}
// (PD2 raw 424, 420, 585, 427 at 0x10450600) and fills 1.13c's curse context from that row: auralencalc frames,
// aurarangecalc radius, auratargetstate, and per pick fixed stats valued by aurastatcalc1-5 at the cast level
// (decrepify also sets the context's +0xc flag), then runs the stock area curse callback (1.13c D2Game +0x50250).
// D2R's srvdo 112 (93847 0x5794b0) picks rand % 5 from the player curses at 0x1d3e798 (66, 72, 82, 87, 91) and fills
// fixed stat values per pick before the same area call (0x434390, callback 0x55a7e0, context = CurseParams). Its fill
// is per index, so a list swap alone would put Life Tap's fill on MonLowerRes: the area call is redirected (no SDK
// service casts a skill's area apply) and the context refilled from PD2's pick. The rows are found by PD2's names
// through D2R's skills name map (DataTables +0x11c8, the map 0x49fba0 looks "FireGolem" up in), as the merge moved them.
#include "common.h"
#include "registry.h"

#include "../item_rules.h"
#include "pd2_engine.h"
#include "hot_log.h"
#include "log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// D2R's srvdo 112 area call: lea rax,[rbp-0x59] (the context) ... lea rax,<callback> / mov edx,3 ... call 0x434390
constexpr const char* AreaCall =
    "48 8d 45 a7 44 8b ce 48 89 44 24 28 4d 8b c7 48 8d 05 ?? ?? ?? ?? ba 03 00 00 00 49 8b ce 48 89 44 24 20 e8 ?? ?? ?? ?? "
    "eb 02 33 c0";
constexpr uint32_t AreaCallAt = 35;
constexpr int TablesSkillNames = 0x11c8;  // skills name map in DataTables (93847 0x49fc73), for TcIndexByName
constexpr const char* Curses[] = {"MonAmplifyDamage", "MonWeaken", "MonLowerRes", "MonDecrepify"};
constexpr int Decrepify = 3;
// PD2's stat for each aurastatcalc, as the row's aurastat slot holding that stat: lower resist puts calc2 on coldresist
// (aurastat3) and calc3 on lightresist (aurastat2); decrepify calc2 on item_fastercastrate (3), calc3 on attackrate (2)
constexpr int StatSlot[4][5] = {{0, -1, -1, -1, -1}, {0, -1, -1, -1, -1}, {0, 2, 1, 3, -1}, {0, 2, 1, 3, 4}};

using AreaFn = int64_t (*)(Game*, int32_t, Unit*, int32_t, void*, d2r::CurseParams*) noexcept;
using NameIndexFn = int32_t (*)(void*, const char*, int32_t) noexcept;
using TablesFn = const uint8_t* (*)(uint8_t) noexcept;
AreaFn g_area = nullptr;
std::atomic<bool> g_logged {false};
std::atomic<uint32_t> g_casts {0};

// The skills row of `name` in this bank, < 1 when missing (the map's answer, as 0x49fba0 reads it)
auto SkillByName(const uint8_t* tables, const char* name) noexcept -> int32_t {
	return d2r::Engine<NameIndexFn>(d2r::sig::TcIndexByName)(At<void*>(tables, TablesSkillNames), name, 0);
}

// D2R's pick and fill ran first (its row checks pass: player curses have a target state); the cast is redone here.
auto Area(Game* game, int32_t mode, Unit* unit, int32_t range, void* callback, d2r::CurseParams* c) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr || c == nullptr) {
		return g_area(game, mode, unit, range, callback, c);
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* tables = d2r::Engine<TablesFn>(d2r::sig::DataTables)(static_cast<uint8_t>(dataFlag));
	if (tables == nullptr) {
		return 0;
	}
	if (!g_logged.exchange(true)) {
		plog::Info("monster curse: skills %s %d, %s %d, %s %d, %s %d", Curses[0], SkillByName(tables, Curses[0]), Curses[1],
		           SkillByName(tables, Curses[1]), Curses[2], SkillByName(tables, Curses[2]), Curses[3],
		           SkillByName(tables, Curses[3]));
	}
	const int pick = static_cast<int>(d2r::RandomBelow(d2r::api.unitSeed(unit), 4));
	const int32_t skillId = SkillByName(tables, Curses[pick]);
	const uint8_t* skill = skillId >= 1 ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	const int16_t state = skill != nullptr ? At<int16_t>(skill, SkAuraTargetState) : -1;
	if (state < 0 || static_cast<uint64_t>(state) >= At<uint64_t>(tables, TablesStateCount)) {
		return 0;  // PD2's exit for a missing row or state: no cast
	}
	const int32_t level = c->level;
	c->skill = skillId;
	c->pad10[1] = pick == Decrepify ? 1 : 0;
	c->frames = d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	for (int i = 0; i < 6; ++i) {
		const int slot = i < 5 ? StatSlot[pick][i] : -1;
		const int16_t stat = slot < 0 ? -1 : At<int16_t>(skill, SkAuraStat1 + 2 * slot);
		c->stats[i] = stat < 0 ? -1 : stat;
		c->values[i] = stat < 0 ? 0 : d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1 + 4 * i, skillId, level);
	}
	c->state = state;
	std::memset(reinterpret_cast<uint8_t*>(c) + sizeof(d2r::CurseParams), 0, 0x10);  // Life Tap's events, 0 elsewhere
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkAuraRangeCalc, skillId, level);
	if (const uint32_t n = ++g_casts; pd2::HotSample(n)) {
		plog::Info("monster curse: skill %d level %d, state %d, %d frames, radius %d, stat %d = %d (%u so far)", skillId,
		           level, c->state, c->frames, radius, c->stats[0], c->values[0], n);
	}
	return g_area(game, mode, unit, radius, callback, c);
}

}  // namespace

auto InstallMonsterCurse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = d2r::api.unitSeed != nullptr && d2r::api.getSkillRecord != nullptr &&
	                d2r::sig::DataTables.state == sigscan::State::Ok && d2r::sig::TcIndexByName.state == sigscan::State::Ok &&
	                RedirectCall(context, AreaCall, AreaCallAt, reinterpret_cast<uint64_t>(&Area),
	                             reinterpret_cast<uint64_t*>(&g_area));
	plog::Info("monster curse: pd2 list %s", on ? "on (ids by name at the first cast)"
	                                            : "FAILED (srvdo 112 area call signature, name map or unit seed)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; monster curse list%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallMonsterCurse, 10)
