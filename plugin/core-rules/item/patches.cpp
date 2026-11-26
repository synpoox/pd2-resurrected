// Byte patches and small hooks: telekinesis, shrine potions, freeze, chill, stun, curse length, dual wield, pierce cap.
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

// --- Telekinesis skips arrows and bolts (#1833) --------------------------------------------------------------------
// PD2's Telekinesis item test (0x102fb340) keeps types 4, 9, 22, 38, 41; D2R's (93847: 0x33f170, called only by
// Telekinesis: srvdo 21 0x554880, client 0x18dde0 and 0x1923b0) also tests 56 (missile). No SDK service covers the
// skill's item filter, so a byte patch jumps over the `lea edx,[rax+0x38]; mov rcx,rbx; call; test; jnz` test.
constexpr const char* TelekinesisMissileTest =
    "8d 50 26 48 8b cb e8 ?? ?? ?? ?? 85 c0 75 ?? 8d 50 38 48 8b cb e8 ?? ?? ?? ?? 85 c0 75 ?? 8d 50 09";
constexpr int TelekinesisMissileAt = 15;  // the `lea edx,[rax+0x38]` inside the match

// One MutationService byte patch at `rva`, refused unless the live bytes are `expected`.
auto PatchBytes(const D2RL::PluginContext* context, uint64_t rva, const uint8_t* expected, const uint8_t* bytes,
                       uint32_t size) noexcept -> bool {
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = rva,
	                                              .expected = expected,
	                                              .expectedSize = size,
	                                              .bytes = bytes,
	                                              .size = size};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

auto PatchTelekinesisArrows(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0;
	static constexpr uint8_t Stock[] = {0x8d, 0x50};
	static constexpr uint8_t Skip[] = {0xeb, 0x0d};  // jmp to the type 9 test, 15 bytes on
	if (sigscan::ScanUnique(context->exeBase, TelekinesisMissileTest, TelekinesisMissileAt, &rva) != 1 ||
	    !PatchBytes(context, rva, Stock, Skip, sizeof(Skip))) {
		return false;
	}
	plog::Info("telekinesis: missile test skipped at 0x%llx, arrows and bolts stay on the ground",
	           static_cast<unsigned long long>(rva));
	return true;
}

// --- Shrine potions: Poison drops tpgs, Exploding drops tpfs (#1851) ----------------------------------------------
// PD2 rewrites the item code pushed by its shrine potion makers (1.13c D2Game +0x6c281 in +0x6c200, +0x6c601 in
// +0x6c580) from `gpm ` to `tpgs` and from `opm ` to `tpfs`. D2R's makers (93847: 0x591a90, 0x591700) pass the code as
// `mov edx,imm32` to the item-code lookup 0x313ef0; these are its only code uses. No SDK service covers a code
// constant in code, hence the byte patches; the imm32 sits 4 bytes into each match.
constexpr const char* ShrineGasCode = "48 8b 07 ba 67 70 6d 20 0f b6 88 06 01 00 00 e8 ?? ?? ?? ?? 8b d8 83 f8 ff 75 0f";
constexpr const char* ShrineFireCode = "48 8b 07 ba 6f 70 6d 20 0f b6 88 06 01 00 00 e8 ?? ?? ?? ?? 8b d8 83 f8 ff 75 0f";
constexpr int ShrineCodeAt = 4;

auto PatchShrinePotions(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t gas = 0;
	uint64_t fire = 0;
	static constexpr uint8_t GasStock[] = {'g', 'p', 'm', ' '};
	static constexpr uint8_t FireStock[] = {'o', 'p', 'm', ' '};
	static constexpr uint8_t Tpgs[] = {'t', 'p', 'g', 's'};
	static constexpr uint8_t Tpfs[] = {'t', 'p', 'f', 's'};
	if (sigscan::ScanUnique(context->exeBase, ShrineGasCode, ShrineCodeAt, &gas) != 1 ||
	    sigscan::ScanUnique(context->exeBase, ShrineFireCode, ShrineCodeAt, &fire) != 1 ||
	    !PatchBytes(context, gas, GasStock, Tpgs, sizeof(Tpgs)) || !PatchBytes(context, fire, FireStock, Tpfs, sizeof(Tpfs))) {
		return false;
	}
	plog::Info("shrine potions: tpgs at 0x%llx, tpfs at 0x%llx", static_cast<unsigned long long>(gas),
	           static_cast<unsigned long long>(fire));
	return true;
}

