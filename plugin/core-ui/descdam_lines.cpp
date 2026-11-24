// PD2's skill tooltip damage lines (#2005). D2R's descdam dispatcher (93847 0x28a100, D2Client UI/SkillDesc.cpp) calls
// table[descdam] (ids 1-28, 0x2368fe0) with (dataFlag, unit, skill, skills record, level, &line, &offhand line,
// &colour, &offhand colour); a function writes the damage text ("min-max") and its colour. Each function below is
// PD2's (ProjectDiablo.dll, PD2.gpr; read byte by byte, docs/features/core-rules.md descdam row) built from D2R's own pieces: its
// weapon fill (0x28dcb0 + 0x28d5f0, which ids 7 and 24 call), skill calcs, elemental and physical damage and its
// "min-max" formatter, so only the numbers PD2 changed differ from D2R's lines.
//   11 Vengeance     0x103ad2e0: weapon + per element (own min/max stat less the Holy auras' and Enchant's lists)
//                    raised by calc1-3 + mastery %, + 3 x the skill's EMin-EMax + its mastery %s; blue
//   13 Double Throw  0x103ad6b0: D2R's two-hand throw line with ddam calc1/calc2 (D2R's reads skills calc1, which
//                    is Split Throw's missile count); PD2's Split Throw one-hand branch is what the two-hand line
//                    prints with one weapon (SrcDam 128, no skill damage)
//   15 Dragon Talon  0x103ad7b0: PD2's damage% (ddam calc1, as the kick deals it: core-rules, #1862; D2R's is
//                    Param1 + (lvl-1) x Param2) on PD2's kick base (common/kick_base.h, #2051: stat 21/22 less the
//                    weapon's, + 111 + boots + dexterity), which our server kick deals too, + the elemental damage
//   25 charge-ups    0x103ade80: weapon at calc1 x (charges + 1) % + the p<charges+1>dm line, coloured by its element
//   26 arrows        0x103ae000: (weapon + the skill's elemental) x SrcDam / 128, coloured by EType
// Slots are written with PatchWriteU64, each expecting the pointer it holds at install (D2R's function).
#include "descdam_lines.h"

#include "kick_base.h"
#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace descdam_lines {

