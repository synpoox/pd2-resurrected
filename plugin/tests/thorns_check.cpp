// Exercise the actual event handler and D2RCore wrapper with engine calls replaced by host probes.
#include "../core-rules/skill/thorns.cpp"
#include <array>
#include <cstdlib>

namespace {
std::array<uint8_t, 0x200> wearer{}, enemy{}, missile{}, player{}, gameBytes{}, missileRow{};
Unit* owner = nullptr;
Unit* petOwner = nullptr;
int hits = 0, wounds = 0, physical = 0, lightning = 0, hitClass = 0, stateKey = 7, auraChance = 0;
uint32_t seed[2] = {1, 0};
uint8_t aura[8]{};
Unit* AsUnit(std::array<uint8_t, 0x200>& bytes) { return reinterpret_cast<Unit*>(bytes.data()); }
template<class T> void Put(void* p, int offset, T value) { std::memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof(value)); }
void Check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) { std::exit(1); }
}
int32_t GetStat(Unit*, int32_t stat, int32_t) noexcept { return stat == Physical ? 100 : stat == Lightning ? 200 : 0; }
const uint8_t* GetMissile(uint32_t, int32_t) noexcept { return missileRow.data(); }
Unit* GetOwner(Game*, Unit*) noexcept { return owner; }
Unit* GetPetOwner(Unit*) noexcept { return petOwner; }
int32_t GetKey(Unit*) noexcept { return stateKey; }
void SetKey(Unit*, int32_t key) noexcept { stateKey = key; }
void Hit(Game*, Unit*, Unit*, int32_t, void*) noexcept {}
void Deal(Game*, Unit*, Unit* target, void* damage) noexcept {
    Check(target == AsUnit(enemy), "damage goes to the attacker or missile owner");
    Check(stateKey == 2 && At<uint32_t>(damage, DamageResult) == 0x4021, "bank and reflected-hit flags");
    ++hits;
    physical = At<int32_t>(damage, DmgPhys);
    lightning = At<int32_t>(damage, DmgLight);
    hitClass = At<int32_t>(damage, DmgHitClass);
}
void* StateList(Unit*, int32_t) noexcept { return auraChance > 0 ? aura : nullptr; }
int32_t ListStat(uint32_t, void*, int32_t, int32_t) noexcept { return auraChance; }
uint32_t* Seed(Unit*) noexcept { return seed; }
int32_t OriginalDispatch(Game*, int32_t event, Unit* unit, Unit*, void*) noexcept {
    auto* node = At<uint8_t*>(unit, UnitEventNodes);
    Check(At<uint16_t>(node, EventNodeEvent) == 0xffff, "thorns hidden from D2RCore");
    node = At<uint8_t*>(node, EventNodeNext);
    Check(At<uint16_t>(node, EventNodeEvent) == event, "other item events preserved");
    return 42;
}
}
auto RedirectCall(const D2RL::PluginContext*, const char*, uint32_t, uint64_t, uint64_t*) noexcept -> bool { return false; }

namespace itemrules {
auto OpenWounds(Game*, Unit*, Unit*, uint32_t stat) noexcept -> void {
    Check(stat == Wounds, "open wounds uses stat 135");
    ++wounds;
}
}