// --- Half freeze duration: 2 or more points cannot be frozen (#1864) -----------------------------------------------
// PD2's freeze length check (ProjectDiablo.dll 0x10304db0, called at 1.13c D2Game +0xdc477 in place of +0xda7b0):
// without item_cannotbefrozen (153), item_halffreezeduration (118) == 1 halves the freeze and cold lengths, >= 2 sets
// both to 0, <= 0 leaves them. D2R's damage effects (93847: 0x44df10, the hit path of every unit, monsters' and
// players') halve for any nonzero 118: `test eax,eax; jz skip; mov eax,[r15+0x118]` at 0x44eb83 becomes `cmp eax,1;
// jl skip; jg <the 153 path's zeroing, 0x44eb68>; mov eax,[rbx-4]; nop` (rbx = r15+0x11c). No SDK service covers a
// hit's freeze length, hence the byte patch. The match starts at the `lea rbx`, so the patched jumps' targets are in it.
constexpr const char* HalfFreezeTest =
    "49 8d 9f 1c 01 00 00 45 39 b7 18 01 00 00 7f 05 44 39 33 7e 4f 45 33 c0 ba 99 00 00 00 48 8b ce e8 ?? ?? ?? ?? "
    "85 c0 74 0c 45 89 b7 18 01 00 00 44 89 33 eb 2f 45 33 c0 48 8b ce 41 8d 50 76 e8 ?? ?? ?? ?? 85 c0 74 1c";
constexpr int HalfFreezeTestAt = 68;  // the stat 118 `test eax,eax` inside the match

auto PatchHalfFreeze(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0;
	static constexpr uint8_t Stock[] = {0x85, 0xc0, 0x74, 0x1c, 0x41, 0x8b, 0x87, 0x18, 0x01, 0x00, 0x00};
	static constexpr uint8_t Pd2[] = {0x83, 0xf8, 0x01, 0x7c, 0x1b, 0x7f, 0xde, 0x8b, 0x43, 0xfc, 0x90};
	if (sigscan::ScanUnique(context->exeBase, HalfFreezeTest, HalfFreezeTestAt, &rva) != 1 ||
	    !PatchBytes(context, rva, Stock, Pd2, sizeof(Pd2))) {
		return false;
	}
	plog::Info("half freeze: 2 or more points cannot be frozen, test patched at 0x%llx", static_cast<unsigned long long>(rva));
	return true;
}

// --- Chill also slows leap and faster cast rate (#1861) ------------------------------------------------------------
// PD2 routes the chill list's attackrate add (1.13c D2Game +0xdc88d in +0xdc780) through ProjectDiablo.dll 0x1036e4a0
// (installed 0x1036e510): attackrate (68) by the chill value v (negative), stat 423 by 3v, and, when the chilled unit's
// total item_fastercastrate (105) is >= 0, 105 by max(v, -FCR) on the same list. Stat 423 is a PD2 raw row:
// `item_leap_speed`, row 476 in our itemstatcost (Leap's calc2 adds item_leap_speed / 4). D2R's chill apply (93847:
// 0x4509b0, from the hit apply 0x44ce80: every unit) makes the new list with 67, 68 and 69 only, then recomputes the
// unit's rates (0x350b40, unit, file, line). That one call goes to ChillRates, which adds the two stats to the unit's
// state 11 list first. No SDK service reaches a chill's stat list, hence the call redirect.
constexpr const char* ChillRatesCall =
    "45 8d 46 44 45 8b cf 90 44 89 74 24 20 48 8b d3 e8 ?? ?? ?? ?? 0f b6 8d 06 01 00 00 45 8d 46 45 45 8b cf 90 "
    "44 89 74 24 20 48 8b d3 e8 ?? ?? ?? ?? 41 b8 ?? ?? ?? ?? 48 8d 15 ?? ?? ?? ?? 48 8b cf e8";
constexpr uint32_t ChillRatesCallAt = 65;
constexpr int32_t StateCold = 11, StatAttackRate = 68, StatFasterCast = 105, StatLeapSpeed = 476;

// PD2's FCR add: none below 0 FCR, else v but never past 0
constexpr auto ChillCastAdd(int32_t fcr, int32_t v) noexcept -> int32_t {
	return fcr < 0 ? 0 : fcr > -v ? v : -fcr;
}
static_assert(ChillCastAdd(40, -25) == -25 && ChillCastAdd(10, -25) == -10 && ChillCastAdd(0, -25) == 0 &&
              ChillCastAdd(-5, -25) == 0);