namespace {

using sigscan::Kind;

// The table read in the dispatcher (cmp qword [r11+rcx*8+table], 0 / jnz)
sigscan::Entry DescDamTable {"DescDamTable", "descdam", Kind::Abs, 4, 0, "49 83 bc cb ?? ?? ?? ?? 00 75 02"};
// (dataFlag, skillId) -> skilldesc record
sigscan::Entry DescRecord {"DescRecord", "descdam", Kind::Code, 0, 0,
                           "48 89 5c 24 08 55 56 57 48 83 ec 30 48 63 f2 0f b6 e9 e8 ?? ?? ?? ?? 48 8b f8 48 8b de 85 f6 78 09 48 3b 98 b8 11 00 00"};
// (dataFlag, unit, calc, skillId, level) -> a skilldesc formula's value
sigscan::Entry DescCalc {"DescCalc", "descdam", Kind::Code, 0, 0,
                         "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 41 56 41 57 48 81 ec b0 00 00 00 45 8b f1 41 8b d8 48 8b fa 44 0f b6 f9 e8 ?? ?? ?? ?? 48 8b e8 8b f3 48 3b 98 58 01 00 00"};
// (unit, &min, &max, &colour, ed1, ed2, skill, 0x80, 0, 0): the weapon's damage raised by ed1/ed2 %
sigscan::Entry WeaponFill {"WeaponFill", "descdam", Kind::Code, 0, 0, "48 89 54 24 10 53 41 54 41 55 41 57"};
// (unit, &min, &max, &colour, 0, skill): the rest of the weapon line, after WeaponFill
sigscan::Entry WeaponFillRest {"WeaponFillRest", "descdam", Kind::Code, 0, 0,
                               "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 4c 89 4c 24 20 57 41 54 41 55 41 56 41 57 48 83 ec 20 4d 8b f8"};
// (min, max, char[32]): "min-max" (max raised to min + 1), "K" above 10000
sigscan::Entry FormatRange {"FormatRange", "descdam", Kind::Code, 0, 0, "48 83 ec 38 4d 8b d8"};
// (string, length): makes room in the line's string (the handlers then copy, terminate and set the length)
sigscan::Entry StringResize {"StringResize", "descdam", Kind::Rip, 1, 5, "e8 ?? ?? ?? ?? 48 8b 05 ?? ?? ?? ?? 8d 55 0b"};
// D2R's two-hand line (dataFlag, unit, skill, record, level, fill, v1, v2, &line, &line2, &colour, &colour2, addSkill)
sigscan::Entry TwoHandLine {"TwoHandLine", "descdam", Kind::Code, 0, 0, "4c 8b dc 55 57 41 54 41 55 41 56 41 57 49 8d 6b b8"};
// its throw fill (D2R's descdam 13 passes it)
sigscan::Entry ThrowFill {"ThrowFill", "descdam", Kind::Code, 0, 0, "88 4c 24 08 53 56 41 55 41 56"};
// (dataFlag, unit, skillId, level, mastery) -> the skill's elemental min / max << 8
sigscan::Entry SkillElemMin {"SkillElemMin", "descdam", Kind::Code, 0, 0, "40 55 56 57 41 56 41 57 48 83 ec 30 4c 8b fa"};
sigscan::Entry SkillElemMax {"SkillElemMax", "descdam", Kind::Code, 0, 0,
                             "48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57 48 83 ec 30 4c 8b fa 41 8b f9"};
// The kick line's pieces (D2R's descdam 15): () -> the local player's kick damage%, (unit, &min, &max, &pct) the
// boots' damage, (unit, &min, &max, &colour) the elemental damage
sigscan::Entry KickPercent {"KickPercent", "descdam", Kind::Code, 0, 0, "41 54 48 83 ec 50 e8 ?? ?? ?? ??"};
sigscan::Entry KickItemDamage {"KickItemDamage", "descdam", Kind::Code, 0, 0, "4c 89 4c 24 20 48 89 54 24 10 53 56"};
sigscan::Entry KickElemDamage {"KickElemDamage", "descdam", Kind::Code, 0, 0, "4c 89 4c 24 20 4c 89 44 24 18 48 89 54 24 10 57"};

sigscan::Entry* const Own[] = {&DescDamTable, &DescRecord, &DescCalc, &WeaponFill, &WeaponFillRest, &FormatRange,
                               &StringResize, &TwoHandLine, &ThrowFill, &SkillElemMin, &SkillElemMax, &KickPercent,
                               &KickItemDamage, &KickElemDamage};

// D2R's line string: text, length (then capacity)
struct Line {
	char* text;
	size_t length;
};

using Handler = void (*)(uint8_t, void*, void*, const uint8_t*, int32_t, Line*, Line*, int32_t*, int32_t*) noexcept;
using DescRecordFn = const uint8_t* (*)(uint8_t, int32_t) noexcept;
using CalcFn = int32_t (*)(uint8_t, void*, int32_t, int32_t, int32_t) noexcept;
using WeaponFillFn = void (*)(void*, int32_t*, int32_t*, int32_t*, int32_t, int32_t, void*, int32_t, uint64_t, uint8_t) noexcept;
using WeaponFillRestFn = void (*)(void*, int32_t*, int32_t*, int32_t*, uint64_t, void*) noexcept;
using FormatFn = void (*)(int32_t, int32_t, char*) noexcept;
using ResizeFn = void (*)(Line*, size_t) noexcept;
using TwoHandFn = void (*)(uint8_t, void*, void*, const uint8_t*, int32_t, void*, int32_t, int32_t, Line*, Line*, int32_t*,
                           int32_t*, bool) noexcept;
using DamageFn = int32_t (*)(uint8_t, void*, int32_t, int32_t, int32_t) noexcept;
using KickPercentFn = int32_t (*)() noexcept;
using KickPartFn = void (*)(void*, int32_t*, int32_t*, int32_t*) noexcept;
using UnitStatFn = int32_t (*)(void*, int32_t, int32_t) noexcept;
using HasStateFn = int32_t (*)(void*, int32_t) noexcept;
using StateListFn = void* (*)(void*, int32_t) noexcept;
using ListStatFn = int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept;

// skills record (rvas_93847 offsets.h / knowledge/d2r-93847-addresses.md)
using d2r::rva::SkAuraStat1, d2r::rva::SkAuraState, d2r::rva::SkCalc1, d2r::rva::SkSrcDam, d2r::rva::SkEType;
// skilldesc record (its field table at 0x302380: descdam +0x10, ddam calc1 +0x14, calc2 +0x18, p1-3dmelem +0x1c,
// p1-3dmmin +0x20, p1-3dmmax +0x2c)
constexpr size_t SdCalc1 = 0x14, SdCalc2 = 0x18, SdElem = 0x1c, SdMin = 0x20, SdMax = 0x2c;
// stats and merged states Vengeance reads (core-rules' srvdo 174 uses the same)
constexpr int32_t FireMin = 48, LightMin = 50, ColdMin = 54, FireMastery = 329, LightMastery = 330, ColdMastery = 331;
constexpr int32_t HolyFire = 35, HolyWind = 43, HolyShock = 46, Enchant = 16, ColdEnchant = 223;
constexpr int32_t Blue = 3;

static_assert(ChargeIndex(0) == 0 && ChargeIndex(3) == 2 && ChargeIndex(-1) == 0);
static_assert(ElemColor(1) == 1 && ElemColor(2) == 9 && ElemColor(4) == 3 && ElemColor(5) == 2 && ElemColor(0) == -1);
static_assert(Percent(512, 30) == 153 && Percent(-512, 30) == -153);
static_assert(SrcDamScale(100, 96) == 75 && SrcDamScale(-100, 96) == -75);

template <typename T>
auto At(const uint8_t* base, size_t offset) noexcept -> T {
	T v;
	std::memcpy(&v, base + offset, sizeof v);
	return v;
}
template <typename Fn>
auto Call(const sigscan::Entry& e) noexcept -> Fn {
	return reinterpret_cast<Fn>(e.address);
}

uint64_t g_tableRva = 0;
using WeaponFn = void* (*)(void* inventory) noexcept;
using UsableFn = int32_t (*)(void* item) noexcept;
WeaponFn g_weapon = nullptr;  // the wielded weapon and its usable test, the server kick's (kick_base.h)
UsableFn g_usable = nullptr;
Handler g_d2r[std::size(Ids)] = {};
std::atomic<uint32_t> g_lines[std::size(Ids)] = {};

auto SkillId(const uint8_t* rec) noexcept -> int32_t {
	return At<int16_t>(rec, 0);
}
auto Desc(uint8_t flag, const uint8_t* rec) noexcept -> const uint8_t* {
	return Call<DescRecordFn>(DescRecord)(flag, SkillId(rec));
}
auto DescValue(uint8_t flag, void* unit, const uint8_t* rec, const uint8_t* desc, size_t field, int32_t level) noexcept
	-> int32_t {
	return Call<CalcFn>(DescCalc)(flag, unit, At<int32_t>(desc, field), SkillId(rec), level);
}
void Weapon(void* unit, void* skill, int32_t ed1, int32_t ed2, int32_t* min, int32_t* max, int32_t* color) noexcept {
	Call<WeaponFillFn>(WeaponFill)(unit, min, max, color, ed1, ed2, skill, 0x80, 0, 0);
	Call<WeaponFillRestFn>(WeaponFillRest)(unit, min, max, color, 0, skill);
}
// The handlers' tail: the "min-max" text into the line (D2R's resize, copy, terminate, length)
void Write(Line* line, int32_t min, int32_t max, bool show) noexcept {
	char text[32] = "";
	if (show) {
		Call<FormatFn>(FormatRange)(min, max, text);
	}
	const size_t n = std::strlen(text);
	Call<ResizeFn>(StringResize)(line, n);
	std::memcpy(line->text, text, n);
	line->text[n] = '\0';
	line->length = n;
}
void Logged(int slot, int32_t skill, int32_t level, int32_t min, int32_t max, int32_t color) noexcept {
	if (pd2::HotSample(g_lines[slot])) {
		plog::Info("descdam %d: skill %d level %d -> %d-%d colour %d", Ids[slot], skill, level, min, max, color);
	}
}

// --- 11: Vengeance (0x103ad2e0) --------------------------------------------------------------------------------
void Vengeance(uint8_t flag, void* unit, void* skill, const uint8_t* rec, int32_t level, Line* line, Line*, int32_t* color,
               int32_t*) noexcept {
	*color = 0;
	if (unit == nullptr || skill == nullptr || rec == nullptr) {
		return;
	}
	const auto stat = Call<UnitStatFn>(d2r::sig::UnitStat);
	const int32_t id = SkillId(rec);
	const auto aura = [&](int32_t s, int32_t state) {
		void* list = Call<StateListFn>(d2r::sig::UnitStateList)(unit, state);
		return list != nullptr ? Call<ListStatFn>(d2r::sig::ListStatValue)(flag, list, s, 0) : 0;
	};
	struct Element {
		int32_t minStat, mastery, stateA, stateB;
	};
	const Element elements[3] = {{FireMin, FireMastery, HolyFire, Enchant},
	                             {ColdMin, ColdMastery, HolyWind, ColdEnchant},
	                             {LightMin, LightMastery, HolyShock, -1}};
	const int32_t emin = Call<DamageFn>(SkillElemMin)(flag, unit, id, level, 1) >> 8;
	const int32_t emax = Call<DamageFn>(SkillElemMax)(flag, unit, id, level, 1) >> 8;
	int32_t min = 0, max = 0, weaponColor = Blue;
	Weapon(unit, skill, 0, 0, &min, &max, &weaponColor);
	for (int i = 0; i < 3; ++i) {
		const Element& e = elements[i];
		const int32_t mastery = stat(unit, e.mastery, 0);
		const int32_t pct = Call<CalcFn>(d2r::sig::EvalSkillCalc)(flag, unit, At<int32_t>(rec, SkCalc1 + 4 * i), id, level) + mastery;
		const auto net = [&](int32_t s) {
			return stat(unit, s, 0) - aura(s, e.stateA) - (e.stateB >= 0 ? aura(s, e.stateB) : 0);
		};
		const int32_t lo = net(e.minStat), hi = net(e.minStat + 1);
		min += lo + Percent(lo, pct) + emin + Percent(emin, mastery);
		max += hi + Percent(hi, pct) + emax + Percent(emax, mastery);
	}
	*color = Blue;
	Write(line, min, max, min != 0 && max != 0);
	Logged(0, id, level, min, max, *color);
}

// --- 13: Double Throw, Split Throw (0x103ad6b0) -------------------------------------------------------------------
void Throw(uint8_t flag, void* unit, void* skill, const uint8_t* rec, int32_t level, Line* line, Line* line2, int32_t* color,
           int32_t* color2) noexcept {
	const uint8_t* desc = rec != nullptr ? Desc(flag, rec) : nullptr;
	if (unit == nullptr || skill == nullptr || desc == nullptr) {
		g_d2r[1](flag, unit, skill, rec, level, line, line2, color, color2);
		return;
	}
	const int32_t v1 = DescValue(flag, unit, rec, desc, SdCalc1, level), v2 = DescValue(flag, unit, rec, desc, SdCalc2, level);
	Call<TwoHandFn>(TwoHandLine)(flag, unit, skill, rec, level, reinterpret_cast<void*>(ThrowFill.address), v1, v2, line,
	                             line2, color, color2, true);
	if (pd2::HotSample(g_lines[1])) {
		plog::Info("descdam 13: skill %d level %d, ddam calc1 %d calc2 %d -> %s / %s", SkillId(rec), level, v1, v2,
		           line->text != nullptr ? line->text : "", line2->text != nullptr ? line2->text : "");
	}
}

// --- 15: Dragon Talon, Dragon Flight (D2R's 0x284b40 with PD2's damage%; D2R's base, the server kick's) ----------
void Kick(uint8_t flag, void* unit, void* skill, const uint8_t* rec, int32_t level, Line* line, Line*, int32_t* color,
          int32_t*) noexcept {
	*color = 0;
	const uint8_t* desc = rec != nullptr ? Desc(flag, rec) : nullptr;
	if (unit == nullptr || skill == nullptr || desc == nullptr) {
		return;
	}
	const int32_t id = SkillId(rec);
	kick_base::KickStats s {};
	s.pct = Call<KickPercentFn>(KickPercent)() + DescValue(flag, unit, rec, desc, SdCalc1, level);
	if (s.pct != 0) {
		*color = Blue;
	}
	Call<KickPartFn>(KickItemDamage)(unit, &s.bootsMin, &s.bootsMax, &s.pct);
	s.pct += 100;
	const auto stat = Call<UnitStatFn>(d2r::sig::UnitStat);
	void* inventory = d2r::Engine<void* (*)(void*, const char*, int32_t) noexcept>(d2r::sig::UnitInventory)(unit, __FILE__, __LINE__);
	void* weapon = inventory != nullptr ? g_weapon(inventory) : nullptr;
	if (weapon != nullptr && d2r::Engine<int32_t (*)(void*, int32_t) noexcept>(d2r::sig::ItemIsType)(weapon, kick_base::TypeWeapon) != 0 &&
	    g_usable(weapon) != 0) {
		s.weapMin = stat(weapon, kick_base::StatMin, 0);
		s.weapMax = stat(weapon, kick_base::StatMax, 0);
	}
	s.minDam = stat(unit, kick_base::StatMin, 0);
	s.maxDam = stat(unit, kick_base::StatMax, 0);
	s.normal = stat(unit, kick_base::StatNormal, 0);
	s.dex = stat(unit, kick_base::StatDex, 0);
	const kick_base::KickRange base = kick_base::Pd2Base(s);
	int32_t min = base.min >> 8;
	int32_t max = base.max >> 8;
	int32_t elemMin = 0, elemMax = 0;
	Call<KickPartFn>(KickElemDamage)(unit, &elemMin, &elemMax, color);
	min += elemMin;
	max += elemMax;
	Write(line, min, max, true);
	Logged(2, id, level, min, max, *color);
}

// --- 25: the Assassin's charge-ups (0x103ade80) -------------------------------------------------------------------
void ChargeUp(uint8_t flag, void* unit, void* skill, const uint8_t* rec, int32_t level, Line* line, Line*, int32_t* color,
              int32_t*) noexcept {
	*color = 0;
	const uint8_t* desc = rec != nullptr ? Desc(flag, rec) : nullptr;
	if (unit == nullptr || skill == nullptr || desc == nullptr) {
		return;
	}
	int32_t n = 0;
	const int16_t chargeStat = At<int16_t>(rec, SkAuraStat1);
	const int32_t state = At<uint16_t>(rec, SkAuraState);
	if (chargeStat > -1 && Call<HasStateFn>(d2r::sig::UnitHasState)(unit, state) != 0) {
		if (void* list = Call<StateListFn>(d2r::sig::UnitStateList)(unit, state); list != nullptr) {
			n = ChargeIndex(Call<ListStatFn>(d2r::sig::ListStatValue)(flag, list, chargeStat, 0));
		}
	}
	const int32_t ed = DescValue(flag, unit, rec, desc, SdCalc1, level) * (n + 1);
	int32_t min = 0, max = 0, weaponColor = 0;
	Weapon(unit, skill, ed, 0, &min, &max, &weaponColor);
	min += DescValue(flag, unit, rec, desc, SdMin + 4 * static_cast<size_t>(n), level);
	max += DescValue(flag, unit, rec, desc, SdMax + 4 * static_cast<size_t>(n), level);
	const int32_t elem = ElemColor(desc[SdElem + static_cast<size_t>(n)]);
	*color = ed > 0 ? Blue : (elem >= 0 ? elem : weaponColor);
	Write(line, min, max, min != 0 && max != 0);
	Logged(3, SkillId(rec), level, min, max, *color);
}

// --- 26: Magic, Fire and Cold Arrow (0x103ae000) ------------------------------------------------------------------
void Arrow(uint8_t flag, void* unit, void* skill, const uint8_t* rec, int32_t level, Line* line, Line*, int32_t* color,
           int32_t*) noexcept {
	*color = 0;
	if (unit == nullptr || skill == nullptr || rec == nullptr) {
		return;
	}
	const int32_t id = SkillId(rec), srcDam = rec[SkSrcDam] != 0 ? rec[SkSrcDam] : 128;  // SrcDam 0 = 128 (offsets.h)
	int32_t wmin = 0, wmax = 0, weaponColor = 0;
	Weapon(unit, skill, 0, 0, &wmin, &wmax, &weaponColor);
	// PD2 hands SrcDam to its weapon fill as the scale; D2R's lines (ids 7, 24) fill at 0x80 and scale the sum after
	int32_t min = SrcDamScale(wmin, srcDam), max = SrcDamScale(wmax, srcDam);
	min += SrcDamScale(Call<DamageFn>(SkillElemMin)(flag, unit, id, level, 1) >> 8, srcDam);
	max += SrcDamScale(Call<DamageFn>(SkillElemMax)(flag, unit, id, level, 1) >> 8, srcDam);
	const int32_t elem = ElemColor(rec[SkEType]);
	*color = elem >= 0 ? elem : weaponColor;
	Write(line, min, max, min != 0 && max != 0);
	Logged(4, id, level, min, max, *color);
}

constexpr Handler Ours[std::size(Ids)] = {Vengeance, Throw, Kick, ChargeUp, Arrow};

}  // namespace

