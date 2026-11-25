// Shared by the item rule files in this folder (item_rules.cpp installs them in order): the includes and the names one
// rule file takes from another. Moved out of item_rules.cpp (#2198); the code is unchanged.
#pragma once
#include <intrin.h>
#include "../item_rules.h"
#include "../skill_functions.h"
#include "../skill/common.h"

#define NOMINMAX
#include <windows.h>
#include <D2RLPlugin/lifecycle.h>

#include "map_stack.h"
#include "sig_hook.h"
#include "pd2_engine.h"
#include "rathma_pair.h"
#include "rathma_split.h"
#include "log.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/mutations.h>
#include <D2RLPlugin/threads.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

namespace itemrules {

using d2r::At;
using d2r::Game;
using d2r::Unit;

using AddLifeFn = int32_t (*)(Unit*, int32_t) noexcept;
using ApplyCurseFn = int64_t (*)(Unit* target, d2r::CurseParams* params) noexcept;
using BossCurseFn = void (*)(Game*, Unit*, Unit*, int32_t) noexcept;
using MissileRollFn = uint64_t (*)(Unit*, Unit*) noexcept;
using ItemAuraTickFn = void (*)(Game* game, Unit* unit, int32_t key, int32_t skill, int32_t stateKey) noexcept;
using StatCallbackInitFn = uint64_t (*)(Unit* unit, uint32_t flags, void* callback, void* arg) noexcept;
using ScheduleReplenishFn = void (*)(Game* game, Unit* item) noexcept;
using ReplenishEventFn = void (*)(Game* game, Unit* item, int32_t type) noexcept;  // 0x4730d0 passes these three
constexpr int32_t SkillSmite = 97;
constexpr uint32_t StatChargedSkill = 204;  // item_charged_skill: charges | max << 8
constexpr uint32_t StatDye = 530;            // transform_dye
constexpr uint32_t StatEquippedEth = 368;  // equipped_eth
constexpr uint32_t StatMissingHp = 369;    // missing_hp
constexpr uint32_t StatLife = 6;
constexpr uint32_t StatMaxLife = 7;
constexpr uint32_t StatDurability = 72;
constexpr int BossCurseHostileCall = 0x23;  // e8 rel32: (game, caster, target) -> non-zero when the target is fair game
constexpr uint8_t BossCurseEntry[] = {0x44, 0x89, 0x4c, 0x24, 0x20, 0x55, 0x56, 0x41,
                                      0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec};
constexpr char BossCursePattern[] =
    "44 89 4c 24 20 55 56 41 54 41 56 41 57 48 8b ec 48 81 ec 80 00 00 00 45 8b f9 4d 8b f0 4c 8b e2 48 8b f1 "
    "e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 0f b6 8e 06 01 00 00 48 89 9c 24 b0 00 00 00 e8 ?? ?? ?? ?? 48 8b d8 "
    "48 83 b8 b8 11 00 00 42";
constexpr int PropertySlot = 26;           // PD2's func 25; D2R's 25 is affix-rand (tools/pd2_port.py FUNCS)
constexpr int32_t UnlimitedPortalCheck = 2, UnlimitedPortalUse = 2;
constexpr int32_t ItemType2han = 120, ItemType1han = 122;  // merged itemtypes rows (PD2's 114, 116)
auto Stat(Unit* unit, uint32_t stat) noexcept -> int32_t;
auto StatOf(Unit* unit, int32_t stat, int32_t layer = 0) noexcept -> int32_t;
auto DataTables(uint32_t dataFlag) noexcept -> const uint8_t*;
auto LayerShift(uint32_t dataFlag) noexcept -> uint32_t;
auto LayerMask(uint32_t dataFlag) noexcept -> uint32_t;
auto RefreshOpBase(Unit* unit, uint32_t stat) noexcept -> void;
auto AllocateRelay(uint64_t image) noexcept -> uint8_t*;
auto BossCurse(Game* game, Unit* caster, Unit* target, int32_t level) noexcept -> void;
auto CappedAddLife(Unit* unit, int32_t amount) noexcept -> int32_t;
auto ChargesReplenishEvent(Game* game, Unit* item, int32_t type) noexcept -> void;
auto CrushingBlow(Game* game, Unit* unit, Unit* target, d2r::CrushingBlowArgs* args) noexcept -> int32_t;
auto CurseApply(Unit* target, d2r::CurseParams* params) noexcept -> int64_t;
auto EquipAuraTick(Game* game, Unit* unit, int32_t key, int32_t skill, int32_t stateKey) noexcept -> void;
auto EquipStatCallbackInit(Unit* unit, uint32_t flags, void* callback, void* arg) noexcept -> uint64_t;
auto FindZeroChargeSkills(const D2RL::PluginContext* context) noexcept -> bool;
auto MissileStrike(Unit* unit, Unit* weapon) noexcept -> uint64_t;
auto PropSorcSkillRandCtc(uint8_t dataFlag, uint32_t a1, uint64_t a2, Unit* item, const void* property, uint8_t set,
                          uint16_t stat, uint16_t val, int32_t first, uint32_t a8, uint32_t a9, uint64_t a10) noexcept
	-> int32_t;
auto ReplenishSchedule(Game* game, Unit* item) noexcept -> void;
auto PatchWeaponCrit(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchHitEvents(const D2RL::PluginContext* context) noexcept -> bool;
extern const D2RL::PluginContext* g_context;
extern const D2RL::ThreadService* g_threads;
extern const D2RL::DataTableService* g_tables;
extern AddLifeFn g_addLifeOriginal;
extern ApplyCurseFn g_applyCurseOriginal;
extern std::atomic<uint32_t> g_blowRolls;
extern std::atomic<uint32_t> g_blows;
extern int32_t (*g_bossCurseHostile)(Game*, Unit*, Unit*) noexcept;
extern BossCurseFn g_bossCurseOriginal;
extern std::atomic<uint32_t> g_capBlocks;
extern std::atomic<uint32_t> g_capChecks;
extern std::atomic<uint32_t> g_chargesReplenished;
extern std::atomic<uint32_t> g_crits;
extern std::atomic<uint32_t> g_curseGate;
extern std::atomic<uint32_t> g_curseResisted;
extern std::atomic<uint32_t> g_curses;
extern std::atomic<uint32_t> g_deadlyStrikes;
extern std::atomic<uint32_t> g_equipCallbacks;
extern std::atomic<uint32_t> g_equipTicks;
extern std::atomic<uint32_t> g_ethReads;
extern ItemAuraTickFn g_itemAuraTickOriginal;
extern MissileRollFn g_missileRollOriginal;
extern std::atomic<uint32_t> g_phasing;
extern ReplenishEventFn g_replenishEventOriginal;
extern std::atomic<uint32_t> g_rolls;
extern ScheduleReplenishFn g_scheduleReplenishOriginal;
extern StatCallbackInitFn g_statCallbackInitOriginal;
extern std::atomic<uint32_t> g_strikeRolls;

}  // namespace itemrules

// Defined at global scope, as they were in item_rules.cpp.
using namespace itemrules;

using MagicMakerFn = uint64_t (*)(Unit**, void*) noexcept;
using ItemMakerFn = uint64_t (*)(void*, Unit**, void*) noexcept;
using StackEqualFn = int32_t (*)(Unit*, Unit*) noexcept;
using PierceRollFn = void (*)(Game*, Unit*, Unit*) noexcept;
using ClientPierceRollFn = void (*)(Unit*, Unit*) noexcept;
constexpr const char* PierceRollEntry =
    "40 53 56 57 48 83 ec 30 49 8b d8 48 8b fa 48 8b f1 4d 85 c0 74 12 48 85 d2 74 0d 48 8b ca e8 ?? ?? ?? ?? 83 f8 03 74 26";
constexpr uint8_t PierceRollStock[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49,
                                       0x8b, 0xd8, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xf1};  // whole instructions
constexpr const char* ClientPierceRollEntry =
    "48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57 48 83 ec 30 45 33 ff 48 8b fa 4c 8b f1 bd 01 00 00 00";
constexpr uint8_t ClientPierceRollStock[] = {0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74,
                                             0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57};  // whole instructions
void ClientPierceRoll(Unit* missile, Unit* owner) noexcept;
void __cdecl HookMagicMaker(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept;
auto InstallCharmRows(const D2RL::PluginContext* context) noexcept -> bool;
auto InstallDyeTint(const D2RL::PluginContext* context) noexcept -> const char*;
auto MapItemMaker(void* game, Unit** item, void* request) noexcept -> uint64_t;
auto MapStackEqual(Unit* a, Unit* b) noexcept -> int32_t;
auto PatchChillRates(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchCurseLength(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchDualWieldRate(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchHalfFreeze(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchShrinePotions(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchStunLength(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchTelekinesisArrows(const D2RL::PluginContext* context) noexcept -> bool;
auto PatchUnlimitedBooks(const D2RL::PluginContext* context) noexcept -> bool;
void PierceRoll(Game* game, Unit* missile, Unit* owner) noexcept;
auto PatchBytes(const D2RL::PluginContext* context, uint64_t rva, const uint8_t* expected, const uint8_t* bytes,
                       uint32_t size) noexcept -> bool;
extern ClientPierceRollFn g_clientPierceRollOriginal;
extern ItemMakerFn g_itemMakerOriginal;
extern std::atomic<uint32_t> g_mapMagicRefused;
extern std::atomic<uint32_t> g_mapStackRefused;
extern PierceRollFn g_pierceRollOriginal;
extern StackEqualFn g_stackEqualOriginal;