using RatesFn = void (*)(Unit*, const char*, int32_t) noexcept;
RatesFn g_chillRates = nullptr;

void ChillRates(Unit* unit, const char* file, int32_t line) noexcept {
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(unit, StateCold);
	if (list != nullptr) {
		const uint8_t flag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::UnitDataFlag)(unit);
		const auto set = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetListStat);
		const int32_t v = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(d2r::sig::ListStatValue)(
			flag, list, StatAttackRate, 0);
		set(flag, list, StatLeapSpeed, v * 3, 0);
		const int32_t fcr = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(unit, StatFasterCast, 0);
		if (const int32_t add = ChillCastAdd(fcr, v); add != 0) {
			set(flag, list, StatFasterCast, add, 0);
		}
	}
	g_chillRates(unit, file, line);
}

auto PatchChillRates(const D2RL::PluginContext* context) noexcept -> bool {
	for (const sigscan::Entry* e : {&d2r::sig::UnitStateList, &d2r::sig::UnitDataFlag, &d2r::sig::SetListStat,
	                                &d2r::sig::ListStatValue, &d2r::sig::UnitStat}) {
		if (e->state != sigscan::State::Ok) {
			return false;
		}
	}
	if (!RedirectCall(context, ChillRatesCall, ChillRatesCallAt, reinterpret_cast<uint64_t>(&ChillRates),
	                  reinterpret_cast<uint64_t*>(&g_chillRates))) {
		return false;
	}
	plog::Info("chill: leap speed and faster cast rate added to the cold list (rate call redirected)");
	return true;
}

// --- Stun length: Mind Blast's fixed stun, Smite's player-vs-player cap (#1865) -----------------------------------
// PD2 replaces the stun apply (1.13c D2Game +0xdcc20, called at +0xde287 in the hit apply +0xde0c0) with
// ProjectDiablo.dll 0x10302a20 (attacker, target, frames). Against 1.13c's it adds, before the stock checks and caps:
// - the attacker's current skill (D2Common #10511) is Mind Blast (273) with 1 hard point (skill +0x28 == 1): the stun
//   is 50 frames whatever the hit passed (33 on PD2's PvP levels 157, 159, 166, which our data does not have);
// - a player hits a player: Smite (97) is capped at 63 frames.
// The stock rest stays the same in D2R's copy (93847: 0x451c30, the one call at 0x44d11c in the hit apply 0x44ce80):
// a unique monster skips 90%, hirelings cap at difficultylevels' MercenaryMaxStunLength (13, PD2's 13), all at 250.
// That call goes to StunLength. No SDK service reaches a hit's stun length, hence the call redirect.
constexpr const char* StunApplyCall =
    "44 8b 87 2c 01 00 00 48 8b d6 49 8b ce e8 ?? ?? ?? ?? 44 8b 87 18 01 00 00 48 8b d6 49 8b ce e8";
constexpr uint32_t StunApplyCallAt = 13;
constexpr int32_t SkillMindBlast = 273;  // SkillSmite (97) is the crushing blow's, above

// PD2's stun length before the stock caps: Mind Blast at 1 hard point 50, Smite between players at most 63
constexpr auto Pd2StunLength(int32_t frames, int32_t skill, int32_t hardPoints, bool playerVsPlayer) noexcept -> int32_t {
	if (skill == SkillMindBlast && hardPoints == 1) {
		return 50;
	}
	return playerVsPlayer && skill == SkillSmite && frames > 63 ? 63 : frames;
}
static_assert(Pd2StunLength(0, SkillMindBlast, 1, false) == 50);   // Mind Blast, monster target, no stun passed
static_assert(Pd2StunLength(200, SkillMindBlast, 1, true) == 50);  // Mind Blast between players
static_assert(Pd2StunLength(40, SkillMindBlast, 2, false) == 40);  // Mind Blast at 2 hard points: the hit's own
static_assert(Pd2StunLength(100, SkillSmite, 20, true) == 63);     // Smite, player vs player
static_assert(Pd2StunLength(100, SkillSmite, 20, false) == 100);   // Smite on a monster or hireling
static_assert(Pd2StunLength(30, SkillSmite, 20, true) == 30);      // Smite under the cap
static_assert(Pd2StunLength(100, 0, 1, true) == 100);              // any other skill

using StunFn = void (*)(Unit* attacker, Unit* target, int32_t frames) noexcept;
StunFn g_stunApply = nullptr;