sigscan::Entry* const Uses[8] = {&d2r::sig::UnitStat,     &d2r::sig::UnitStateList, &d2r::sig::ListStatValue,
                                  &d2r::sig::UnitHasState, &d2r::sig::EvalSkillCalc,
                                  &d2r::sig::RecordOffsets, &d2r::sig::UnitInventory, &d2r::sig::ItemIsType};

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	sigscan::Resolve(Own, std::size(Own), context->exeBase, nullptr, 0);
	for (const sigscan::Entry* e : Own) {
		if (e->state != sigscan::State::Ok) {
			std::snprintf(report, size, "FAILED (%s: %s)", e->name, sigscan::StateText(e->state));
			return;
		}
	}
	g_weapon = reinterpret_cast<WeaponFn>(kick_base::CallTarget(context->exeBase, kick_base::WeaponBlock, kick_base::WeaponCallAt));
	g_usable = reinterpret_cast<UsableFn>(kick_base::CallTarget(context->exeBase, kick_base::WeaponBlock, kick_base::UsableCallAt));
	if (g_weapon == nullptr || g_usable == nullptr) {
		std::snprintf(report, size, "FAILED (kick weapon getter signature)");
		return;
	}
	g_tableRva = DescDamTable.rva;
	const auto* table = reinterpret_cast<const uint64_t*>(context->exeBase + g_tableRva);
	for (size_t i = 0; i < std::size(Ids); ++i) {
		const uint64_t d2r = table[Ids[i]];
		g_d2r[i] = reinterpret_cast<Handler>(d2r);
		if (d2r < context->exeBase || !context->PatchWriteU64(g_tableRva + 8ull * static_cast<uint64_t>(Ids[i]), &d2r,
		                                                      sizeof d2r, reinterpret_cast<uint64_t>(Ours[i]))) {
			std::snprintf(report, size, "FAILED (descdam slot %d refused)", Ids[i]);
			return;
		}
	}
	plog::Info("descdam: table at 0x%llx, ids 11, 13, 15, 25, 26 are PD2's", static_cast<unsigned long long>(g_tableRva));
	std::snprintf(report, size, "pd2 lines on");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "descdam: lines 11 %u, 13 %u, 15 %u, 25 %u, 26 %u", g_lines[0].load(), g_lines[1].load(),
	              g_lines[2].load(), g_lines[3].load(), g_lines[4].load());
}

}  // namespace descdam_lines
