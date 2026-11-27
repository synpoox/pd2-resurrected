// Power Strike family, srvst 10 (#2224): ProjectDiablo.dll 0x103b5dd0.
// PD2 rolls with SkillToHit and adds only the skill's lightning, without calc1 damage% or calc4/EType conversion.
// D2R 93847's 0x558930 supplies the remaining start/queue layout. No SDK service exposes this melee start;
// replace its shared table slot through MutationService, using signature-resolved engine helpers.
#include "pd2_engine.h"
#include "log.h"
#include "registry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {
using d2r::At;
using d2r::Game;
using d2r::Unit;

// Resolve its call in srvst 10: the used-skill getter shares its body with other skill getters.
sigscan::Entry UsedSkill {"PowerStrikeUsedSkill", "skills", sigscan::Kind::Rip, 4, 8,
    "48 8b cb e8 ?? ?? ?? ?? 48 85 c0 75 1a 48 8d 4c 24 30 88 44 24 30"};
// The elemental min/max called by D2R's srvst 10, with mastery enabled (fifth argument 1).
sigscan::Entry ElemMin {"PowerStrikeElemMin", "skills", sigscan::Kind::Code, 0, 0,
    "40 55 56 57 41 56 41 57 48 83 ec 30 4c 8b fa"};
sigscan::Entry ElemMax {"PowerStrikeElemMax", "skills", sigscan::Kind::Code, 0, 0,
    "48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57 48 83 ec 30 4c 8b fa 41 8b f9"};

auto SrvStPowerStrike(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
    using namespace d2r::rva;
    if (game == nullptr || unit == nullptr) {
        return 0;
    }
    const uint32_t flag = d2r::DataFlag(game);
    const uint8_t* skill = d2r::api.getSkillRecord(flag, skillId);
    if (skill == nullptr || d2r::Engine<void* (*)(Unit*) noexcept>(UsedSkill)(unit) == nullptr) {
        return 0;
    }
    Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::sig::SkillTargetUnit)(game, unit);
    if (target == nullptr || target == unit) {
        return 0;
    }
    const int32_t toHit = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SkillToHit)(unit, skillId, level);
    const uint8_t mask = At<uint8_t>(reinterpret_cast<const void*>(d2r::sig::MeleeRollFlagMask.address), 0);
    d2r::DamageBlock damage;
    damage.resultFlags = d2r::Engine<uint16_t (*)(Game*, Unit*, Unit*, int32_t, uint8_t, int32_t) noexcept>(
        d2r::sig::MeleeRoll)(game, unit, target, toHit, (skill[SkRollFlags] & mask) != 0 ? 1 : 0, 0);
    if ((damage.resultFlags & ResultHit) != 0) {
        using DamageFn = int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept;
        const int32_t min = d2r::Engine<DamageFn>(ElemMin)(flag, unit, skillId, level, 1);
        const int32_t max = d2r::Engine<DamageFn>(ElemMax)(flag, unit, skillId, level, 1);
        const uint32_t range = max > min ? static_cast<uint32_t>(max) - static_cast<uint32_t>(min) : 0;
        auto* seed = d2r::Engine<d2r::UnitSeedFn>(d2r::sig::UnitSeed)(unit);
        const int32_t lightning = min + static_cast<int32_t>(d2r::RandomBelow(seed, range));
        std::memcpy(reinterpret_cast<uint8_t*>(&damage) + DmgLight, &lightning, sizeof(lightning));
    }
    const uint8_t srcDam = skill[SkSrcDam] != 0 ? skill[SkSrcDam] : 128;
    d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, uint8_t) noexcept>(d2r::sig::QueueHit)(game, unit, target, &damage, srcDam);
    d2r::ReleaseDamage(damage);
    return 1;
}
}  // namespace

auto InstallPowerStrike(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
    sigscan::Entry* local[] = {&UsedSkill, &ElemMin, &ElemMax};
    sigscan::Resolve(local, std::size(local), context->exeBase, nullptr, 0);
    const sigscan::Entry* uses[] = {&UsedSkill, &ElemMin, &ElemMax, &d2r::sig::SkillTargetUnit,
        &d2r::sig::SkillToHit, &d2r::sig::MeleeRollFlagMask, &d2r::sig::MeleeRoll,
        &d2r::sig::UnitSeed, &d2r::sig::QueueHit, &d2r::sig::EngineAllocator, &d2r::sig::SrvStTable};
    bool on = d2r::imageBase == context->exeBase && d2r::api.getSkillRecord &&
        std::all_of(std::begin(uses), std::end(uses), [](const auto* e) { return e->state == sigscan::State::Ok; });
    if (on) {
        const uint64_t slot = d2r::sig::SrvStTable.rva + 10 * sizeof(uint64_t);
        const uint64_t own = At<uint64_t>(reinterpret_cast<const void*>(context->exeBase + slot), 0);
        on = own != 0 && context->PatchWriteU64(slot, &own, sizeof(own), reinterpret_cast<uint64_t>(&SrvStPowerStrike));
    }
    plog::Info("power-strike: PD2 srvst 10 %s", on ? "on" : "FAILED (image, signatures or slot)");
    const size_t used = std::strlen(report);
    if (used < reportSize) {
        std::snprintf(report + used, reportSize - used, "; power-strike start%s", on ? "" : " FAILED");
    }
    return on;
}

PD2R_RULE(InstallPowerStrike, 390)