// --- Stun refresh between players (#1947) ---
// PD2's stun apply, on a target already in state 21 (stunned; PD2 raw 21 = our 21), resolves 0x103749f0(attacker, 1)
// and skips the refresh (no new end frame, no event) when that is a player and the target is a player; D2R's 0x451c30
// refreshes on any attacker. 0x103749f0: a player is itself; a monster gives its AI owner (1.13c monster data +0x28
// -> +0x30), a missile with flag 0x400 its owner (+0x94), and a monster found so gets its own AI owner in turn; no
// owner: the unit itself. D2R's pieces: the AI owner getter 0x4a53c0 (from skill_functions' Blade Sentinel install)
// and MissileOwner 0x490300 (no SDK service resolves a unit's owner). A first stun is untouched. Server side, so it
// holds for joined clients.
constexpr int StunnedState = 21;
constexpr int MonsterDataAi = 0x30;  // 0x4a53c0 asserts on a monster without its AI block: checked before the call
auto StunOwnerStep(Unit* unit, AiOwnerFn aiOwner) noexcept -> Unit* {
	switch (At<int32_t>(unit, d2r::rva::UnitType)) {
	case 1: {
		const auto* data = At<const uint8_t*>(unit, d2r::rva::UnitData);
		return data != nullptr && At<void*>(data, MonsterDataAi) != nullptr ? aiOwner(unit) : nullptr;
	}
	case 3:
		return d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::sig::MissileOwner)(At<Game*>(unit, d2r::rva::UnitGame), unit);
	default:
		return nullptr;
	}
}
auto StunOwner(Unit* unit, AiOwnerFn aiOwner) noexcept -> Unit* {
	if (At<int32_t>(unit, d2r::rva::UnitType) == 0) {
		return unit;
	}
	Unit* owner = StunOwnerStep(unit, aiOwner);
	if (owner == nullptr) {
		return unit;
	}
	Unit* next = At<int32_t>(owner, d2r::rva::UnitType) == 1 ? StunOwnerStep(owner, aiOwner) : nullptr;
	return next != nullptr ? next : owner;
}
// True when PD2 keeps the target's current stun; false (D2R's refresh) when a piece is missing
auto KeepsStun(Unit* attacker, Unit* target) noexcept -> bool {
	const AiOwnerFn aiOwner = AiOwnerGetter();
	if (aiOwner == nullptr || d2r::sig::MissileOwner.state != sigscan::State::Ok ||
	    d2r::sig::UnitStateList.state != sigscan::State::Ok || At<int32_t>(target, d2r::rva::UnitType) != 0 ||
	    d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(target, StunnedState) == nullptr) {
		return false;
	}
	const Unit* owner = StunOwner(attacker, aiOwner);
	return owner != nullptr && At<int32_t>(owner, d2r::rva::UnitType) == 0;
}

void StunLength(Unit* attacker, Unit* target, int32_t frames) noexcept {
	if (attacker != nullptr && target != nullptr && KeepsStun(attacker, target)) {
		return;
	}
	void* skill = attacker != nullptr && target != nullptr ? d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::CurrentSkill)(attacker)
	                                                         : nullptr;
	if (skill != nullptr) {
		const int32_t id = d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(d2r::sig::UnitSkillId)(skill, __FILE__,
		                                                                                                         __LINE__);
		const int32_t hard = id == SkillMindBlast
		                         ? d2r::Engine<int32_t (*)(Unit*, void*, int32_t) noexcept>(d2r::sig::UnitSkillLevel)(attacker, skill, 0)
		                         : 0;
		const bool pvp = At<int32_t>(attacker, d2r::rva::UnitType) == 0 && At<int32_t>(target, d2r::rva::UnitType) == 0;
		if (const int32_t pd2 = Pd2StunLength(frames, id, hard, pvp); pd2 != frames) {
			frames = pd2;
		}
	}
	g_stunApply(attacker, target, frames);
}

auto PatchStunLength(const D2RL::PluginContext* context) noexcept -> bool {
	for (const sigscan::Entry* e : {&d2r::sig::CurrentSkill, &d2r::sig::UnitSkillId, &d2r::sig::UnitSkillLevel}) {
		if (e->state != sigscan::State::Ok) {
			return false;
		}
	}
	if (!RedirectCall(context, StunApplyCall, StunApplyCallAt, reinterpret_cast<uint64_t>(&StunLength),
	                  reinterpret_cast<uint64_t*>(&g_stunApply))) {
		return false;
	}
	plog::Info("stun length: Mind Blast 50 frames, Smite 63 player vs player (stun apply call redirected)");
	return true;
}

