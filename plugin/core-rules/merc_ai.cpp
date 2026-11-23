// PD2's changes to the mercenary AI (#1803): monai row 61 Hireable, every hireling. ProjectDiablo.dll writes its own
// think (0x1035a270) into the AI table; it runs a few rules, then jumps to 1.13c's Hireable think (D2Game +0x1dd40),
// which its patch table patches in eight places. D2R's live think (0x5beac0, AI table row 61) is the same code as
// 1.13c's (melee vs ranged from monstats aip1), so each 1.13c site has one D2R site. Ported here:
// - PD2's pre-think Battle Orders (0x10356bc0): a merc with Battle Orders (hireling ids 25/27/29) casts it whenever it or
//   its owner lacks the BO state and the owner is within 30 subtiles, before the rest of the think. An inline hook on
//   D2R's think entry: no SDK service runs inside an AI think.
// - PD2's attack call (1.13c +0x1e05d -> 0x10360fd0): before the stock attack (D2R 0x5bf6e0), an act5hire2 merc with
//   MonWhirlwind whirls at a target 6 to 19 subtiles away, and an act4hire merc with A4 AmpDmg curses a target without
//   amplifydamage on 25% of attacks. The think's one call is redirected (RedirectCall).
// - 1.13c +0x1e000 jge -> jmp: no 6/128 (12/128 ranged) roll that skips the target search in the think's 0x40-collision
//   branch. D2R's jc at 0x5bee23 becomes a 2-byte nop (MutationService).
// - PD2's aura restart (think 0x1035a270 at 0x1035a31f, #1925), before the Battle Orders pre-cast: a merc whose
//   hireling.txt row has Mode1 1, at Skill1's reqlevel and without its aurastate, restarts Skill1 through D2R's aura
//   start (0x438a70; PD2's copy 0x10371810, same args) and idles 10 frames. The row is read as D2R's skill pick
//   (0x5bf070) reads it: the owner's pet record (0x4ff2d0) by the merc's id, then hireling.txt by id and level
//   (0x3965d0, game info 0x48ff40), all found by signature. Native calls: no SDK service reads hireling rows.
// Not ported: its 5% party and pet buffing (#1925, stopped: a game-frame window, not a roll), and the follow
// rework (1.13c +0x1deb3, +0x1ded6, +0x1defb, +0x1e0eb, +0x1e11c; #1926), whose replacement is not in the think.
// PD2 keys its rules on its own hireling ids and skill ids; ours are renumbered, so these key on the merc's skills and
// monstats class. A merc without the skill (a Classic hireling row, another merc type) keeps D2R's think.
#include "merc_ai.h"

#include "hot_log.h"
#include "item_rules.h"
#include "log.h"
#include "monster_count.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// files/data: skills.txt ids, hireling.txt modes of those skills, monstats classes (PD2: skill 425, classes 0x231, 0x420)
constexpr int32_t SkillBattleOrders = 149, SkillMonWhirlwind = 495, SkillA4AmpDmg = 621;
constexpr uint8_t ModeBattleOrders = 5, ModeWhirlwind = 14, ModeA4AmpDmg = 7;
constexpr int32_t ClassAct5Hire2 = 561, ClassAct4Hire = 1073;
constexpr int32_t StatLevel = 12, StateAmplifyDamage = 9;
constexpr int32_t BuffRange = 30;                   // PD2: owner closer than 0x1e
constexpr int32_t WhirlMin = 6, WhirlMax = 19;       // PD2: distance - 6 <= 0xd
constexpr uint32_t AmpChance = 25;                   // PD2: rand() % 100 < 0x19
constexpr uint32_t SkInTown = 0x100;                 // skills record +0x24: +0x25 bit 0
constexpr int HireSkill1 = 0x90, HireMode1 = 0xd8;   // D2R hireling record: int32 Skill1-6, uint8 Mode1-6 (0x5bf070)
constexpr int32_t AuraIdle = 10;                     // PD2: 0x103a1910(game, merc, 10)

