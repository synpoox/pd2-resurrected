// PD2 event 6, ProjectDiablo.dll 0x10346140 (#2226), replacing D2R 93847's physical-only 0x582b20.
// Item nodes run through D2RCore's private event table: wrap its dispatcher as well as the aura slot.
// No SDK service exposes these nodes; both pointer writes use MutationService and signature-resolved sites.
#include "pd2_engine.h"
#include "log.h"
#include "registry.h"
#include "../item_rules.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

namespace itemrules {
auto OpenWounds(d2r::Game*, d2r::Unit*, d2r::Unit*, uint32_t) noexcept -> void;
}

namespace {
using d2r::At;
using d2r::Game;
using d2r::Unit;
using namespace d2r::rva;

constexpr int Physical = 78, Lightning = 128, Wounds = 135, ThornsState = 36, ThornsChance = 131;
constexpr int MissileDamageRate = 0x1c0;  // DamageRate: 1.13c +0x19c, D2R's table loader 0x3c2c70

constexpr auto Reflected(int32_t damage, int32_t rate) noexcept -> int32_t {
    return damage > 0 && rate > 0 ? std::max(static_cast<int32_t>(static_cast<int64_t>(damage) * rate >> 10), 1) : damage;
}
static_assert(Reflected(100, 1024) == 100 && Reflected(100, 256) == 25);
static_assert(Reflected(1, 1) == 1 && Reflected(100, 0) == 100 && Reflected(0, 256) == 0);

sigscan::Entry DispatchCall {"ThornsDispatch", "thorns", sigscan::Kind::Code, 0, 0,
    "4c 8b c5 8b d6 48 89 5c 24 20 48 8b cf e8 ?? ?? ?? ?? 48 8b 4c 24 30 8b d8"};
sigscan::Entry PetOwner {"ThornsPetOwner", "thorns", sigscan::Kind::Code, 0, 0,
    "40 53 48 83 ec 20 48 8b d9 e8 ?? ?? ?? ?? 83 f8 01 0f 85 ?? ?? ?? ?? 48 85 db"};
using DispatchFn = int32_t (*)(Game*, int32_t, Unit*, Unit*, void*, void*) noexcept;
DispatchFn g_dispatch = nullptr;

auto Stat(Unit* unit, int32_t stat, int32_t layer = 0) noexcept -> int32_t {
    return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(unit, stat, layer);
}

auto PlayerOwned(Unit* target) noexcept -> bool {
    // PD2 0x103749f0(unit, 1): walk up to two AI owners, rather than the unit's missile-owner fields.
    for (int i = 0; i < 2 && target != nullptr && At<int32_t>(target, UnitType) == UnitMonster; ++i) {
        target = d2r::Engine<Unit* (*)(Unit*) noexcept>(PetOwner)(target);
    }
    return target != nullptr && At<int32_t>(target, UnitType) == 0;
}

auto Thorns(Game* game, int32_t, Unit* unit, Unit* attacker, void* incoming, int32_t packed, int32_t) noexcept -> int32_t {
    if (game == nullptr || unit == nullptr || attacker == nullptr) {
        return 0;
    }
    int32_t rate = 0;
    if (At<int32_t>(attacker, UnitType) == 3) {
        if (incoming == nullptr) {
            return 0;
        }
        const auto* missile = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::MissileRecord)(
            d2r::DataFlag(game), At<int32_t>(attacker, UnitClassId));
        rate = missile != nullptr ? At<int32_t>(missile, MissileDamageRate) : 0;
        attacker = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::sig::MissileOwner)(game, attacker);
        if (attacker == nullptr) {
            return 0;
        }
    }
    const uint32_t flags = At<uint32_t>(attacker, UnitMainFlags);
    if ((flags & 4) == 0 || At<int32_t>(attacker, UnitType) == 0 ||
        ((flags & 0x80000000u) != 0 && PlayerOwned(attacker))) {
        return 0;
    }
    const uint32_t key = static_cast<uint32_t>(packed);
    const int32_t stat = key >> 16;
    const int32_t amount = Reflected(Stat(unit, stat, key & 0xffff), rate);
    if (amount <= 0 || (stat != Physical && stat != Lightning)) {
        return 0;
    }
    d2r::DamageBlock damage;
    damage.resultFlags = 0x4021;
    const uint32_t fixed = static_cast<uint32_t>(amount) << 8;
    std::memcpy(reinterpret_cast<uint8_t*>(&damage) + (stat == Physical ? DmgPhys : DmgLight), &fixed, sizeof(fixed));
    const int32_t hitClass = stat == Physical ? 0x8d : 0x4d;
    std::memcpy(reinterpret_cast<uint8_t*>(&damage) + DmgHitClass, &hitClass, sizeof(hitClass));
    const int32_t oldKey = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitStateKey)(unit);
    d2r::Engine<void (*)(Unit*, int32_t) noexcept>(d2r::sig::SetUnitStateKey)(unit, d2r::DataFlag(game));
    d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept>(d2r::sig::HitEvents)(game, unit, attacker, 1, &damage);
    d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(d2r::sig::DealDamage)(game, unit, attacker, &damage);
    d2r::Engine<void (*)(Unit*, int32_t) noexcept>(d2r::sig::SetUnitStateKey)(unit, oldKey);
    d2r::ReleaseDamage(damage);
    if (stat == Physical) {
        itemrules::OpenWounds(game, unit, attacker, Wounds);
        void* aura = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(unit, ThornsState);
        const int32_t chance = aura != nullptr ? d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(
            d2r::sig::ListStatValue)(d2r::DataFlag(game), aura, ThornsChance, 0) : 0;
        if (chance > 0 && d2r::RandomBelow(d2r::Engine<d2r::UnitSeedFn>(d2r::sig::UnitSeed)(unit), 100) < static_cast<uint32_t>(chance)) {
            itemrules::OpenWounds(game, unit, attacker, Wounds);
        }
    }
    return 1;
}