// --- Dim Vision and Terror full length; hit blind takes blinddimvision (#1837) ------------------------------------
// PD2 NOPs 1.13c srvdo 30's AiCurseDivisor call (D2Game +0x506a8), so Dim Vision and Terror (and item procs of them)
// keep their full length in Nightmare and Hell. D2R's srvdo 30 (93847: 0x55c840) divides at 0x55ca27 only for
// auratargetstate 23 or 56: `test esi,esi; jz` becomes `jmp`. Attract (srvdo 59) and Confuse (61) keep the divide, as
// in PD2, so difficultylevels.txt is left alone. No SDK service covers a skill's length, hence the byte patches.
constexpr const char* AiCurseDivide =
    "85 f6 74 ?? 0f b6 54 24 30 0f b6 8f 06 01 00 00 e8 ?? ?? ?? ?? 48 85 c0 74 ?? 8b 48 18 85 c9 74 ?? 41 8b c6 99 f7 f9";
// PD2's hit-blind cast (1.13c +0x50470, patch +0x505a0) puts state 216 blinddimvision, not Dim Vision's curse state;
// D2R's (0x5598f0, from the hit-blind stat handler 0x583400) reads auratargetstate at 0x559b3b: `movsx eax,word
// [r13+0xa2]` becomes `mov eax,<blinddimvision>; nop3`. That state is not a curse, so the blind keeps the target's curse.
constexpr const char* HitBlindState = "41 0f bf 85 a2 00 00 00 48 8d 55 ?? 48 8b 4d ?? 89 45 ?? e8";

// The row of state `name` in the mod's states.txt (D2R numbers states by row under the header), or -1.
auto StateRow(const D2RL::PluginContext* context, std::string_view name) noexcept -> int32_t {
	if (context->modDirectory == nullptr || context->activeMod == nullptr) {
		return -1;
	}
	const std::wstring tail = L"\\data\\global\\excel\\states.txt";
	const std::wstring dir = context->modDirectory;
	std::wstring mod;
	for (const char* c = context->activeMod; *c != '\0'; ++c) {
		mod += static_cast<wchar_t>(static_cast<unsigned char>(*c));
	}
	for (const std::wstring& path : {dir + L"\\" + mod + L".mpq" + tail, dir + tail}) {  // both layouts, as propids
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
			continue;
		}
		char line[512];
		int32_t row = -1;  // the header line is row -1
		bool start = true;  // a line longer than the buffer comes in pieces; only its first piece counts
		while (std::fgets(line, sizeof(line), f) != nullptr) {
			const size_t cell = std::strcspn(line, "\t\r\n");
			if (start && row >= 0 && cell == name.size() && std::memcmp(line, name.data(), cell) == 0) {
				std::fclose(f);
				return row;
			}
			row += start && line[0] != '\n' && line[0] != '\r' ? 1 : 0;
			start = std::strchr(line, '\n') != nullptr;
		}
		std::fclose(f);
		return -1;
	}
	return -1;
}

auto PatchCurseLength(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t divide = 0, blind = 0;
	const int32_t state = StateRow(context, "blinddimvision");
	static constexpr uint8_t TestJz[] = {0x85, 0xf6, 0x74};
	static constexpr uint8_t TestJmp[] = {0x85, 0xf6, 0xeb};
	static constexpr uint8_t ReadState[] = {0x41, 0x0f, 0xbf, 0x85, 0xa2, 0x00, 0x00, 0x00};
	uint8_t blindState[] = {0xb8, 0, 0, 0, 0, 0x0f, 0x1f, 0x00};
	std::memcpy(blindState + 1, &state, sizeof(state));
	const bool full = sigscan::ScanUnique(context->exeBase, AiCurseDivide, 0, &divide) == 1 &&
	                  PatchBytes(context, divide, TestJz, TestJmp, sizeof(TestJmp));
	const bool blinds = state > 0 && sigscan::ScanUnique(context->exeBase, HitBlindState, 0, &blind) == 1 &&
	                    PatchBytes(context, blind, ReadState, blindState, sizeof(blindState));
	plog::Info("curse length: AiCurseDivisor divide %s at 0x%llx (Dim Vision, Terror full length); hit blind state %s "
	           "(blinddimvision = %d) at 0x%llx",
	           full ? "skipped" : "FAILED", static_cast<unsigned long long>(divide), blinds ? "patched" : "FAILED", state,
	           static_cast<unsigned long long>(blind));
	return full && blinds;
}