using ThinkFn = void (*)(Game*, Unit* merc, uint8_t* tick) noexcept;
using OwnerFn = Unit* (*)(Unit* merc) noexcept;
// (game, merc, class, owner, target, seed, tick, aip block): D2R's attack, 0x5bf6e0
using AttackFn = void (*)(Game*, Unit* merc, int32_t cls, Unit* owner, Unit* target, uint32_t* seed, uint8_t* tick,
                          int16_t* aip) noexcept;
using UseSkillFn = int32_t (*)(Game*, Unit*, uint8_t mode, int32_t skill, Unit* target, int32_t x, int32_t y, uint8_t) noexcept;
using HasStateFn = int32_t (*)(Unit*, int32_t state) noexcept;
using DistanceFn = int32_t (*)(Unit*, Unit*) noexcept;
using UnitSkillFn = void* (*)(Unit*, int32_t skill, uint8_t) noexcept;
using PetRecordFn = const int32_t* (*)(Unit* owner, int32_t unitId) noexcept;  // 0x4ff2d0: [2] the hireling id
using GameInfoFn = uint8_t* (*)(uint8_t* out, Game*, Unit*) noexcept;          // 0x48ff40: out[1] 1 = classic rows
using HirelingRowFn = const uint8_t* (*)(uint32_t bank, int32_t expansion, int32_t id, int32_t level) noexcept;
using AuraStartFn = void (*)(Unit*, int32_t zero, int32_t skill, int32_t minusOne) noexcept;  // 0x438a70

ThinkFn g_thinkOriginal = nullptr;
AttackFn g_attackOriginal = nullptr;
OwnerFn g_owner = nullptr;
PetRecordFn g_petRecord = nullptr;
GameInfoFn g_gameInfo = nullptr;
HirelingRowFn g_hirelingRow = nullptr;
AuraStartFn g_auraStart = nullptr;  // null: no aura restart

std::atomic<uint32_t> g_thinks {0}, g_auras {0}, g_battleOrders {0}, g_whirlwinds {0}, g_amps {0}, g_attacks {0};

auto UseSkill(Game* game, Unit* merc, uint8_t mode, int32_t skill, Unit* target) noexcept -> bool {
	return d2r::Engine<UseSkillFn>(d2r::sig::AiUseSkill)(game, merc, mode, skill, target, 0, 0, 0) != 0;
}
auto HasState(Unit* unit, int32_t state) noexcept -> bool {
	return d2r::Engine<HasStateFn>(d2r::sig::UnitHasState)(unit, state) != 0;
}
auto Distance(Unit* a, Unit* b) noexcept -> int32_t {
	return d2r::Engine<DistanceFn>(d2r::sig::UnitDistance)(a, b);
}
auto HasSkill(Unit* unit, int32_t skill) noexcept -> bool {
	return d2r::Engine<UnitSkillFn>(d2r::sig::GetUnitSkill)(unit, skill, 0) != nullptr;
}

// PD2's 0x10356bc0 for its owner and itself, after its room guard (0x1037f6b0): no cast without a room, nor in town
// unless the skill has InTown (skills record +0x24 bit 0x100).
auto BattleOrders(Game* game, Unit* merc, Unit* owner, int32_t* distance) noexcept -> bool {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(merc);
	if (room == nullptr || owner == nullptr || At<int32_t>(owner, d2r::rva::UnitType) != 0 || !HasSkill(merc, SkillBattleOrders)) {
		return false;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), SkillBattleOrders);
	if (skill == nullptr || d2r::api.unitStat(merc, StatLevel, 0) < At<uint16_t>(skill, d2r::rva::SkReqLevel)) {
		return false;
	}
	if ((At<uint32_t>(skill, d2r::rva::SkFlags24) & SkInTown) == 0 &&
	    pd2::monstercount::IsTown(d2r::Engine<int32_t (*)(void*) noexcept>(d2r::sig::RoomLevelId)(room))) {
		return false;
	}
	const int32_t state = At<int16_t>(skill, d2r::rva::SkAuraState);
	if (state <= 0 || (HasState(merc, state) && HasState(owner, state))) {
		return false;
	}
	*distance = Distance(merc, owner);
	return *distance < BuffRange && UseSkill(game, merc, ModeBattleOrders, SkillBattleOrders, nullptr);
}