int main() {
    d2r::sig::UnitStat.address = reinterpret_cast<uint64_t>(&GetStat);
    d2r::sig::MissileRecord.address = reinterpret_cast<uint64_t>(&GetMissile);
    d2r::sig::MissileOwner.address = reinterpret_cast<uint64_t>(&GetOwner);
    PetOwner.address = reinterpret_cast<uint64_t>(&GetPetOwner);
    d2r::sig::UnitStateKey.address = reinterpret_cast<uint64_t>(&GetKey);
    d2r::sig::SetUnitStateKey.address = reinterpret_cast<uint64_t>(&SetKey);
    d2r::sig::HitEvents.address = reinterpret_cast<uint64_t>(&Hit);
    d2r::sig::DealDamage.address = reinterpret_cast<uint64_t>(&Deal);
    d2r::sig::UnitStateList.address = reinterpret_cast<uint64_t>(&StateList);
    d2r::sig::ListStatValue.address = reinterpret_cast<uint64_t>(&ListStat);
    d2r::sig::UnitSeed.address = reinterpret_cast<uint64_t>(&Seed);
    auto* game = reinterpret_cast<Game*>(gameBytes.data());
    auto* unit = AsUnit(wearer);
    auto* target = AsUnit(enemy);
    Put(game, GameDataFlag, uint8_t{2});
    Put(target, UnitType, int32_t{1});
    Put(target, UnitMainFlags, uint32_t{4});
    Put(AsUnit(player), UnitMainFlags, uint32_t{4});
    auto run = [&](int stat, Unit* attacker, void* damage = nullptr) {
        return Thorns(game, 0, unit, attacker, damage, stat << 16, 0);
    };
    Check(run(Physical, target) == 1 && physical == 25600 && lightning == 0 && hitClass == 0x8d && wounds == 1,
        "physical thorns and one open-wounds attempt");
    Check(stateKey == 7, "restore wearer's bank");
    Check(run(Lightning, target) == 1 && physical == 0 && lightning == 51200 && hitClass == 0x4d && wounds == 1,
        "lightning thorns has no open-wounds roll");
    auraChance = 100;
    Check(run(Physical, target) == 1 && wounds == 3, "Thorns aura adds a second open-wounds attempt");
    auraChance = 0;
    Put(AsUnit(missile), UnitType, int32_t{3});
    owner = target;
    Put(missileRow.data(), MissileDamageRate, int32_t{256});
    Check(run(Physical, AsUnit(missile), aura) == 1 && physical == 6400, "missile owner gets DamageRate / 1024 reflection");
    Put(missileRow.data(), MissileDamageRate, int32_t{1});
    Check(run(Lightning, AsUnit(missile), aura) == 1 && lightning == 256, "positive missile reflection has a one-point minimum");
    Put(missileRow.data(), MissileDamageRate, int32_t{0});
    Check(run(Lightning, AsUnit(missile), aura) == 1 && lightning == 51200, "zero DamageRate keeps full reflection as in PD2");
    Check(run(Physical, AsUnit(missile)) == 1 && physical == 25600, "missile hit (event 0, no damage block) reflects");
    owner = nullptr;
    Check(run(Physical, AsUnit(missile), aura) == 0, "missing missile owner is ignored");
    Put(target, UnitMainFlags, uint32_t{0});
    Check(run(Physical, target) == 0, "non-attackable target is ignored");
    Put(target, UnitMainFlags, uint32_t{0x80000004});
    petOwner = AsUnit(player);
    Check(run(Physical, target) == 0 && run(Physical, AsUnit(player)) == 0, "players and player-owned pets are ignored");
    petOwner = nullptr;
    Put(target, UnitMainFlags, uint32_t{4});
    Check(run(Physical, nullptr) == 0, "missing attacker is ignored");
    std::array<uint8_t, 0x40> thorns{}, other{};
    Put(unit, UnitEventNodes, thorns.data());
    Put(thorns.data(), EventNodeEvent, uint16_t{3});
    Put(thorns.data(), EventNodeStat, int32_t{Lightning});
    Put(thorns.data(), EventNodeNext, other.data());
    Put(other.data(), EventNodeEvent, uint16_t{3});
    Put(other.data(), EventNodeStat, int32_t{198});
    std::array<uint8_t, 0x40> thorns2{};  // a second item's lightning thorns node: still one reflected hit
    Put(other.data(), EventNodeNext, thorns2.data());
    Put(thorns2.data(), EventNodeEvent, uint16_t{3});
    Put(thorns2.data(), EventNodeStat, int32_t{Lightning});
    g_dispatch = OriginalDispatch;
    g_missileDispatch = OriginalDispatch;
    const int before = hits;
    Check(Dispatch(game, 3, unit, target, aura) == 42 && hits == before + 1 &&
        At<uint16_t>(thorns.data(), EventNodeEvent) == 3 && At<uint16_t>(thorns2.data(), EventNodeEvent) == 3,
        "dispatch handles thorns exactly once and restores the nodes");
    for (auto* node : {thorns.data(), other.data(), thorns2.data()}) {
        Put(node, EventNodeEvent, uint16_t{0});
    }
    const int missileBefore = hits;
    Check(MissileDispatch(game, 0, unit, target, nullptr) == 42 && hits == missileBefore + 1 &&
        At<uint16_t>(thorns.data(), EventNodeEvent) == 0, "missile-hit dispatch handles thorns once with no damage block");
    return 0;
}