// --- Dual wield attack rate: the faster weapon's (#1867) -----------------------------------------------------------
// PD2 NOPs 1.13c D2Common's dual wield average (+0x334ac..+0x334cc in the unit rate setter +0x33110, Barbarian and
// Assassin) and calls ProjectDiablo.dll 0x102fc0e0: for a player it compares attackrate (68) + item_fasterattackrate
// (93) of the right and the left weapon, makes the higher one (the left on a tie) the only active weapon and takes
// the unit's 68 + effective IAS (120 * IAS / (120 + IAS)) with it; a monster keeps the average. D2R's copy (93847:
// 0x350b40) already takes that rate with each hand active, [rbp-0x2c] with the inventory's main weapon (rdi),
// [rbp-0x30] with the other (rbx), but averages them (0x3513ff) and only for a skill whose weapsel is above 1.
// Two byte patches, both or neither: the weapsel gate (0x35129c) also lets a player in, as PD2 has no skill gate, and
// the average calls DualWieldRate. No SDK service covers a unit's animation rate, hence the patches. 0x350b40 runs on
// the server and on every client (each unit's animation), and the plugin patches each process the same way.
constexpr const char* DualWieldGate =
    "e8 ?? ?? ?? ?? 84 c0 0f 84 ?? ?? ?? ?? 48 85 db 74 0e 80 bb 08 02 00 00 01 77 1f e9";
constexpr int DualWieldGateAt = 18;  // the `cmp byte [rbx+0x208],1` (weapsel)
constexpr const char* DualWieldAverage =
    "45 84 ed 75 09 48 85 ff 75 21 84 c0 74 1d 44 38 75 18 75 09 48 85 db 75 12 84 c0 74 0e "
    "8b 45 d0 03 45 d4 99 2b c2 d1 f8 44 8b f8 48 8b ce e8";
constexpr int DualWieldAverageAt = 29;  // `mov eax,[rbp-0x30]; add eax,[rbp-0x2c]; cdq; sub; sar; mov r15d,eax`
constexpr int32_t StatFasterAttack = 93;  // item_fasterattackrate; StatAttackRate (68) is above (same rows in PD2)
constexpr int32_t BodyLocRightArm = 4;

// PD2's pick (0x102fc0e0): the right weapon's rate when its 68 + 93 is the higher, else the left's
constexpr auto FasterHandRate(int32_t rightSpeed, int32_t leftSpeed, int32_t rightRate, int32_t leftRate) noexcept
	-> int32_t {
	return rightSpeed > leftSpeed ? rightRate : leftRate;
}
static_assert(FasterHandRate(20, 10, 130, 110) == 130);
static_assert(FasterHandRate(10, 10, 130, 110) == 110);  // a tie takes the left
static_assert(FasterHandRate(20, 25, 140, 120) == 120);  // by the weapons' raw 68 + 93, not by the rates
static_assert((-3 + 0) / 2 == -1);                      // C++ division truncates as D2R's cdq / sub / sar does

// rates[0] = [rbp-0x30] (the other weapon active), rates[1] = [rbp-0x2c] (the main one); null when not a weapon
auto DualWieldRate(Unit* unit, Unit* main, Unit* other, const int32_t* rates) noexcept -> int32_t {
	if (unit == nullptr || At<int32_t>(unit, d2r::rva::UnitType) != 0 || main == nullptr || other == nullptr) {
		return (rates[0] + rates[1]) / 2;  // D2R's own average (a monster)
	}
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
	const auto speed = [&](Unit* weapon) noexcept { return stat(weapon, StatAttackRate, 0) + stat(weapon, StatFasterAttack, 0); };
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitInventory)(unit, __FILE__, __LINE__);
	const bool otherRight = inventory != nullptr &&
	                        d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(d2r::sig::InventoryBodyItem)(inventory, BodyLocRightArm) == other;
	return otherRight ? FasterHandRate(speed(other), speed(main), rates[0], rates[1])
	                  : FasterHandRate(speed(main), speed(other), rates[1], rates[0]);
}