// PD2's aura restart (0x1035a270 from 0x1035a2d8): the owner is a player, the merc's pet record and hireling row
// exist, Mode1 is 1, the merc is at Skill1's reqlevel and lacks its aurastate. PD2 idles 10 frames whatever the start did.
auto AuraRestart(Game* game, Unit* merc, Unit* owner) noexcept -> bool {
	if (g_auraStart == nullptr || At<int32_t>(owner, d2r::rva::UnitType) != 0) {
		return false;
	}
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(merc, __FILE__, __LINE__);
	const int32_t* pet = g_petRecord(owner, id);
	if (pet == nullptr) {
		return false;
	}
	uint8_t info[16] {};
	g_gameInfo(info, game, merc);
	const int32_t level = d2r::api.unitStat(merc, StatLevel, 0);
	const uint8_t* row = g_hirelingRow(d2r::DataFlag(game), info[1] != 1 ? 1 : 0, pet[2], level);  // as 0x5bf127 passes it
	if (row == nullptr || At<uint8_t>(row, HireMode1) != 1) {
		return false;
	}
	const int32_t skillId = At<int32_t>(row, HireSkill1);
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr || level < At<uint16_t>(skill, d2r::rva::SkReqLevel)) {
		return false;
	}
	const int32_t state = At<int16_t>(skill, d2r::rva::SkAuraState);
	if (state <= 0 || HasState(merc, state)) {  // every Mode1 row's skill has an aurastate
		return false;
	}
	g_auraStart(merc, 0, skillId, -1);
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::sig::AiIdle)(game, merc, AuraIdle);
	return true;
}

auto MercThink(Game* game, Unit* merc, uint8_t* tick) noexcept -> void {
	const uint32_t n = ++g_thinks;
	Unit* owner = merc != nullptr ? g_owner(merc) : nullptr;
	int32_t distance = -1;
	const char* rule = nullptr;
	if (owner != nullptr && AuraRestart(game, merc, owner)) {
		++g_auras;
		rule = "PD2 aura restart";
	} else if (owner != nullptr && BattleOrders(game, merc, owner, &distance)) {
		++g_battleOrders;
		rule = "PD2 Battle Orders cast";
	}
	if (pd2::HotSample(n)) {
		plog::Info("merc think %u: class %d, owner %s, %s", n, merc != nullptr ? At<int32_t>(merc, d2r::rva::UnitClassId) : -1,
		           owner != nullptr ? "yes" : "no", rule != nullptr ? rule : "D2R think");
	}
	if (rule == nullptr) {
		g_thinkOriginal(game, merc, tick);
	}
}

// PD2's 0x10360fd0 (PD2.gpr 0x10360f60): it returns once it issued the skill, whatever the skill command did.
auto MercAttack(Game* game, Unit* merc, int32_t cls, Unit* owner, Unit* target, uint32_t* seed, uint8_t* tick,
                int16_t* aip) noexcept -> void {
	const uint32_t n = ++g_attacks;
	const char* rule = nullptr;
	if (target != nullptr && merc != nullptr) {
		if (cls == ClassAct5Hire2 && HasSkill(merc, SkillMonWhirlwind)) {
			*reinterpret_cast<Unit**>(tick + d2r::rva::AiTickTarget) = target;
			const int32_t d = Distance(merc, target);
			if (d >= WhirlMin && d <= WhirlMax) {
				UseSkill(game, merc, ModeWhirlwind, SkillMonWhirlwind, target);
				++g_whirlwinds;
				rule = "PD2 Whirlwind";
			}
		} else if (cls == ClassAct4Hire && HasSkill(merc, SkillA4AmpDmg)) {
			*reinterpret_cast<Unit**>(tick + d2r::rva::AiTickTarget) = target;
			// shortcut: PD2 rolls the CRT's rand(); the merc's seed gives the same odds
			if (!HasState(target, StateAmplifyDamage) && seed != nullptr && d2r::RandomBelow(seed, 100) < AmpChance) {
				UseSkill(game, merc, ModeA4AmpDmg, SkillA4AmpDmg, target);
				++g_amps;
				rule = "PD2 A4 AmpDmg";
			}
		}
	}
	if (pd2::HotSample(n)) {
		plog::Info("merc attack %u: class %d, %s", n, cls, rule != nullptr ? rule : "D2R attack");
	}
	if (rule == nullptr) {
		g_attackOriginal(game, merc, cls, owner, target, seed, tick, aip);
	}
}

