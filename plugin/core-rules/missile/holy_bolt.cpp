// Holy Bolt, Fist of the Heavens' bolts and Holy Nova: PD2's srvhit 7 heal, here shit 70 (#2227).
// docs/research/skills/holy-bolt-heal.md has the decode of both games' functions.
#include "shared.h"

#include "../item_rules.h"        // StunOwner: PD2's 0x103749f0(unit, 1)
#include "../skill/registry.h"
#include "../skill_functions.h"  // AiOwnerGetter
#include "kick_base.h"           // CallTarget
#include "sig.h"

namespace missiles {
std::atomic<uint32_t> g_holyBoltHeals {0};
}  // namespace missiles

namespace {

// D2R 93847 srvhit 7 (0x45cb80) at 0x45cc18: its pet test 0x48e3a0 (E8 at +12) and party test 0x48e250 (E8 at +33),
// both (game, owner, target, skill) -> 0 / 1, the same tests as 1.13c's +0xe0210 and +0xe0660 that PD2 calls.
constexpr const char* AllyTests =
    "45 8b cc 4c 8b c5 48 8b d0 48 8b cb e8 ?? ?? ?? ?? 85 c0 75 ?? 45 8b cc 4c 8b c5 48 8b d6 48 8b cb e8";
constexpr uint32_t PetTestAt = 12, PartyTestAt = 33;
// The party test's (0x48e30e) call of the party id getter 0x506d40 (game, player) -> party id, 0xffff without one
// (1.13c's +0x9beb0, which +0xdfa60 wraps).
constexpr const char* PartyIdCall =
    "48 8b bb d8 00 00 00 48 85 ff 74 ?? 48 8b cb e8 ?? ?? ?? ?? 85 c0 75 ?? 48 8b d3 48 8b cf e8 ?? ?? ?? ?? "
    "0f b7 f0 b8 ff ff 00 00 66 3b f0";
constexpr uint32_t PartyIdAt = 30;

constexpr int StockSlot = 7;
constexpr int MsProgOverlay = 0x58;  // int16 ProgOverlay, as srvhit 7 reads it (1.13c's +0x36)
constexpr int32_t StatMinDamage = 21, StatMaxDamage = 22;
constexpr int32_t HealResult = 5;  // PD2's: hit effect (1) and no damage (4)

using AllyFn = int32_t (*)(Game*, Unit*, Unit*, int32_t) noexcept;
using PartyFn = uint16_t (*)(Game*, Unit*) noexcept;

MissileSrvHitFn g_stock = nullptr;  // D2R's srvhit 7: the hit when the target is not healed
AllyFn g_petTest = nullptr, g_partyTest = nullptr;
PartyFn g_partyId = nullptr;
AiOwnerFn g_aiOwner = nullptr;

sigscan::Entry* const Uses[] = {&d2r::sig::MissileRecord, &d2r::sig::MissileOwner, &d2r::sig::MissileSkill,
                                &d2r::sig::MissileLevel, &d2r::sig::GetSkillRecord, &d2r::sig::EvalSkillCalc,
                                &d2r::sig::UnitSeed, &d2r::sig::UnitStat, &d2r::sig::UnitMaxLife,
                                &d2r::sig::SetUnitStat, &d2r::sig::SetOverlay};

// PD2's third heal test compares 1.13c's +0xdfa60 of the caster's top owner and of the target as whole registers: a
// player gives its party id (0xffff without a party, so two players without a party match), any other unit 0xffff
// over its own pointer's high half, so it matches only itself.
// shortcut: two non-players whose pointers share their high 16 bits also matched in PD2 (a heap accident); not ported.
auto SameParty(Unit* a, Unit* b) noexcept -> bool {
	if (a == b) {
		return true;
	}
	Game* ga = At<Game*>(a, d2r::rva::UnitGame);
	Game* gb = At<Game*>(b, d2r::rva::UnitGame);
	return At<int32_t>(a, d2r::rva::UnitType) == 0 && At<int32_t>(b, d2r::rva::UnitType) == 0 && ga != nullptr &&
	       gb != nullptr && g_partyId(ga, a) == g_partyId(gb, b);
}

// 1.13c's +0x39b40 as PD2 calls it (0x103a3dd0(21, -1, missile, 22)): the missile's 21..22 roll, 0 when either is
// below 1. D2R's srvhit 7 inlines the same roll.
auto DamageRoll(Unit* missile, uint32_t* seed) noexcept -> int32_t {
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
	int32_t lo = stat(missile, StatMinDamage, 0), hi = stat(missile, StatMaxDamage, 0);
	if (lo < 1 || hi < 1) {
		return 0;
	}
	if (hi < lo) {
		std::swap(lo, hi);
	}
	return lo + static_cast<int32_t>(d2r::RandomBelow(seed, static_cast<uint32_t>(hi - lo)));
}

}  // namespace