// Gate stub: `cmp byte [rbx+0x208],1; ja ret; cmp dword [rsi],0; sete al; cmp al,0; ret`, so the site's `ja` is taken
// for weapsel > 1 or a player (eax is dead there). Average stub: DualWieldRate(rsi, rdi, rbx, rbp-0x30) into r15d.
auto DualWieldStubs(uint8_t* relay) noexcept -> void {
	static constexpr uint8_t Gate[] = {0x80, 0xbb, 0x08, 0x02, 0x00, 0x00, 0x01, 0x77, 0x08, 0x83, 0x3e, 0x00,
	                                   0x0f, 0x94, 0xc0, 0x3c, 0x00, 0xc3};
	std::memcpy(relay, Gate, sizeof(Gate));
	// mov rcx,rsi / mov rdx,rdi / mov r8,rbx / lea r9,[rbp-0x30] / sub rsp,0x28 / mov rax,DualWieldRate / call rax /
	// add rsp,0x28 / mov r15d,eax / ret (the site's rsp is 16-aligned, so the call's is too)
	static constexpr uint8_t Head[] = {0x48, 0x8b, 0xce, 0x48, 0x8b, 0xd7, 0x4c, 0x8b, 0xc3, 0x4c, 0x8d,
	                                   0x4d, 0xd0, 0x48, 0x83, 0xec, 0x28, 0x48, 0xb8};
	static constexpr uint8_t Tail[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0x44, 0x8b, 0xf8, 0xc3};
	uint8_t* average = relay + 0x20;
	const uint64_t target = reinterpret_cast<uint64_t>(&DualWieldRate);
	std::memcpy(average, Head, sizeof(Head));
	std::memcpy(average + sizeof(Head), &target, 8);
	std::memcpy(average + sizeof(Head) + 8, Tail, sizeof(Tail));
}