// push rbp / push rdi / push r15 / sub rsp,0x80 / mov rdi,rdx
constexpr uint8_t ThinkEntry[] = {0x40, 0x55, 0x57, 0x41, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xfa};
constexpr const char* ThinkPattern =
	"40 55 57 41 57 48 81 EC 80 00 00 00 48 8B FA 4D 8B F8 48 8B E9 48 8D 15 ?? ?? ?? ?? 48 8B CF 41 B8 EB 0D 00 00 E8";
// The think's owner lookup (E8 at +11)
constexpr const char* OwnerCallPattern = "66 44 89 74 24 50 66 89 44 24 56 E8 ?? ?? ?? ?? 48 8B F0 48 85 C0 74 0C 48 8B C8 E8";
// The think's attack call (E8 at +13)
constexpr const char* AttackCallPattern =
	"4C 89 64 24 28 48 8B CD 48 89 44 24 20 E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? 45 85 F6 74 13 41 B0 05";
// The skip roll: `and ecx,0x7f ... cmp ecx,edx / jc <idle>` (jc at +10)
constexpr const char* SearchRollPattern = "83 E1 7F 41 89 44 24 04 3B CA 72 74 EB 03 44 8B F3";
// D2R's aura start 0x438a70: `cmp r8d,5 / jz` (skill 5 is skipped), prologue, the state 0x55 test
constexpr const char* AuraStartPattern = "41 83 F8 05 0F 84 ?? ?? ?? ?? 44 89 4C 24 20 55 41 56 41 57 48 83 EC 40 48 89 5C 24 "
                                         "60 45 8B F0 44 8B FA 48 8B E9 85 D2 75 1C 41 8D 57 55 E8";
// The skill pick 0x5bf070's game info call (+22) and hireling row call (+78)
constexpr const char* HirelingRowPattern =
	"4C 8B C5 48 89 9C 24 D0 00 00 00 49 8B D7 48 8D 8C 24 80 00 00 00 E8 ?? ?? ?? ?? 45 33 C0 48 8B CD F2 0F 10 00 41 8D "
	"50 0C F2 0F 11 44 24 58 E8 ?? ?? ?? ?? 41 0F B6 8F 06 01 00 00 33 D2 80 7C 24 59 01 44 8B C8 44 8B C7 8B D8 0F 95 C2 E8";
// The attack 0x5bf6e0's pet record call (+33), after its UnitId call
constexpr const char* PetRecordPattern =
	"41 B8 1B 0D 00 00 89 84 24 C8 00 00 00 48 8D 15 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 8B D0 49 8B CF E8 ?? ?? ?? ?? 48 85 C0 0F 84";

// The absolute target of the E8 call at image rva `site`
auto Callee(uint64_t base, uint64_t site) noexcept -> uint64_t {
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(base + site + 1), sizeof(rel));
	return base + site + 5 + static_cast<int64_t>(rel);
}

constexpr uint8_t SearchRollStock[] = {0x72, 0x74}, SearchRollNop[] = {0x66, 0x90};

}  // namespace