auto Dispatch(Game* game, int32_t event, Unit* unit, Unit* target, void* damage, void* list) noexcept -> int32_t {
    if (game == nullptr || unit == nullptr || target == nullptr || (event != 0 && event != 3)) {
        return g_dispatch(game, event, unit, target, damage, list);
    }
    struct Node { uint8_t* address; int32_t stat; };
    std::vector<Node> nodes;
    for (auto* node = At<uint8_t*>(unit, UnitEventNodes); node != nullptr; node = At<uint8_t*>(node, EventNodeNext)) {
        const int32_t stat = At<int32_t>(node, EventNodeStat);
        if (At<uint16_t>(node, EventNodeEvent) == event && (stat == Physical || stat == Lightning)) {
            nodes.push_back({node, stat});
        }
    }
    for (const auto& node : nodes) {
        *reinterpret_cast<uint16_t*>(node.address + EventNodeEvent) = 0xffff;
    }
    const int32_t result = g_dispatch(game, event, unit, target, damage, list);
    for (const auto& node : nodes) {
        *reinterpret_cast<uint16_t*>(node.address + EventNodeEvent) = static_cast<uint16_t>(event);
    }
    for (const auto& node : nodes) {
        Thorns(game, event, unit, target, damage, node.stat << 16, 0);
    }
    return result;
}
}  // namespace

auto InstallThorns(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
    sigscan::Entry* local[] = {&DispatchCall, &PetOwner};
    sigscan::Resolve(local, std::size(local), context->exeBase, nullptr, 0);
    const sigscan::Entry* uses[] = {&DispatchCall, &PetOwner, &d2r::sig::AuraEventTable, &d2r::sig::UnitStat,
        &d2r::sig::MissileRecord, &d2r::sig::MissileOwner, &d2r::sig::UnitStateKey, &d2r::sig::SetUnitStateKey,
        &d2r::sig::HitEvents, &d2r::sig::DealDamage, &d2r::sig::UnitStateList, &d2r::sig::ListStatValue,
        &d2r::sig::UnitSeed, &d2r::sig::EngineAllocator};
    bool on = d2r::imageBase == context->exeBase && std::all_of(std::begin(uses), std::end(uses),
        [](const auto* e) { return e->state == sigscan::State::Ok; });
    if (on) {
        on = SwapThunkSlot(context, DispatchCall, 13, reinterpret_cast<uint64_t>(&Dispatch), reinterpret_cast<uint64_t*>(&g_dispatch));
        if (on) {
            const uint64_t slot = d2r::sig::AuraEventTable.rva + 6 * sizeof(uint64_t);
            const uint64_t old = At<uint64_t>(reinterpret_cast<const void*>(context->exeBase + slot), 0);
            on = old != 0 && context->PatchWriteU64(slot, &old, sizeof(old), reinterpret_cast<uint64_t>(&Thorns));
            if (!on) {
                const uint64_t own = reinterpret_cast<uint64_t>(&Dispatch);
                (void)context->PatchWriteU64(ThunkSlotRva(DispatchCall, 13), &own, sizeof(own), reinterpret_cast<uint64_t>(g_dispatch));
            }
        }
    }
    plog::Info("thorns: PD2 event 6 %s", on ? "on" : "FAILED");
    const size_t used = std::strlen(report);
    if (used < reportSize) {
        std::snprintf(report + used, reportSize - used, "; thorns event 6%s", on ? "" : " FAILED");
    }
    return on;
}

PD2R_RULE(InstallThorns, 390)
