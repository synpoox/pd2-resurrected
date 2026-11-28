// Exercise the real srvst 10 with engine doubles, including monster callers (#2224).
#include "../core-rules/skill/power_strike.cpp"

#include <array>
#include <cstdlib>

namespace {
void Check(bool ok) { if (!ok) { std::abort(); } }
std::array<uint8_t, 0x180> gameBytes {};
std::array<uint8_t, 0x300> skill {};
std::array<uint8_t, 8> attacker {}, victim {};
Game* game = reinterpret_cast<Game*>(gameBytes.data());
Unit* unit = reinterpret_cast<Unit*>(attacker.data());
Unit* target = reinterpret_cast<Unit*>(victim.data());
uint32_t seed[2] {42, 7};
uint8_t mask = 4;
int id = 14, level = 20, bonus = 260, queued = 0, rolls = 0, elemCalls = 0;
int minimum = 256, maximum = 512;
uint16_t result = 1;
bool rowPresent = true, skillPresent = true;
Unit* aimed = target;
int32_t expectedLight = 0;
uint8_t expectedSrcDam = 128, expectedAlways = 0;

auto Record(uint32_t, int32_t n) noexcept -> uint8_t* { Check(n == id); return rowPresent ? skill.data() : nullptr; }
auto Used(Unit* u) noexcept -> void* { Check(u == unit); return skillPresent ? skill.data() : nullptr; }
auto Target(Game* g, Unit* u) noexcept -> Unit* { Check(g == game && u == unit); return aimed; }
auto ToHit(Unit* u, int32_t n, int32_t l) noexcept -> int32_t {
    Check(u == unit && n == id && l == level); return bonus;
}
auto Roll(Game* g, Unit* u, Unit* t, int32_t ar, uint8_t always, int32_t extra) noexcept -> uint16_t {
    Check(g == game && u == unit && t == target && ar == bonus && always == expectedAlways && extra == 0);
    ++rolls; return result;
}
auto Min(uint32_t, Unit* u, int32_t n, int32_t l, int32_t mastery) noexcept -> int32_t {
    Check(u == unit && n == id && l == level && mastery == 1); ++elemCalls; return minimum;
}
auto Max(uint32_t, Unit* u, int32_t n, int32_t l, int32_t mastery) noexcept -> int32_t {
    Check(u == unit && n == id && l == level && mastery == 1); ++elemCalls; return maximum;
}
auto Seed(Unit* u) noexcept -> uint32_t* { Check(u == unit); return seed; }
void Queue(Game* g, Unit* u, Unit* t, void* p, uint8_t src) noexcept {
    Check(g == game && u == unit && t == target && src == expectedSrcDam);
    Check(At<uint32_t>(p, 0) == 0 && At<uint32_t>(p, 4) == result);
    Check(At<int32_t>(p, d2r::rva::DmgLight) == expectedLight);
    Check(At<int32_t>(p, d2r::rva::DmgPercent) == 0);
    Check(At<int32_t>(p, 0x148) == 0 && At<int32_t>(p, 0x14c) == 0); // EType conversion
    ++queued;
}
template <typename Fn> void Bind(sigscan::Entry& e, Fn fn) { e.address = reinterpret_cast<uint64_t>(fn); }
void Cast() {
    uint32_t expectedSeed[2] {seed[0], seed[1]};
    expectedLight = result & 1 ? minimum + static_cast<int32_t>(d2r::RandomBelow(expectedSeed,
        maximum > minimum ? static_cast<uint32_t>(maximum - minimum) : 0)) : 0;
    const int before = queued, calls = elemCalls;
    Check(SrvStPowerStrike(game, unit, id, level) == 1 && queued == before + 1);
    Check(elemCalls == calls + (result & 1 ? 2 : 0));
    Check(seed[0] == expectedSeed[0] && seed[1] == expectedSeed[1]);
}
}

int main() {
    d2r::api.getSkillRecord = Record;
    Bind(UsedSkill, Used); Bind(ElemMin, Min); Bind(ElemMax, Max);
    Bind(d2r::sig::SkillTargetUnit, Target); Bind(d2r::sig::SkillToHit, ToHit);
    Bind(d2r::sig::MeleeRoll, Roll); Bind(d2r::sig::UnitSeed, Seed); Bind(d2r::sig::QueueHit, Queue);
    d2r::sig::MeleeRollFlagMask.address = reinterpret_cast<uint64_t>(&mask);
    // Poison the unused calcs and EType: no calc evaluator is bound, so either calc would fail this test.
    const int32_t calc = 99999;
    std::memcpy(skill.data() + d2r::rva::SkCalc1, &calc, sizeof(calc));
    std::memcpy(skill.data() + d2r::rva::SkCalc4, &calc, sizeof(calc));
    skill[d2r::rva::SkEType] = 3;
    // Exercise the shared entry with distinct skill ids/levels/AR, without a player-only filter.
    for (int n : {14, 34, 519, 521, 558}) {
        id = n; level = n % 30 + 1; bonus = 20 + 12 * level;
        result = 1; Cast();
        result = 0; Cast();
        result = 0x80; Cast(); // avoided/blocked result survives queuing
    }
    result = 1; maximum = minimum; Cast();
    maximum = minimum - 1; Cast();
    maximum = minimum + 101; Cast(); // non-power-of-two RNG range
    skill[d2r::rva::SkSrcDam] = expectedSrcDam = 64;
    skill[d2r::rva::SkRollFlags] = mask;
    expectedAlways = 1; Cast();
    const int before = rolls;
    Check(SrvStPowerStrike(nullptr, unit, id, level) == 0 && SrvStPowerStrike(game, nullptr, id, level) == 0);
    rowPresent = false; Check(SrvStPowerStrike(game, unit, id, level) == 0); rowPresent = true;
    skillPresent = false; Check(SrvStPowerStrike(game, unit, id, level) == 0); skillPresent = true;
    aimed = nullptr; Check(SrvStPowerStrike(game, unit, id, level) == 0);
    aimed = unit; Check(SrvStPowerStrike(game, unit, id, level) == 0);
    Check(rolls == before);
}