sigscan::Entry* const MercAiUses[11] = {&d2r::sig::AiUseSkill,  &d2r::sig::UnitHasState, &d2r::sig::UnitDistance,
                                        &d2r::sig::GetUnitSkill, &d2r::sig::UnitStat,     &d2r::sig::GetSkillRecord,
                                        &d2r::sig::RecordOffsets, &d2r::sig::UnitRoom,   &d2r::sig::RoomLevelId,
                                        &d2r::sig::UnitId,       &d2r::sig::AiIdle};

static int g_mercAiTotal = 0;

auto InstallMercAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const uint64_t base = context->exeBase;
	uint64_t ownerSite = 0, roll = 0, auraStart = 0, rowSite = 0, petSite = 0;
	bool hooked = false;
	// set before the think hook goes live; any miss leaves g_auraStart null (no aura restart)
	const bool aura = sigscan::ScanUnique(base, AuraStartPattern, 0, &auraStart) == 1 &&
	                  sigscan::ScanUnique(base, HirelingRowPattern, 22, &rowSite) == 1 &&
	                  sigscan::ScanUnique(base, PetRecordPattern, 33, &petSite) == 1;
	if (aura) {
		g_gameInfo = reinterpret_cast<GameInfoFn>(Callee(base, rowSite));
		g_hirelingRow = reinterpret_cast<HirelingRowFn>(Callee(base, rowSite + 56));
		g_petRecord = reinterpret_cast<PetRecordFn>(Callee(base, petSite));
		g_auraStart = reinterpret_cast<AuraStartFn>(base + auraStart);
	}
	if (sigscan::ScanUnique(base, OwnerCallPattern, 11, &ownerSite) == 1) {
		g_owner = reinterpret_cast<OwnerFn>(Callee(base, ownerSite));
		hooked = sigscan::HookBySignature(context, "MercThink", ThinkPattern, 0, ThinkEntry, sizeof(ThinkEntry), &MercThink,
		                                  &g_thinkOriginal);
	} else {
		plog::Warn("merc ai: skipped the owner call: not one match");
	}
	const bool redirected = RedirectCall(context, AttackCallPattern, 13, reinterpret_cast<uint64_t>(&MercAttack),
	                                     reinterpret_cast<uint64_t*>(&g_attackOriginal));
	const bool patched = sigscan::ScanUnique(base, SearchRollPattern, 10, &roll) == 1 &&
	                     context->PatchBytes(roll, SearchRollStock, sizeof(SearchRollStock), SearchRollNop, sizeof(SearchRollNop));
	const bool restart = hooked && aura;
	plog::Info("merc ai: think hook %s, aura start at 0x%llx %s, attack call %s, search roll at 0x%llx %s",
	           hooked ? "on" : "off", static_cast<unsigned long long>(auraStart), restart ? "on" : "off",
	           redirected ? "on" : "off", static_cast<unsigned long long>(roll), patched ? "on" : "off");
	int installed = 0;
	int total = 0;  // every rule below: MercAiCount() reports it, so there is no literal to keep
	size_t used = 0;
	const auto rule = [&](bool ok, const char* name) {
		installed += ok ? 1 : 0;
		++total;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s%s%s", used ? "; " : "", name, ok ? "" : " FAILED");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	};
	if (reportSize > 0) {
		report[0] = 0;
	}
	rule(hooked, "battle orders");
	rule(restart, "aura restart");
	rule(redirected, "whirlwind and amp");
	rule(patched, "target search");
	g_mercAiTotal = total;
	return installed;
}

auto MercAiCount() noexcept -> int {
	return g_mercAiTotal;  // set by InstallMercAi, which every caller runs first
}

auto MercAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "merc thinks %u: aura restarts %u, battle orders %u; attacks %u: whirlwind %u, amp %u",
	              g_thinks.load(), g_auras.load(), g_battleOrders.load(), g_attacks.load(), g_whirlwinds.load(), g_amps.load());
}