// One transaction checks and writes both sites, and restores both on a failure.
auto PatchDualWieldRate(const D2RL::PluginContext* context) noexcept -> bool {
	for (const sigscan::Entry* e : {&d2r::sig::UnitStat, &d2r::sig::UnitInventory, &d2r::sig::InventoryBodyItem}) {
		if (e->state != sigscan::State::Ok) {
			return false;
		}
	}
	uint64_t gate = 0, average = 0;
	const D2RL::MutationService* mutations = nullptr;
	if (sigscan::ScanUnique(context->exeBase, DualWieldGate, DualWieldGateAt, &gate) != 1 ||
	    sigscan::ScanUnique(context->exeBase, DualWieldAverage, DualWieldAverageAt, &average) != 1 ||
	    context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	DualWieldStubs(relay);
	static constexpr uint8_t GateStock[] = {0x80, 0xbb, 0x08, 0x02, 0x00, 0x00, 0x01, 0x77, 0x1f};
	static constexpr uint8_t AverageStock[] = {0x8b, 0x45, 0xd0, 0x03, 0x45, 0xd4, 0x99, 0x2b, 0xc2, 0xd1, 0xf8, 0x44, 0x8b, 0xf8};
	// call gate stub / ja +0x21 (the stock ja's target) / nop nop; call average stub / 9 nops
	uint8_t gateBytes[] = {0xe8, 0, 0, 0, 0, 0x77, 0x21, 0x90, 0x90};
	uint8_t averageBytes[] = {0xe8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
	const int64_t gateReach = reinterpret_cast<int64_t>(relay) - static_cast<int64_t>(context->exeBase + gate + 5);
	const int64_t averageReach = reinterpret_cast<int64_t>(relay + 0x20) - static_cast<int64_t>(context->exeBase + average + 5);
	const int32_t gateRel = static_cast<int32_t>(gateReach), averageRel = static_cast<int32_t>(averageReach);
	std::memcpy(gateBytes + 1, &gateRel, 4);
	std::memcpy(averageBytes + 1, &averageRel, 4);
	DWORD old;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (gateReach != gateRel || averageReach != averageRel || VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) == 0 ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	const D2RL::Mutations::BytePatchRequest patches[] = {
	    {.structSize = D2RL::Mutations::BytePatchRequestSize, .rva = gate, .expected = GateStock,
	     .expectedSize = sizeof(GateStock), .bytes = gateBytes, .size = sizeof(gateBytes)},
	    {.structSize = D2RL::Mutations::BytePatchRequestSize, .rva = average, .expected = AverageStock,
	     .expectedSize = sizeof(AverageStock), .bytes = averageBytes, .size = sizeof(averageBytes)}};
	bool staged = true;
	for (const auto& patch : patches) {
		D2RL::Mutations::OperationHandle operation = 0;
		staged = staged && mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	}
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (!staged || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		// a failed commit that did not restore the stock bytes may still call into the relay: keep the page then
		if (!staged || D2RL::Mutations::HasFlag(result.flags, D2RL::Mutations::CommitFlags::OriginalStateRestored)) {
			VirtualFree(relay, 0, MEM_RELEASE);
		}
		return false;
	}
	plog::Info("dual wield rate: a player's two weapons attack at the faster one's rate (gate 0x%llx, average 0x%llx)",
	           static_cast<unsigned long long>(gate), static_cast<unsigned long long>(average));
	return true;
}

// --- Missile pierce cap (#1839) -----------------------------------------------------------------------------------
// PD2 replaces both halves of the pierce roll made when a piercing missile is created:
// - server: 0x10374f20 (installed 0x10374f90), called at 1.13c D2Game +0x6fe59 in place of +0x6f660. chance =
//   item_pierce + skill_pierce + 0x10381720(owner, skill_pierce) (our HandStatSum);
// - client: 0x10375060 (installed 0x103750d0), called at 1.13c D2Client +0xb363d in place of 0x6fb60a60. chance =
//   item_pierce + skill_pierce, seed always the owner's base pierce_idx (1.13c's chance 100 / seed 0 for another
//   player's missile is dropped).
// Both roll rand % 100 < chance up to 10 times, 6 for a Lightning Fury missile (missile data +0xa, the skill: 1.13c
// D2Common 0x6fdba2b0), into the missile's pierce_idx, the pierces left, which each hit lowers by one (D2R 0x463bde
// server, 0x1b87f2 client). D2R's rolls (93847: server 0x537ed0, client 0x1bced0) stop at 4 and add the weapon mastery
// bonus type 3 (0x33deb0, players only). No SDK service covers missile creation, so both rolls are replaced by hooks
// found by signature. pierce_idx stays a byte in D2R's missile copy (0x537e14).
constexpr uint32_t StatPierceIdx = 0x148, StatSkillPierce = 0xa6, StatItemPierce = 0x9c;
constexpr int32_t SkillLightningFury = 35;
constexpr uint8_t MissilePierceFlag = 0x04;  // missiles.txt Pierce: D2R tests record +0x24 against its bit table's 4
PierceRollFn g_pierceRollOriginal = nullptr;  // not called: PD2's roll replaces D2R's
ClientPierceRollFn g_clientPierceRollOriginal = nullptr;  // not called either

// The part both halves share: the owner and missile types, the Pierce flag, then the roll into pierce_idx.
// `server` adds PD2's server-only HandStatSum term.
static void RollPierce(uint32_t dataFlag, Unit* missile, Unit* owner, bool server) noexcept {
	using namespace d2r::rva;
	if (missile == nullptr || owner == nullptr || At<int32_t>(missile, UnitType) != 3 ||
	    At<int32_t>(owner, UnitType) > 1) {
		return;
	}
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
	    dataFlag, At<int32_t>(missile, UnitClassId));
	if (row == nullptr || (At<uint8_t>(row, 0x24) & MissilePierceFlag) == 0) {
		return;
	}
	// signed, as D2R's compare; PD2's unsigned one would let a negative chance pierce every roll (no data gives one)
	const int32_t chance =
	    Stat(owner, StatSkillPierce) +
	    d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(UnitStatNoParam)(owner, StatItemPierce, 0) +
	    (server ? HandStatSum(owner, StatSkillPierce) : 0);
	if (chance == 0) {
		return;
	}
	const int32_t skill = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSkill)(missile);
	const int32_t cap = skill == SkillLightningFury ? 6 : 10;
	// the owner's base pierce_idx seeds a local D2 RNG {lo, 666}, as both games do (D2R 0x367110)
	uint64_t seed = static_cast<uint32_t>(
	                    d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(UnitBaseStat)(owner, StatPierceIdx, 0)) |
	                (666ull << 32);
	int32_t count = 0;
	while (count < cap) {
		seed = (seed & 0xffffffff) * 0x6ac690c5 + (seed >> 32);
		if (static_cast<int32_t>(static_cast<uint32_t>(seed) % 100) >= chance) {
			break;
		}
		++count;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(missile, StatPierceIdx, count, 0);
}

void PierceRoll(Game* game, Unit* missile, Unit* owner) noexcept {
	if (game != nullptr) {
		RollPierce(d2r::DataFlag(game), missile, owner, true);
	}
}

void ClientPierceRoll(Unit* missile, Unit* owner) noexcept {
	if (missile != nullptr) {
		RollPierce(d2r::api.unitDataFlag(missile), missile, owner, false);
	}
}