// shit 70 (PD2 srvhit 7, ProjectDiablo.dll 0x103a7840). With sHitPar1 set, the target is healed when it is the
// caster's pet or party member (D2R's own two tests) or when the caster's top owner (0x103749f0(owner, 1)) and the
// target are in the same party, players without a party included. The heal is rnd[cltcalc2, cltcalc3) x 256 for the
// caster at the missile's skill and level (cltcalc3 at least cltcalc2 + 1), or the missile's 21..22 roll when that is
// below 1; life is clipped at max life, ProgOverlay is shown, and the result is 5. Any other target gets D2R's srvhit 7
// (0x45cb80), whose heal then cannot fire (its tests are the first two above) and whose damage results match PD2's for
// the three rows (sHitPar2 0, sHitPar3 0). D2R's own heal read calc1..calc2 (calc1 is the bolt count).
// shortcut: D2R's non-heal results differ from PD2's for sHitPar2 1-3 or sHitPar3 2-3; port them if a row needs those.
auto SrvHitHolyBolt(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || hit == nullptr || g_stock == nullptr) {
		return 1;
	}
	if (g_petTest == nullptr) {  // a signature missed: D2R's srvhit 7 as before
		return g_stock(game, missile, hit);
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::MissileRecord)(
	    dataFlag, At<int32_t>(missile, UnitClassId));
	const int32_t skill = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::MissileSkill)(missile);
	const auto* skillRow = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::GetSkillRecord)(dataFlag, skill);
	if (row == nullptr || skillRow == nullptr) {
		return 1;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::sig::MissileOwner)(game, missile);
	if (owner == nullptr) {
		owner = missile;  // PD2's 0x103749f0(missile, 0)
	}
	const bool heal = At<int32_t>(row, MsHitPar1) != 0 &&
	                  (g_petTest(game, owner, hit, skill) != 0 || g_partyTest(game, owner, hit, skill) != 0 ||
	                   SameParty(StunOwner(owner, g_aiOwner), hit));
	if (!heal) {
		return g_stock(game, missile, hit);
	}
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::MissileLevel)(missile);
	using EvalFn = int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept;
	const auto eval = d2r::Engine<EvalFn>(d2r::sig::EvalSkillCalc);
	const int32_t lo = eval(dataFlag, owner, At<int32_t>(skillRow, SkCltCalc2), skill, level);
	int32_t hi = eval(dataFlag, owner, At<int32_t>(skillRow, SkCltCalc3), skill, level);
	if (hi <= lo) {
		hi = lo + 1;
	}
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::sig::UnitSeed)(missile);
	const uint32_t roll = d2r::RandomBelow(seed, static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo));
	int32_t amount = static_cast<int32_t>((static_cast<uint32_t>(lo) + roll) << 8);  // PD2's 32-bit shl
	if (amount < 1) {
		amount = DamageRoll(missile, seed);
	}
	const int32_t life = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(hit, StatLife, 0);
	const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitMaxLife)(hit);
	const int64_t sum = static_cast<int64_t>(life) + amount;
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat)(
	    hit, StatLife, static_cast<int32_t>(sum < max ? sum : max), 0);
	const int16_t overlay = At<int16_t>(row, MsProgOverlay);
	if (overlay > 0) {  // PD2 tests != 0; D2R's SetOverlay is given no negative row
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SetOverlay)(hit, overlay, 0);
	}
	++g_holyBoltHeals;
	return HealResult;
}

auto InstallHolyBoltHeal(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_stock = *reinterpret_cast<MissileSrvHitFn*>(context->exeBase + d2r::rva::MissileSrvHitTable + StockSlot * 8);
	g_aiOwner = AiOwnerGetter();
	bool ok = g_stock != nullptr && g_aiOwner != nullptr;
	for (const sigscan::Entry* e : Uses) {
		ok = ok && e->state == sigscan::State::Ok;
	}
	const uint64_t pet = kick_base::CallTarget(context->exeBase, AllyTests, PetTestAt);
	const uint64_t party = kick_base::CallTarget(context->exeBase, AllyTests, PartyTestAt);
	const uint64_t partyId = kick_base::CallTarget(context->exeBase, PartyIdCall, PartyIdAt);
	ok = ok && pet != 0 && party != 0 && partyId != 0;
	if (ok) {
		g_partyTest = reinterpret_cast<AllyFn>(party);
		g_partyId = reinterpret_cast<PartyFn>(partyId);
		g_petTest = reinterpret_cast<AllyFn>(pet);  // last: SrvHitHolyBolt tests it
	}
	plog::Info("holy bolt: pd2 heal %s", ok ? "on (shit 70)" : "FAILED (srvhit 7 signatures), D2R's srvhit 7 kept");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; holy bolt heal%s", ok ? "" : " FAILED");
	}
	return ok;
}

PD2R_RULE(InstallHolyBoltHeal, 390)
PD2R_SLOT(69, MissileSrvHit, 70, static_cast<MissileSrvHitFn>(SrvHitHolyBolt), "holy bolt heal")
