// PD2 Resurrected's engine facts for D2R 3.3.0 build 93847: the addresses, record offsets and structures only our
// plugins use, on top of d2rAI-toolkit's shared/plugin-common (its d2r.h and rvas_93847.h hold the shared ones).
// Include this instead of d2r.h. A value another project needs moves to the toolkit's rvas_93847.h, not a copy there.
//
// Every value was found in the Ghidra analysis of the dumped 93847 image; the comment beside each says where (the
// Split Throw spike's FINDINGS.md on branch research/split-throw, or the D2R function it was read from) and the game
// check that uses it. core-rules' names match the RVA column of docs/core-rules.md; another plugin's values sit in a
// section headed by that plugin and the work item that found them (core-ui: LP2, #212).
#pragma once

#include "d2r.h"  // the toolkit's: rva::ImageStamp, Game, Unit, MissileParams, api, Engine, At

#include <cstddef>
#include <cstdint>

namespace d2r::rva {

// --- Tables -------------------------------------------------------------------------------------------------------
// Client skill "do" functions, indexed by skills.txt cltdofunc: 130 slots (bound dword 0x235f7c4); 110-129 are free.
// Dispatcher 0x216d20 calls int32_t fn(Unit* caster, int32_t skillId, int32_t level); non-zero plays the do sound.
inline constexpr uint64_t CltDoTable = 0x235f7d0;
inline constexpr int CltDoSlots = 130;
inline constexpr int CltDoFirstFree = 110;
// Server skill "start" functions (srvstfunc): 91 slots, 71-90 free. Caller 0x43b3e0; 0 from the function aborts the cast.
inline constexpr uint64_t SrvStTable = 0x238e710;
inline constexpr int SrvStSlots = 91;
inline constexpr int SrvStFirstFree = 71;
// Client skill "start" functions (cltstfunc): 70 slots (bound dword 0x235f7c0), 62-69 free. Dispatcher 0x218030 calls
// int32_t fn(Unit* caster, int32_t skillId, int32_t level); 0 refuses the cast.
inline constexpr uint64_t CltStTable = 0x235f590;
inline constexpr int CltStSlots = 70;
inline constexpr int CltStFirstFree = 62;
// D2R's own slots PD2's rules call through (Find Potion / Find Item start, the plain melee do function)
inline constexpr int SrvStFindPotion = 33;
inline constexpr int SrvStFindItem = 34;
inline constexpr int CltStFindPotion = 26;
inline constexpr int CltStFindItem = 28;
inline constexpr int SrvDoMelee = 2;  // 0x430240: Bash's do function in D2R, applies the queued hit
inline constexpr int SrvDoPoisonDaggerSlot = 32;  // 0x55cc50: D2R's Poison Dagger do function, applies the queued hit
// srvdofunc 58, Revive: brings the skill's target corpse back in place as the caster's pet. It never registers the
// skill's auraevent1-4 on the pet (nor does anything else on that path), so Revive's event 32 needs core-rules.
inline constexpr int SrvDoRevive = 58;
inline constexpr uint64_t SrvDoReviveD2R = 0x55e7e0;
// srvdofunc 150, D2R's Smite do (also MonSmite's, UberTalicSmite's, MonSmiteLucion's): builds the hit on its stack,
// QueueHit's it, then ApplyQueuedHit (0x564930 decompiled, #165)
inline constexpr int SrvDoSmite = 150;
inline constexpr uint64_t SrvDoSmiteD2R = 0x564930;
// (game, attacker, target, &damage, flag): queues a hit in the attacker's list (+0x108, QueuedHit reads it); a hit
// that landed is result & 1 with none of 0x8380
inline constexpr uint64_t QueueHit = 0x44b600;
// mov [rsp+0x20],rbx / push rbp / push rsi / push r14 / sub rsp,0x30 / mov rbx,r9
inline constexpr uint8_t QueueHitEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x41,
                                            0x56, 0x48, 0x83, 0xec, 0x30, 0x49, 0x8b, 0xd9};
inline constexpr int EventDoMeleeDamage = 5;  // the event PD2's Smite roll stands in for
// srvstfunc 66: (game, unit, skill, level) -> 1, registers the skill's auraevent1-4 / auraeventfunc1-4 on unit for
// good (no stat list, no source), through 0x588670 and 0x438230 per event
inline constexpr uint64_t RegisterSkillEvents = 0x42fa90;
// 0x554c40: Teleport to the skill's point; 0 when there is no room, the level forbids it or the point collides (0x804)
inline constexpr int SrvDoTeleport = 27;
// Charge, D2R's own: srvst 31 (0x564dc0) aims the charge path, srvdo 67 (0x5622c0) runs it and hits at the end
inline constexpr int SrvStVanillaCharge = 31, SrvDoVanillaCharge = 67;
inline constexpr int SrvDoInferno = 19;  // 0x554460: D2R's (vanilla) Inferno; PD2's srvdo 182 hands monsters to it
// MissileParams::callback, run by the create (0x3bac90) from the table at 0x2381f30 as fn(missile, callbackArg).
// 1 (0x3b75c0, D2R's Firestorm, srvdo 117 / cltdo 67): frames capped at 77, seed from the arg, a type 10 path; it is
// PD2's maker callback 0x1036f220 but for the seed (DiabWall's 7 also skips the path 20% of the time)
inline constexpr int MissileCallbackMaker = 1;
// Player pets (D2R's PlayerPets.cpp), walked the way its pet add (0x501550, from the summon 0x4350e0) walks them
inline constexpr uint64_t UnitPlayerData = 0x34b240;  // (player) -> player data; its pet table at +0x98
inline constexpr int PlayerPets = 0x98;
inline constexpr uint64_t PetEntry = 0x5013f0;  // (dataFlag, pet table, pet type) -> the type's entry (stride 0x20)
// entry: first pet node (id +0x4, next +0x18), count, max (0x501830 stores the summon's petmax; 0x501ac0 asserts
// count <= max)
inline constexpr int PetHead = 0x0, PetCount = 0x8, PetMax = 0xc;
inline constexpr uint64_t FirstPet = 0x4ff1a0;  // (game, owner, pet type, 1) -> the type's first pet, or null
// (game, owner, pet id, 1): takes the pet off the list and kills it, as the pet add's loop over a group does
inline constexpr uint64_t RemovePet = 0x4ffd30;
// missiles.txt pSrvDoFunc: int64_t fn(Game*, Unit* missile), 54 slots, 46-53 free.
inline constexpr uint64_t MissileSrvDoTable = 0x2390e80;
inline constexpr int MissileSrvDoSlots = 54;
inline constexpr int MissileSrvDoFirstFree = 46;
// missiles.txt pSrvHitFunc: void fn(Game*, Unit* missile, Unit* hit), 71 slots, 61-70 free.
inline constexpr uint64_t MissileSrvHitTable = 0x2391030;
inline constexpr int MissileSrvHitSlots = 71;
inline constexpr int MissileSrvHitFirstFree = 61;
// missiles.txt pCltDoFunc: void fn(Unit* missile, int32_t a2), 92 slots, 78-91 free.
inline constexpr uint64_t MissileCltDoTable = 0x2358430;
inline constexpr int MissileCltDoSlots = 92;
inline constexpr int MissileCltDoFirstFree = 78;
// missiles.txt pCltHitFunc: int32_t fn(Unit* missile, Unit* hit), 81 slots (dispatcher 0x1b8e60, bound dword
// 0x2358a64), 66-80 free (and 6-8, 11, 15, 17, 20-23, 27, 35, 45, 49, 58, 59). Zero skips the hit's explosion.
inline constexpr uint64_t MissileCltHitTable = 0x2358710;
inline constexpr int MissileCltHitSlots = 81;
inline constexpr int MissileCltHitFirstFree = 66;
// missiles.txt pSrvDmgFunc: void fn(Game*, Unit* missile, Unit* hit, Damage*), 31 slots, 18-30 free. Called by srvhit 1
// (0x45bc40) while it builds the hit's damage.
inline constexpr uint64_t MissileSrvDmgTable = 0x2391270;
inline constexpr int MissileSrvDmgSlots = 31;
inline constexpr int MissileSrvDmgFirstFree = 18;
// skills.txt auraeventfunc1-4: int32_t fn(Game*, int32_t event, Unit* unit, Unit* target, void* damage, int32_t skill,
// int32_t level), 41 slots (dispatcher 0x43823a, bound 40), all of 1-40 filled. Slot 32 (0x42eea0, a skill damage
// event) is reached by no D2R row; PD2's Revive event takes it (docs/research/necromancer-w14.md).
inline constexpr uint64_t AuraEventTable = 0x238e5c0;
inline constexpr int AuraEventRevive = 32;
inline constexpr uint64_t AuraEvent32D2R = 0x42eea0;
// Slot 16 (0x583150) is D2R's crushing blow (itemstatcost item_crushingblow, Summon Goatman): chance = the unit's stat
// skill >> 16 at layer skill & 0xffff (item events pass stat << 16 | layer), roll % 100 on the unit's seed, then
// CrushingBlowApply. PD2's 16 is its own crushing blow with Smite's chance added (docs/research/w19-holy-sword-smite.md).
inline constexpr int AuraEventCrushingBlow = 16;
inline constexpr uint64_t AuraEvent16D2R = 0x583150;
// (game, unit, target, &CrushingBlowArgs) -> the event's result; called by D2R's event 16 after its roll
inline constexpr uint64_t CrushingBlowApply = 0x581bd0;

// Missiles (srvhit 4 is Exploding Arrow's hit, srvdo 7 / cltdo 7 Guided Arrow's homing, srvdo 1 / cltdo 1 the default)
inline constexpr uint64_t SrvHitSubMissiles = 0x45ddb0;  // srvhit 4 (game, missile, hit): HitSubMissile1-4 at the missile
inline constexpr uint64_t MissileSrvDoDefault = 0x463100;  // srvdo 1 (game, missile): move, collide, count down range
inline constexpr uint64_t MissileSrvEnd = 0x4639a0;  // (game, missile, hit, 1): what srvdo 1 does when range runs out
inline constexpr uint64_t MissileCltDoDefault = 0x1b5380;  // cltdo 1 (missile, a2)
inline constexpr uint64_t MissileOwner = 0x490300;  // (game, missile) -> owner unit or null
inline constexpr uint64_t ClientMissileOwner = 0x9a7b0;  // (missile) -> owner unit or null
inline constexpr uint64_t MissileSrvFrames = 0x3bb1e0;  // (missile) -> range frames left (server)
// (missile) -> frames run: the range minus MissileSrvFrames' frames left, 1 on the first step (D2R's cltdo 1 tests 1;
// 1.13c's cdo 69 the same as current == total - 1, D2Common #10737 / #10534)
inline constexpr uint64_t MissileCltFrames = 0x3bb0f0;
inline constexpr uint64_t PathSetTargetUnit = 0x342920;  // (path, unit): path +0x70 target unit
inline constexpr uint64_t PathSetTargetXY = 0x342a50;  // (path, x, y): path +0x10/+0x12 target point; clears +0x70
// (path, mask): path +0x64, what the path collides with (1.13c D2Common #11164, +0x50); missile create 0x5371a0 sets it
// from CollideType's row of the mask table 0x2391378 (CollideType 3: 0x184)
inline constexpr uint64_t PathSetCollisionMask = 0x342740;
inline constexpr uint64_t PathSteer = 0x340e70;  // (path, unit, 0, 1): re-aim the path at its target unit
inline constexpr uint64_t UnitDistance = 0x324fc0;  // (a, b) -> distance in subtiles
// (dataFlag, missile, owner, calc, -1, -1) -> value; dmg 1 0x453490 evaluates DmgCalc1 this way
inline constexpr uint64_t EvalMissileCalc = 0x3b4a90;
// The client's 3D offset and motion of a missile's graphic, as D2R's Meteor (cltdo 9, 0x1b6480) sets them (1.13c's
// D2Client 0xc7e30 and its writes to gfx +0x30 +0x10..0x18): (missile, x, y, z) << 11; (missile, x, y, z, shift),
// shift 0 stores the values as given (1.13c's raw writes), else << 11.
inline constexpr uint64_t SetMissileOffset = 0x193a80;
inline constexpr uint64_t SetMissileMotion = 0x193ad0;
// Power Strike's client do function (cltdo 22, 1.13c D2Client 0x15660) draws the strike only for the local player:
// `je` at this address skips its early return. PD2 has no such check (W6).
inline constexpr uint64_t CltDoStrikeLocalOnly = 0x2aed4a;

// Double Throw (D2R's own srvdo 74 0x567a20, cltdo 42 0x22f3f0, Chain Lightning's srvhit 12 0x45d5e0)
inline constexpr uint64_t UnitWeapon = 0x4242b0;  // (unit) -> weapon item the server attacks with, or null
inline constexpr uint64_t ClientUnitWeapon = 0x34b000;  // (unit) -> the client's weapon item, or null
inline constexpr uint64_t ItemIsType = 0x373890;  // (item, itemtypes row) -> non-zero when the item is of that type
inline constexpr uint64_t ItemMissileType = 0x371b60;  // (item) -> weapons.txt missiletype row
// (game, missile, unit, skill, level, UnitStateKey(unit), 0, 0, 0, 0, 1) -> missile thrown at the skill's target
inline constexpr uint64_t ThrowSkillMissile = 0x4333f0;
inline constexpr uint64_t ClientThrowSkillMissile = 0x213f50;  // (unit, missile, skill, level, UnitStateKey(unit))
inline constexpr uint64_t MissileBounces = 0x3bc0a0;  // (missile) -> chain count left (Chain Lightning's)
inline constexpr uint64_t SetMissileBounces = 0x3bd8a0;  // (missile, count); srvdo 139 sets it to uy - ty
inline constexpr uint64_t SetMissileTargetDx = 0x3bda20;  // (missile, tx - ux), set by srvdo 139 (0x585440)
inline constexpr uint64_t MissileSkill = 0x3bc450;  // (missile) -> skill id
inline constexpr uint64_t MissileLevel = 0x3bb470;  // (missile) -> skill level
// Blade Fury's fragment (W32, docs/research/w32-blade-fury.md). MissileBounces' field is 1.13c's missile data +0x28,
// the one PD2's fragment counts its generation in (D2Common #11018 / #10637).
inline constexpr uint64_t MissileActivateFrame = 0x3bae80;  // (missile) -> 1.13c's data +0x08 (D2R +0x0a)
inline constexpr uint64_t SetMissileFramesLeft = 0x3bd450;  // (missile, frames): what MissileSrvFrames reads
inline constexpr uint64_t UnitSize = 0x34b580;  // (unit) -> collision size: a missile's row Size, 2 for a player
// (room, x, y, mask) -> the mask's bits set at subtile x/y (0x27 with no room there), 1.13c's single-point test
inline constexpr uint64_t CollisionAt = 0x366250;
inline constexpr uint16_t CollisionMissile = 0x806;  // PD2's fragment tests this mask
// (game, owner, x, y, range, flags, skip unit id, 0) -> nearest target to x/y other than the skipped unit, or null
inline constexpr uint64_t NearestTarget = 0x4331d0;
inline constexpr uint64_t SetUnitStat = 0x2f7d10;  // (unit, stat, value, layer)

// Poison Dagger (W11, docs/research/necromancer-w11.md)
// (unit, &damage, skillId, level): adds the skill's elemental damage to a zeroed damage block (poison +0x38 = min +
// rand(max - min), length +0x3c). D2R's melee start (srvst 32, 0x567070) fills the queued hit with it.
inline constexpr uint64_t ElementalDamage = 0x437d00;
// int32[64] x and y offsets of D2's 64 directions (radius 30), the ones Nova's srvhit 5 (0x45fd20) aims at
inline constexpr uint64_t DirectionX = 0x1d1b650;
inline constexpr uint64_t DirectionY = 0x1d1b750;

// Self buff, the way Frozen Armor's srvdo 18 (0x553e60) builds its aurastate list
// (unit, state, self) -> 1 if it removed any: removes every state of the unit in the same states.txt group as
// `state` (the state itself too with self 1). A state with no group (0) removes nothing.
inline constexpr uint64_t RemoveSkillState = 0x4370b0;
// What 0x4370b0 does for each state it removes: state bit off (unit, state, 0), then the list detached (unit, list)
// and freed (dataFlag, list)
inline constexpr uint64_t ClearUnitState = 0x3354c0;
inline constexpr uint64_t DetachStatList = 0x2f7920;
inline constexpr uint64_t FreeStatList = 0x2f4180;
// (&StateListRequest) -> stat list or null. An older list of the state with the same skill and level only gets
// its timer moved and is returned as it is; a lower level returns null; any other older list is replaced.
inline constexpr uint64_t NewStateList = 0x433d20;
inline constexpr uint64_t UnitStateList = 0x2f5940;  // (unit, state) -> the unit's list of that state, or null
inline constexpr uint64_t ListSkill = 0x2f56a0;  // (list) -> its skill id
inline constexpr uint64_t ListLevel = 0x2f56e0;  // (list) -> its skill level
inline constexpr uint64_t AddAuraStats = 0x574480;  // (unit, list, skillRecord, skillId, level): aurastat1-6 (attackrate also sets stat 69)
inline constexpr uint64_t AddAuraStatsB = 0x574600;  // same arguments, Frozen Armor calls both
inline constexpr uint64_t SetListStat = 0x2f7c00;  // (dataFlag, list, stat, value, layer)
inline constexpr uint64_t UnitStateKey = 0x34b6c0;  // (unit) -> value Frozen Armor passes to 0x435920
inline constexpr uint64_t StateListKey = 0x435920;  // (dataFlag, list, key) -> stat 202 value
inline constexpr uint64_t SendSkillState = 0x438470;  // (game, unit, skill, level, key, 1, state, &aurastat, &calc, 4)
inline constexpr uint64_t SetUnitState = 0x3355a0;  // (unit, state, on)
inline constexpr uint64_t SkillStateExpired = 0x5551f0;  // Frozen Armor's expiry callback: removes the state

// Melee and units (same image as d2r-tools better-auras)
inline constexpr uint64_t ClientSkillTargetUnit = 0x9a570;  // (unit) -> target unit (client)
// (attacker, defender) -> the damage block of the attacker's queued hit on defender (unit +0x108 list), or null
inline constexpr uint64_t QueuedHit = 0x44d660;
// What D2R's Charge hit (srvdo 67) asks before it strikes
inline constexpr uint64_t FindUnit = 0x48fe80;     // (game, unit type, id) -> unit or null
inline constexpr uint64_t UnitInReach = 0x348650;  // (unit, target, reach, 0) -> non-zero when in reach
inline constexpr uint64_t UnitReach = 0x34aa90;    // (unit) -> 1 for a player, 3 for a monster
inline constexpr uint64_t UnitDead = 0x34c2c0;     // (unit) -> non-zero when dying or dead
inline constexpr uint64_t DataTables = 0x300a90;  // (dataFlag) -> data tables; states.txt row count at +0x298
// Leap, the way D2R's Leap (srvdo 77 0x568690, cltdo 43 0x22f9a0) and Leap Attack (srvdo 78 0x568920) run it
inline constexpr uint64_t GetUnitSkill = 0x33dd40;  // (unit, skillId, 0) -> the unit's skill (flags +0x14) or null
inline constexpr uint64_t LeapStart = 0x33e380;  // (unit, unitSkill) -> non-zero: leap path to the skill's point, type 9
inline constexpr uint64_t PathSetVelocity = 0x342ae0;  // (path, velocity): ignored while the skill has flag 0x1000
inline constexpr uint64_t LeapLanded = 0x56b5e0;  // (game, unit, unitSkill) -> non-zero once the unit has landed
inline constexpr uint64_t UnitRoom = 0x34b440;  // (unit) -> its room
// (game, unit, &AreaCenter, radius, unit mask, callback, arg, 0): callback(&AreaContext, victim) for each unit in range
inline constexpr uint64_t AreaUnits = 0x4398b0;
inline constexpr uint32_t AreaUnitMask = 0x8583;  // PD2's and AreaKnockback's default unit mask
// (game, unit, 0, 0, radius, &LeapKnockback, 0, &scratch): Leap's landing knockback (srvdo 77), callback 0x43a740
inline constexpr uint64_t AreaKnockback = 0x437820;
inline constexpr uint64_t SkillMeleeHit = 0x56b170;  // (game, unit, skillId, level): Leap Attack's hit on the skill target
inline constexpr uint64_t CltDoLeap = 0x22f9a0;  // D2R cltdo 43 (unit, skillId): Leap's client half
inline constexpr uint64_t RemoveUnitEvent = 0x48b890;  // (game, unit, event, key): 0 for most; an item aura passes its key
inline constexpr uint64_t AddUnitEvent = 0x48b720;  // (game, unit, event, frame, key, skill, stateKey): zeros for most; item aura ticks (0x437419) pass them
inline constexpr uint64_t MonStatsRecord = 0x976e0;  // (dataFlag, classId) -> monstats record or null
inline constexpr uint64_t UnitId = 0x34a330;  // (unit, file, line) -> unit id
// (unit, skill, level, target, 0) -> non-zero cast: a skill cast from unit at target as an event, sent to clients (aura
// event 33 0x42f300 casts this way); refuses while the unit has state 54
inline constexpr uint64_t CastEventSkill = 0x5896e0;
// Assassin charge-up finishers (W29, docs/research/w29-assassin-finishers.md)
inline constexpr int SrvDoChargeUpSlot = 34;  // 0x570a70: target, roll, queue the hit, AddCharge if it hits, apply
inline constexpr int SrvDoFireCharge1 = 38;  // 0x56ef30 and 0x571860: vanilla's later Fists of Fire charges
inline constexpr int SrvDoFireCharge2 = 39;
// (game, unit, skill, level, aurastate, aurastat1) -> 1: one charge on the aurastate list (made with stats 350/351
// if none), capped at 3, timer reset to auralencalc; aurastat2 grows by aurastatcalc2. srvdo 34 calls it on a hit.
inline constexpr uint64_t AddCharge = 0x56dae0;
// Release dispatcher (srvdo 1, 42, 46, 50, 52 call it with the queued hit): for each progressive state, n =
// min(max(charges, 1), 3) at max(list level, skill level); with prgstack it calls srvprgfunc1..n-1 first, setting the
// charge stat to i, then srvprgfunc n, all as (game, unit, skill, level), and sends SendChargePacket.
inline constexpr uint64_t SrvPrgDispatch = 0x56c3e0;
// (unit, target, skill, level, seed, 0, charges): the release packet; PD2's srvdo 170 sends 1.13c's after each hit
inline constexpr uint64_t SendChargePacket = 0x5396d0;
inline constexpr uint64_t ListStatValue = 0x2f5df0;  // (dataFlag, list, stat, layer) -> value
inline constexpr uint64_t NewStatList = 0x2f7300;  // (flags, expire frame, unit type, unit id) -> list or null
inline constexpr uint64_t AttachStatList = 0x2f2cb0;  // (unit, list, 1)
// Dragon Flight (W31): D2R's srvdo 52 (0x56fc80) fills its kick with this after the roll (MeleeRoll, below), on a
// d2r::LeapKnockback block with DmgPercent set
inline constexpr uint64_t MeleeFill = 0x573af0;  // (game, &block, unit, target, skill, level): the hit's damage

// Melee splash (W33): one victim's damage, built the way PD2's area callback 0x10303190 builds it
// (game, attacker, defender, &damage, 0, srcdam): weapon damage scaled by srcdam / 128 (0 = 128); Leap Attack's
// 0x44b600 fills its hit this way
inline constexpr uint64_t WeaponDamage = 0x44c030;
// D2R's area apply 0x436e90 (game, attacker, victim, &damage), which AreaKnockback's callback 0x43a740 calls, in steps:
inline constexpr uint64_t AvoidRoll = 0x44b930;  // (game, attacker, victim, 1, 0) -> 1 avoided, 2/4/0x10 block kinds
inline constexpr uint64_t UnitHasState = 0x3351b0;  // (unit, state) -> non-zero while the unit has it
inline constexpr uint64_t DamageStep = 0x5889e0;  // (attacker, victim, &damage)
inline constexpr uint64_t DamageEffects = 0x44df10;  // (game, attacker, victim, &damage): 0x44ce80 runs it for area hits
// (game, attacker, victim, area, &damage): with area 0, events 5 domeleedamage (attacker) and 1 (victim), as a melee
// hit's 0x44b2b0 calls it; with area 1, events 6 domissiledamage and 2
inline constexpr uint64_t HitEvents = 0x44ce80;
inline constexpr uint64_t DealDamage = 0x44a9b0;  // (game, attacker, victim, &damage)
// The whole area apply: the steps above with events 6 domissiledamage and 2 (1.13c's 0x10305760 in PD2's Holy Light)
inline constexpr uint64_t AreaApply = 0x436e90;  // (game, attacker, victim, &damage)
// Melee splash, the current skill's damage (W35, docs/research/w35-melee-splash-extras.md)
// (dataFlag, unit, skillId, level, 0) -> the skill's own physical min / max in 1/256 (MinDam/MaxDam with MinLevDam...;
// a kick's from strength and dexterity), the pair D2R's kick fill MeleeFill reads; 1.13c's D2Game 0x9f1a0 adds
// min + rand(max - min) to the hit's physical damage
inline constexpr uint64_t SkillPhysMin = 0x3385b0;
inline constexpr uint64_t SkillPhysMax = 0x338160;
inline constexpr uint64_t UnitSkillLevel = 0x33d1e0;  // (unit, unitSkill, bonus) -> level; bonus 0 = hard points
inline constexpr uint64_t ItemRecord = 0x314110;  // (dataFlag, item classId) -> items record (code +0x80, normcode +0x84)
inline constexpr int ItCode = 0x80;      // uint32 code
inline constexpr int ItNormCode = 0x84;  // uint32 normcode (items field table in 0x315fd0)
inline constexpr int TablesItemTypesCount = 0x1350;  // int64 itemtypes.txt row count in DataTables (ItemIsType's bound)
inline constexpr int SkEType = 0x284;  // uint8 EType (1.13c's 0x1dc)
// Conversion D2R's melee start (srvst 32, 0x567070) sets from a skill with an EType: type and percent (calc4), 1.13c's
// 0x65 / 0x68
inline constexpr int DmgConvType = 0x149;
inline constexpr int DmgConvPct = 0x14c;
// Holy Light (W17, docs/research/w17-holy-light.md)
// (unit, overlay, layer): overlay.txt row as stat 0xb2 on the given layer; 1.13c's D2Common #10689 has no layer (0).
// Asserts on a row past the table, so check DataTables + TablesOverlayCount first, as Leap Attack (0x56b170) does.
inline constexpr uint64_t SetOverlay = 0x349020;
// (unit) -> alignment: stat 172 for players and monsters (0 hostile, 1 neutral, 2 ally; pets 2), 2 for other types.
// 1.13c's D2Common #10830. D2RLoader hooks its entry: call it, never hook it (d2r-tools automap-marker notes).
inline constexpr uint64_t UnitAlignment = 0x2f4190;
// Client: (center unit, x, y, radius, unit mask, callback, arg, 0): callback(&ClientAreaContext, unit) for each unit
// in range; x/y 0 = the center unit's position, radius 0 does nothing (Fist of the Heavens' clthit 26 0x1adcb0)
inline constexpr uint64_t ClientAreaUnits = 0x2142d0;
inline constexpr uint64_t ClientFindUnit = 0x9a5d0;  // (id, type) -> the client's unit or null
// Vengeance (W18, docs/research/w18-vengeance-joust.md): the melee hit as D2R's Leap Attack hit (0x56b170) runs it
inline constexpr uint64_t SkillToHit = 0x339040;  // (unit, skillId, level) -> attack rating for the roll
// (game, unit, target, toHit, flag, 0) -> result flags (bit 0 hit); flag = skill byte SkRollFlags & the byte at
// MeleeRollFlagMask
inline constexpr uint64_t MeleeRoll = 0x44b720;
inline constexpr uint64_t MeleeRollFlagMask = 0x1d996d0;
inline constexpr uint64_t UnitHitClass = 0x3495d0;  // (unit) -> the value Leap Attack puts at DmgHitClass
// (game, unit, target, &damage, srcdam): weapon damage at srcdam / 128, queued on the unit for target
inline constexpr uint64_t QueueMeleeHit = 0x44b600;
inline constexpr uint64_t ApplyQueuedHit = 0x44b2b0;  // (game, unit, target, 0, 1)
// (dataFlag, unit, skillId, level, 1) -> ELen with ELevLen, the length ElementalDamage gives cold and poison
inline constexpr uint64_t ElementalLength = 0x337da0;
inline constexpr uint64_t MissileTargetType = 0x3bc190;  // (missile) -> target unit type, 6 for none
inline constexpr uint64_t MissileTargetId = 0x3bc280;  // (missile) -> target unit id
inline constexpr uint64_t EngineAllocator = 0x12063c0;  // () -> the allocator; vtable +0x20 frees (Leap Attack's cleanup)
// Static Field (W27), the way D2R's Static Field (srvdo 20 0x5546b0, per unit 0x5556a0) builds its hit
// (dataFlag, difficulty) -> difficultylevels.txt record (0x84 bytes); StaticFieldMin at +0x68
inline constexpr uint64_t DifficultyRecord = 0x300830;
inline constexpr int DiffStaticFieldMin = 0x68;
// (unit, &damage, etype, amount, length, &resist stat or null, &etype or null): adds amount to the etype's field
// (2 lightning: +0x2c, hit class +0x144 = 0x40; resist stat 41)
inline constexpr uint64_t AddElementDamage = 0x437980;
inline constexpr int DamageFlag148 = 0x148;   // uint8 D2R's Static Field sets to 1
// Desecrate (W13), the way D2R's summon helper 0x4350e0 (Bone Wall, Raise Skeleton) spawns and 0x509520 counts
inline constexpr uint64_t RoomAtXY = 0x38dfb0;  // (room, x, y) -> the room holding x/y near room, or null
inline constexpr uint64_t RoomLevelId = 0x2efc10;  // (room) -> levels.txt id
// (unit, x, y, mask) -> non-zero when the unit, at its size, collides at x/y; D2R's Teleport (srvdo 27, 0x554c40)
// refuses on it with mask 0x804 for levels Teleport 2, as PD2's teleport core does through its thunk 0x1030bf30
inline constexpr uint64_t UnitCollidesAt = 0x350a20;
// (game, room, x, y, monstats id, mode, -1, 0x42) -> monster or null; counts it in its level's monster region. D2R's
// Nest (0x57c590) passes its 1-based spawn index and 0 for the last two
inline constexpr uint64_t SpawnMonster = 0x492cf0;
// Monster kits (W36), the way D2R's Nest (srvdo 91, 0x57c590) and srvhit 6 (0x45ce90) spawn
inline constexpr uint64_t ClearSpawnSpot = 0x3636d0;  // (room, x, y, 1, 0x100): Nest calls it before spawning
inline constexpr uint64_t NestHold = 0x48e820;  // (unit, 0): Nest calls it before spawning; sets state 54 to the value
// (game, room, monstats id, x, y, mode, 0) -> monster or null: srvhit 6's spawn at the missile
inline constexpr uint64_t SpawnMonsterAt = 0x50a050;
inline constexpr int UnitMainFlags = 0x124;  // uint32, 1.13c's +0xc4 flags with the same bits
inline constexpr uint32_t UnitNoTreasure = 0x20000, UnitNoExperience = 0x4000000;  // Nest sets both on each spawn
// The current unit skill (rva::CurrentSkill) of a nest: where and what its start (srvst 49) set it to spawn
inline constexpr int USkillSpawnX = 0x24, USkillSpawnY = 0x28, USkillSpawnMon = 0x2c, USkillSpawnMode = 0x30;
inline constexpr int SkSumOverlay = 0x13c;  // int16 sumoverlay (1.13c +0xe6): Nest puts it on each spawn
// D2R's own do slots PD2's PoisonEggSpawn calls: Nova's and MaggotEgg's
inline constexpr int SrvDoVanillaNova = 22, SrvDoMaggotEgg = 84, CltDoVanillaNova = 25, CltDoMaggotEgg = 46;
inline constexpr int StatMana = 8, StatMaxMana = 9;  // in 1/256
// (unit): flags it (+0x124 bit 0x2000) for its room's update list; D2R's srvst 63 calls it on the corpse it takes
inline constexpr uint64_t QueueUnitUpdate = 0x38ebf0;
inline constexpr int StateCorpseTaken = 118;  // the state srvst 63 (and PD2's 67) puts on the corpse it uses
// Inferno / Arctic Blast (W21, channel-cast), the way D2R's Inferno (srvdo 19 0x554460 -> 0x551730, cltdo 24 0x2203f0)
inline constexpr uint64_t CurrentSkill = 0x34ba40;  // (unit) -> the unit skill it is using, or null
inline constexpr uint64_t UnitFrame = 0x349560;  // (unit) -> animation frame (+0x5c >> 8)
inline constexpr uint64_t SetUnitFrame = 0x34da20;  // (unit, frame): +0x5c and +0x60 = frame << 8
inline constexpr uint64_t SetUnitFrames = 0x34d990;  // (unit, n): +0x4c = n << 8, keeping its low byte
inline constexpr uint64_t SetUnitAnimSpeed = 0x34d8c0;  // (unit, n): +0x64 = n << 8
// (missile, MsAnimSpeed << 8 / range): cltdo 24 calls it on each flame it draws
inline constexpr uint64_t SetMissileAnimRange = 0x34da60;
inline constexpr uint64_t LocalPlayerSlot = 0x8b2d0;  // () -> slot of the local player
inline constexpr uint64_t ClientPlayer = 0x9a480;  // (slot) -> the client's player unit in that slot
// Mode-change gate for a busy player (modes 7-0x12), called by 0x42bfb0 (game, unit, mode, unitSkill, flag), PD2's
// gate A: (game, unit, requested mode, the request's unit skill or null, flag) -> 0 allow, 1 refuse, 4 buffer
inline constexpr uint64_t BusyModeGate = 0x42d840;
// mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi
inline constexpr uint8_t BusyModeGateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                                0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
inline constexpr uint64_t UnitSkillId = 0x33e080;  // (unitSkill, file, line) -> skills.txt id
// Mode-change gate for a player in get-hit, block or knockback (modes 4, 9, 0x13), called by 0x42bfb0 (game, unit,
// requested mode, flag byte) -> 0 allow, 1 refuse, 4 buffer: PD2's gate B, get-hit half (docs/research/w34-gate-b.md)
inline constexpr uint64_t HitRecoveryGate = 0x42d700;
// mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x20],rsi
inline constexpr uint8_t HitRecoveryGateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                                   0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20};
// The immediates of its two "mode end - game frame > 1" tests (cmp eax,1): walk / run -> buffer, the rest -> refuse
inline constexpr uint64_t HitRecoveryWindowBuffer = 0x42d7d2;
inline constexpr uint64_t HitRecoveryWindowRefuse = 0x42d808;
// (game, unit, event type, 0) -> earliest frame of the unit's timer events of that type (1 = mode end), 0 for none
inline constexpr uint64_t UnitEventFrame = 0x48b230;

// --- Offsets ------------------------------------------------------------------------------------------------------
inline constexpr int GameFrame = 0x170;  // Game: int32 frame counter (unit events are timed in it)

inline constexpr int UnitMonster = 1;
inline constexpr int UnitMode = 0x0c;  // int32 current mode (monmode for a monster)
inline constexpr int MonModeDeath = 0, MonModeDead = 12;
inline constexpr int PathTargetUnit = 0x70;  // Unit* on a dynamic path: what PathSteer aims at (set by PathSetTargetUnit)
inline constexpr int PathCollisionMask = 0x64;  // uint32 (set by PathSetCollisionMask)
inline constexpr int UnitData = 0x10;  // the unit's type data; a missile's holds its range and last unit hit
// int32 type and uint32 id (-1 none) of the last unit a LastCollide missile hit, set by D2Common 0x3bd580 in the
// collision 0x4639a0
inline constexpr int MdLastHitType = 0x20, MdLastHitId = 0x24;
inline constexpr int UnitOwnerType = 0xe8;  // int32 owner unit type (MissileOwner 0x490300 reads both)
inline constexpr int UnitOwnerId = 0xec;    // uint32 owner unit id
inline constexpr int TablesStateCount = 0x298;  // uint64 states.txt row count in DataTables
// charstats.txt in DataTables: records (0xd0 bytes) and count; D2R's Charge start reads the byte at +0x3d (1.13c +0x41)
// as the class's charge speed
inline constexpr int TablesCharStats = 0x1240;
inline constexpr int TablesCharStatsCount = 0x1248;
inline constexpr int CharStatsSize = 0xd0;
inline constexpr int CsChargeVelocity = 0x3d;
// DataTables: the progressive states (int16 state ids) and their count (int64), read by SrvPrgDispatch
inline constexpr int TablesProgressiveStates = 0x420;
inline constexpr int TablesProgressiveCount = 0x428;
inline constexpr int GameDifficulty = 0x104;  // uint8: 0 normal, 1 nightmare, 2 hell
// Game: per-level monster regions (array indexed by level id); a region's spawned-monster count at +0x2cc (0x509520)
inline constexpr int GameMonRegions = 0x1d0;
inline constexpr int MonRegionCount = 0x2cc;
// levels.txt in DataTables: records (0x18c bytes) and count; mon1-25 / nmon1-25 are int16, a negative one ends the list
inline constexpr int TablesLevels = 0x1460;
inline constexpr int TablesLevelCount = 0x1468;
inline constexpr int LevelRecordSize = 0x18c;
inline constexpr int LvlMon1 = 0x3e;
inline constexpr int LvlNMon1 = 0x70;
// monstats record (rva::MonStatsRecord)
inline constexpr int MonSpawn = 0x54;     // int16 spawn, -1 for none
inline constexpr int MonPartyMin = 0x62;  // uint8
inline constexpr int MonPartyMax = 0x63;  // uint8
inline constexpr int MonFlags = 0x3c;  // uint32 flags; lUndead bit 11, hUndead bit 12 (monstats field table 0x39db30)
inline constexpr uint32_t MonUndead = 0x1800;  // 1.13c's D2Common #10239 tests the same two bits
inline constexpr int TablesOverlayCount = 0x1230;  // int64 overlay.txt row count in DataTables

// Damage block QueuedHit returns: +0 hit flags (uint32), +4 result flags (uint16)
inline constexpr int DamageResult = 0x4;
inline constexpr uint16_t ResultHit = 0x1;
inline constexpr uint16_t ResultKnockback = 0x8;
inline constexpr uint16_t ResultAvoided = 0x8380;  // QueueHit's "did not land" bits
inline constexpr int UnitClassId = 0x04;  // int32 row in the unit's table: monstats for a monster, missiles.txt for a missile
inline constexpr int UnitIdField = 0x08;  // uint32, the id NearestTarget skips
inline constexpr int UnitSeqAnim = 0x40;  // pointer cltdo 24 tests to pick how it rewinds an Inferno caster

// Unit skill (rva::GetUnitSkill): flags and the target the hit looks up (0x56ad90)
inline constexpr int USkillFlags = 0x14;  // 0x80 leap starts, 0x100 in the air, 0x1000 path velocity locked
inline constexpr int USkillTargetType = 0x2c;  // 6 = none
inline constexpr int USkillTargetId = 0x30;
inline constexpr int USkillParam = 0x24;  // int32 Charge's counter, PD2's charge distance (its D2Common skill param)
inline constexpr int MonVelocity = 0x6a;  // int16 monstats Velocity
inline constexpr int MonRun = 0x6a;  // int16 monstats Run: D2R's field table (0x39db30) has Velocity at 0x68, Run at 0x6a

inline constexpr int MsHitPar1 = 0x70;  // int32 missiles.txt sHitPar1 (Chain Lightning's chain range, 0 = the skill's)
inline constexpr int MsHitPar2 = 0x74;  // int32 sHitPar2, sHitPar3 +0x78 (1.13c's 0x50 / 0x54 + 0x24)
inline constexpr int MsHitPar3 = 0x78;
inline constexpr int MsCltHitPar1 = 0x90;  // int32 cHitPar1-3 (1.13c's 0x6c-0x74 + 0x24)
inline constexpr int MsCltHitPar2 = 0x94;
inline constexpr int MsCltHitPar3 = 0x98;
inline constexpr int MsCltHitCalc1 = 0xb0;  // int32 CHitCalc1 (calc index; 1.13c's 0x8c + 0x24, before DmgCalc1)
inline constexpr int MsCltSubMissile1 = 0x40;  // int16 CltSubMissile1 (1.13c's 0x1e; D2R's cltdo 9 reads it)
// Damage block a pSrvDmgFunc edits: 1.13c's 0x08-0x2c fields sit 0x10 later (phys 0x08 -> 0x18 in srvdmg 1 and 13,
// poison length 0x2c -> 0x3c in srvdmg 11).
inline constexpr int DmgPhys = 0x18, DmgFire = 0x20, DmgLight = 0x2c, DmgMagic = 0x30, DmgCold = 0x34, DmgPoison = 0x38;
inline constexpr int DmgPoisonLength = 0x3c;
inline constexpr int DmgPercent = 0x1c;  // damage% (1.13c 0x0c), set before the melee fill (D2R's srvdo 52)
inline constexpr int DmgColdLength = 0x118;  // ElementalDamage's cold length (1.13c's 0x30)
// int32 the hit's total, 1.13c's 0x4c: DamageEffects (0x44df10) sets it to the sum of the fields after resists, and
// DealDamage (0x44a9b0) reads it
inline constexpr int DmgTotal = 0x134;
inline constexpr int DmgHitClass = 0x144;    // int32 Leap Attack's hit sets from UnitHitClass
inline constexpr int StatDamagePercent = 25;  // itemstatcost damagepercent, D2R and PD2
inline constexpr int StatPoisonMin = 57;  // poisonmindam, poisonmaxdam 58, poisonlength 59: D2R and PD2
inline constexpr int StatPoisonMax = 58;
inline constexpr int StatPoisonLength = 59;
inline constexpr int StatLife = 6;  // itemstatcost hitpoints (current life, in 1/256), D2R and PD2
inline constexpr int StatMaxLife = 7;  // maxhp, in 1/256
inline constexpr int StatLightResist = 41;  // lightresist, D2R and PD2
inline constexpr int StatVelocityPercent = 67;  // itemstatcost velocitypercent, D2R and PD2
inline constexpr uint32_t NearestTargetFlags = 0x8a783;  // Chain Lightning's search: 0x88583 | the 0xa783 the search adds

// Leeches in the same damage block (built by 0x465d60)
inline constexpr int DamageLifeLeech = 0x120;  // mana leech +0x124, stamina leech +0x128
inline constexpr int DamageManaLeech = 0x124;
inline constexpr int DamageStaminaLeech = 0x128;
// Cold length (1.13c's 0x30) and hit class (1.13c's 0x64), as ElementalDamage (0x437d00) and Hunger (0x576630) fill
// them
inline constexpr int DamageColdLength = 0x118;
inline constexpr int DamageHitClass = 0x144;  // uint32; D2R's Static Field ORs 0xd into it (0x4d, as PD2's)

// missiles.txt record (1.13c's shifted by 0x24)
inline constexpr int MsParam1 = 0x5c;     // int32 Param1 (1.13c's 0x38)
inline constexpr int MsDParam1 = 0x9c;    // int32 dParam1
inline constexpr int MsDParam2 = 0xa0;    // int32 dParam2 (1.13c's 0x7c)
inline constexpr int MsDmgCalc1 = 0xb4;   // int32 DmgCalc1 (calc index), read by dmg 1 0x453490
// uint32 HitFlags, uint16 ResultFlags (1.13c's 0xa8 / 0xac + 0x28; the field table at 0x3c2c70)
inline constexpr int MsHitFlags = 0xd0;
inline constexpr int MsResultFlags = 0xd4;
inline constexpr int MsCollideKill = 0x1ae;  // uint8 CollideKill (1.13c's 0x186 + 0x28; the field table at 0x3c4233)
inline constexpr int MsSkill = 0x1b6;     // int16 Skill (skills.txt row), read off a live explodingarrow row
// int16 HitSubMissile1 and CltHitSubMissile1 (1.13c's 0x24 / 0x2c), as D2R's clthit 26 (0x1adcb0) reads them
inline constexpr int MsHitSubMissile1 = 0x46;
inline constexpr int MsCltHitSubMissile1 = 0x4e;
inline constexpr int MsAnimSpeed = 0x1a0;  // uint8, the client create (0x1b7760) passes it to SetUnitAnimSpeed

// MissileParams::flags
inline constexpr uint32_t MissileFromOrigin = 0x820;      // start at the origin unit, fly to targetX/targetY
inline constexpr uint32_t MissileChannel = 0x8020;  // D2R's Inferno (srvdo 19, cltdo 24) and PD2's srvdo 182 / cltdo 111
inline constexpr uint32_t MissileNotTriggering = 0x20000;  // Multiple Shot's outer missiles, the ones calc3 leaves out
inline constexpr uint32_t MissileAtXY = 0x1;  // start at x/y
inline constexpr uint32_t MissileAimOffset = 0x2;  // targetX/targetY are offsets from the start (Nova's srvhit 5)
// PD2's charge spread (srvprg 171) sets it; CreateMissile hands it, with params +0x64, to the missile's setup 0x3bf9b0
inline constexpr uint32_t MissileFlag10000 = 0x10000;

// Stats Frozen Armor's srvdo 18 sets on its state list (itemstatcost ids)
inline constexpr int StatListSkill = 0x15e;  // 350: the skill that made the list
inline constexpr int StatListLevel = 0x15f;  // 351: its level
inline constexpr int StatListKey = 0xca;     // 202: rva::StateListKey's value

// skills.txt record (748 bytes in D2R)
inline constexpr int SkSrvDoFunc = 0x4e;  // uint16 srvdofunc, read by the dispatcher 0x43acb0
inline constexpr int SkCltMissileA = 0x140; // int16 cltmissilea; cltmissileb +0x142, cltmissilec +0x144
inline constexpr int SkCalc4 = 0x19c;  // skills field table at 0x306193
inline constexpr int SkParam1 = 0x1b8;  // int32 Param1-8, field table at 0x305e68
inline constexpr int SkParam2 = 0x1bc;
inline constexpr int SkParam3 = 0x1c0;
inline constexpr int SkParam4 = 0x1c4;
inline constexpr int SkParam5 = 0x1c8;
inline constexpr int SkParam6 = 0x1cc;
inline constexpr int SkRollFlags = 0x29;  // uint8 Leap Attack's hit masks with MeleeRollFlagMask for MeleeRoll's flag
inline constexpr int SkSrvPrgFunc1 = 0x50;  // int16 srvprgfunc1-3, read by SrvPrgDispatch
inline constexpr int SkSrcDam = 0x24d;  // uint8 SrcDam (0 = 128), read by Leap Attack's 0x56b170
// uint16 ResultFlags, uint32 HitFlags, int32 HitClass (1.13c's 0x12e / 0x130 / 0x134), as D2R's Hunger (0x576630)
// reads them into its damage block
inline constexpr int SkResultFlags = 0x186;
inline constexpr int SkHitFlags = 0x188;
inline constexpr int SkHitClass = 0x18c;
inline constexpr int SkSrvOverlay = 0x6e;  // int16 srvoverlay (skills field table 0x302380)
inline constexpr int SkAuraFilter = 0x70;  // int32 aurafilter, the area unit mask (clthit 26 reads it)
inline constexpr int SkCltCalc1 = 0x16c;  // int32 cltcalc1-3 (calc index; 1.13c's 0x114, PD2's cltprg 106 / 107)
inline constexpr int SkCltCalc2 = 0x170;
inline constexpr int SkCltCalc3 = 0x174;
inline constexpr int SkPetMax = 0x114;   // int32 petmax (calc index), which srvdo 119 hands the pet add
inline constexpr int SkAuraState = 0xa0;      // int16 aurastate
inline constexpr int SkAuraTargetState = 0xa2;  // int16 auratargetstate (skills field table 0x302380)
inline constexpr int SkAuraLenCalc = 0x80;    // int32 auralencalc: 1.13c's 0x60, before aurarangecalc as there
inline constexpr int SkAuraStat1 = 0x74;      // int16 aurastat1-6, read by AddAuraStats (0x574480)
inline constexpr int SkAuraStatCalc1 = 0x88;  // int32 aurastatcalc1-6 (calc index, pass to EvalCalc), same reader
// Two record fields Frozen Armor passes to SendSkillState by address; what they hold is not read yet.
inline constexpr int SkSendStateA = 0xa4;
inline constexpr int SkSendStateB = 0xac;

}  // namespace d2r::rva

namespace d2r {

// Argument of rva::NewStateList, laid out as Frozen Armor fills it on its stack.
struct StateListRequest {
	Unit* owner;               // 0x00
	Unit* unit;                // 0x08
	int32_t skill;             // 0x10
	int32_t level;             // 0x14
	int32_t frames;            // 0x18 how long the list lasts
	int32_t minusOne;          // 0x1c always -1
	int32_t zero;              // 0x20
	int32_t state;             // 0x24
	void (*expired)();         // 0x28 called when the list runs out (rva::SkillStateExpired)
};
static_assert(sizeof(StateListRequest) == 0x30);
static_assert(offsetof(StateListRequest, state) == 0x24);

// rva::AreaUnits: the centre (x, y 0 = the unit's position) and what its callback receives.
struct AreaCenter {
	void* room;  // rva::UnitRoom of the unit
	int32_t x, y;
};
struct AreaContext {
	void* game;
	Unit* unit;
	int64_t count;  // units the callback returned non-zero for
	void* arg;      // AreaUnits' arg
};
static_assert(sizeof(AreaContext) == 0x20);
// The client's (rva::ClientAreaUnits)
struct ClientAreaContext {
	Unit* center;
	int32_t count;  // units the callback returned non-zero for
	void* arg;
};
static_assert(offsetof(ClientAreaContext, arg) == 0x10);

// Argument of rva::CrushingBlowApply, as D2R's event 16 fills it on its stack (0x583223)
struct CrushingBlowArgs {
	void* damage;           // 0x00 the event's damage block
	int32_t divisors[3];    // 0x08 4, 10, 10
	uint8_t missile;        // 0x14 the event is 6 (domissiledamage)
};
static_assert(offsetof(CrushingBlowArgs, divisors) == 0x08);
static_assert(offsetof(CrushingBlowArgs, missile) == 0x14);

// The damage block D2R's Leap (srvdo 77) hands rva::AreaKnockback, filled as it fills it on its stack: hit flags 0,
// result flags 9, an empty inline list at +0x40 (data pointer, size, capacity 16 with the inline bit) and an empty
// tagged pointer at +0x158. The callback copies the block, so it stays as built and needs no cleanup.
struct LeapKnockback {
	uint32_t hitFlags = 0;     // 0x00
	uint32_t resultFlags = 9;  // 0x04
	uint8_t pad08[0x40 - 0x08] {};
	void* listData = inlineList;                          // 0x40
	uint64_t listSize = 0;                                 // 0x48
	uint64_t listCapacity = 0x8000000000000010ull;        // 0x50
	uint8_t inlineList[0xc0] {};                           // 0x58
	uint8_t pad118[0x158 - 0x118] {};
	uint64_t tagged[5] = {1, 0, 0, 0, 0};                  // 0x158
	LeapKnockback() = default;
	LeapKnockback(const LeapKnockback&) = delete;  // listData points into this object
};
static_assert(sizeof(LeapKnockback) == 0x180);
static_assert(offsetof(LeapKnockback, listData) == 0x40);
static_assert(offsetof(LeapKnockback, tagged) == 0x158);
// The same block as a plain hit's damage, for callers that build one (Rathma's bone spear)
using DamageBlock = LeapKnockback;

}  // namespace d2r

// --- Item rules: Ormus' Robes' property (docs/research/p12-ormus-robes.md) ------------------------------------------
namespace d2r::rva {

// properties.txt func1-7: int32_t fn(dataFlag, a1, a2, item, Property*, set, stat, val, first func's result, a8, a9,
// a10), from the property apply 0x3d4620; 37 slots (bound dword 0x2386bd8 = 37), 26-35 null
inline constexpr uint64_t PropertyFuncTable = 0x2386ab0;
inline constexpr int PropertyFuncSlots = 37;
// (dataFlag, a1, a2, item, Property*, set, stat, layer, value, a8, a9, a10): put one stat on the item's list, as
// every D2R property function does (value shifted by the stat's ValShift)
inline constexpr uint64_t PropertySetStat = 0x3d5940;
inline constexpr uint64_t ItemRandom = 0x3d5860;  // (item, min, max) -> min..max from the item's seed
inline constexpr int SkReqLevel = 0x214;    // uint16 reqlevel
inline constexpr int SkLocalDelay = 0x238;  // int32 compiled localdelay formula, -1 for none
// The stat-layer split item events use for skill/level layers: layer >> shift is the skill, layer & mask the level
inline constexpr int TablesSkillLayerShift = 0x14d0, TablesSkillLayerMask = 0x14d4;
// NPC menus (D2Client\src\UI\npcmenu.cpp): a vector of 0x48-byte records, one per NPC with a menu. +0 uint16 NPC
// class, +4 int32 entry count (the last entry is Cancel), +8 uint16 string id[5], +0x18 handler[5]. The client fills
// them from npc.txt and per-NPC setters (Kashya and Qual-Kehk get Hire, Tyrael Travel To Harrogath), then opens one
// with NpcMenuOpen.
inline constexpr uint64_t NpcMenus = 0x2a48830, NpcMenuCount = 0x2a48838;
inline constexpr int NpcMenuSize = 0x48, NpcMenuEntries = 0x04, NpcMenuStrings = 0x08, NpcMenuHandlers = 0x18;
inline constexpr int NpcMenuMaxEntries = 5;
inline constexpr int NpcMenuTyrael = 0x15;  // Tyrael's record (monster 367, tyrael2), as NpcMenuOpen picks it
inline constexpr uint16_t StrNpcMenuHire = 0xd45;
inline constexpr uint64_t NpcMenuHire = 0x112620;  // Hire's handler: opens HireMenuPanel, for any NPC
// (record, resurrect) -> 1: inserts or removes Resurrect before Hire, then the menu opens.
// push rbp / push rbx / push rdi / lea rbp,[rsp-0x47] / sub rsp,0x90
inline constexpr uint64_t NpcMenuOpen = 0x116480;
// The server's NPC table (D2Game\src\Unit\SUnitNpc.cpp, read by 0x3971a0): 43 entries of 0x1c bytes, +0 uint16 NPC
// class, +7 byte "hires mercenaries" (the hire handler 0x53d350 refuses an NPC without it). Kashya, Greiz, Asheara
// and Qual-Kehk have 1; Tyrael (entry 30) has 0.
inline constexpr uint64_t NpcTable = 0x237ea60;
inline constexpr int NpcTableSize = 0x1c, NpcTableHires = 7;
inline constexpr int NpcTableTyrael = 30;
inline constexpr uint16_t MonTyrael2 = 0x16f;
inline constexpr uint8_t NpcMenuOpenEntry[] = {0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0x6c, 0x24,
                                               0xb9, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00};

// --- Item rules: curse resistance and max curses (docs/research/ir4-curses.md) ------------------------------------
// D2Game\src\Skills\SkillNec.cpp: puts one curse on one unit, (target, &CurseParams) -> 1 applied. Every curse
// skill's srvdo reaches it once per target. It calls NewStateList (whose own entry D2RCore hooks), which removes the
// unit's first list flagged ListFlagCurse unless it is this state's, then sets aurastat2-6 on the list.
// mov [rsp+8],rbx / mov [rsp+0x18],rbp / mov [rsp+0x20],rsi / push rdi
inline constexpr uint64_t ApplyCurse = 0x55a7e0;
inline constexpr uint8_t ApplyCurseEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                              0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
// (game, target, stat, value) -> value: what ApplyCurse sets for each aurastat (a fifth of a resist cut on an immune)
inline constexpr uint64_t CurseStatValue = 0x55bfb0;
inline constexpr uint64_t StateIsCurse = 0x335a40;  // (dataFlag, state) -> non-zero for a states.txt curse
// unit +0x88: its stat list owner (+0x1c < 0 marks one); +0x90 its first chain of lists, the one NewStateList searches
inline constexpr int UnitListOwner = 0x88, ListOwnerMark = 0x1c, ListOwnerFirst = 0x90;
inline constexpr int ListFlags = 0x1c, ListNext = 0x68;  // uint32 flags, next list in the chain
// owner +0xaf0: the unit's state bits (uint32 words, bit state & 31), what UnitHasState (0x2f6220) reads. SetUnitState
// writes a second array after it, not this one.
inline constexpr int ListOwnerStates = 0xaf0;
inline constexpr uint32_t ListFlagTimed = 0x2, ListFlagCurse = 0x20;

// --- Item rules: skill while equipped and phasing (docs/research/ir7-equipped-items.md) ---------------------------
// The server's stat callback, (game, unit, item, stat << 32 | layer (a 64-bit r9), old, new), for stats with
// fCallback: a switch on stats 7-209 only (`lea eax,[r12-7]; cmp eax,0xca` at 0x46ff4a), item_aura (151) at
// 0x470470. Its entry is `jmp [rip+x]`: D2RCore hooks it, so core-rules wraps the pointer instead, where it is
// handed to StatCallbackInit.
inline constexpr uint64_t StatCallback = 0x46fdd0;
// (unit, flags, callback, arg): sets up the unit's stat list with its callback (unit creation 0x4252c0, 0x543120).
// mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi / push rbp
inline constexpr uint64_t StatCallbackInit = 0x2f65d0;
inline constexpr uint8_t StatCallbackInitEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                    0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x55};
// Unit event 9's handler, (game, unit, key, skill, stateKey): an item aura's tick. Casts the skill at item_aura's
// level for it (stat 151, layer skill) through SrvSkillDispatch (unit flag 0x20000000 and UnitStateKey = stateKey
// around the cast), then re-adds the event perdelay (calc +0x180, at least 5) frames on, on the frame grid; with no
// item_aura level (or a dead unit, or no aurastate) it removes the event. The item_aura case (0x581970) starts it.
// mov [rsp+0x20],rbx / push rbp / push rsi / push rdi / push r12 / push r14 / sub rsp,0x40
inline constexpr uint64_t ItemAuraTick = 0x437230;
inline constexpr uint8_t ItemAuraTickEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57,
                                                0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xec, 0x40};
inline constexpr int UnitEventItemAura = 9;
inline constexpr uint64_t SetUnitStateKey = 0x3e2b652;  // (unit, key): UnitStateKey's setter (a jmp stub)
// int32 masks ItemAuraTick tests the skill record's byte +0x24 with: either set, the aura ticks again
inline constexpr uint64_t SkillTickMaskA = 0x1d996e4, SkillTickMaskB = 0x1d996e8;
inline constexpr int SkFlags24 = 0x24;
inline constexpr int SkPerDelayCalc = 0x180;  // int32 perdelay calc index
inline constexpr uint32_t UnitFlagItemAuraCast = 0x20000000;
inline constexpr uint64_t SetUnitFlag = 0x34e140;  // (unit, flag, on)
// Server skill dispatcher (D2Game\src\Skills\Skills.cpp): (game, unit, skillId, level, a5, a6, a7) -> non-zero ran;
// calls srvdofunc. Called, not hooked: the Cast Triggers companion hooks it.
inline constexpr uint64_t SrvSkillDispatch = 0x43acb0;

// --- Cube (D2Game\src\Player\PlrTrade.cpp; core-maps' op 30) --------------------------------------------------------
// Recipe search: (result*, game, player, out*) walks cubemain in order and stores the first row that passes its filters
// (enabled, version, ladder, difficulty, class, numinputs == items in the cube, player ops 1-14) and whose 7 inputs all
// match: result {int32 row, uint8 found}. out is 3 x {item*, int32, int32}, zeroed before each row's inputs.
// push rbp / push rbx / push rsi / push rdi / push r12 / push r13 / push r14 / push r15 / lea rbp,[rsp-0x78] /
// sub rsp,0x178
inline constexpr uint64_t CubeRecipeSearch = 0x529090;
inline constexpr uint8_t CubeRecipeSearchEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                                    0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0x88,
                                                    0x48, 0x81, 0xec, 0x78, 0x01, 0x00, 0x00};
// (game, inventory, row, input 0-6, out*, used*) -> non-zero when that input matches (an empty input does); used is
// 0x30 int32 flags for the cube items already taken by earlier inputs, zeroed before each row
inline constexpr uint64_t CubeInputMatch = 0x529d50;
inline constexpr int CubeMatchUsedSlots = 0x30;
// It counts each matching cube item as one (qty at input +7, 0 means 1) and flags every match in used[k], k the item's
// place among the cube items in inventory order. mov [rsp+0x20],r9d / push rbp / push rbx / push rsi / push r13 /
// mov rbp,rsp / sub rsp,0x68 (read from the 6ab3782c dump, #315)
inline constexpr uint8_t CubeInputMatchEntry[] = {0x44, 0x89, 0x4c, 0x24, 0x20, 0x55, 0x53, 0x56, 0x41,
                                                  0x55, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x68};
inline constexpr int CubeInputQty = 7;
inline constexpr int CubeRecordSize = 0x16c, CubeNumInputs = 0xc;  // uint8 numinputs
// The search's numinputs filter: movzx eax,[row+0xc] / cmp eax,r13d (the cube's item count) / jne next row (#315)
inline constexpr uint64_t CubeNumInputsJump = 0x52922a;
inline constexpr uint8_t CubeNumInputsJumpD2R[] = {0x0f, 0x85, 0xbd, 0x06, 0x00, 0x00};
inline constexpr uint8_t CubeNumInputsJumpOff[] = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};  // 6-byte nop
inline constexpr int StatQuantity = 70;
inline constexpr uint64_t ItemLevel = 0x36ccf0;  // (item) -> item level (the matcher's out, 0x529d50)
inline constexpr uint64_t CubeRecipeCount = 0x3e3f20;  // (dataFlag) -> cubemain row count
inline constexpr uint64_t CubeRecipe = 0x3e3f60;       // (dataFlag, row) -> 0x16c-byte record, or null
inline constexpr int CubeEnabled = 0x0, CubeOp = 0x3;  // uint8 enabled, uint8 op
inline constexpr uint64_t UnitInventory = 0x34a360;    // (unit, file, line) -> the unit's inventory, or null
// The executor: (game, player, record, out*) runs the row the search found (out as the search filled it). The cube
// packet handler 0x4f2490 calls the search and then this; the cheat transmute 0x4127c0 calls it too.
// push rbp / push rbx / push rsi / push rdi / push r12 / push r13 / push r14 / push r15 / lea rbp,[rsp-0x15f8]
inline constexpr uint64_t CubeExecutor = 0x5269c0;
inline constexpr uint8_t CubeExecutorEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                                0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x08, 0xea, 0xff, 0xff};
inline constexpr uint64_t InventoryFirstItem = 0x388c10;  // (inventory) -> first item, or null
inline constexpr uint64_t InventoryNextItem = 0x38aba0;   // (item) -> next item in its inventory, or null
inline constexpr uint64_t ItemPage = 0x36cfe0;            // (item) -> item data +0x55: 0 inventory, 3 cube, 4 stash
inline constexpr uint64_t ItemCode = 0x36ef50;            // (item) -> items.txt code as a dword ('box ' 0x20786f62)
inline constexpr uint8_t CubePageCube = 3;
// Socketed items (#325), from the socket-insert path 0x46eb00 (D2Game\src\Items\ItemMode.cpp) and PD2's 0x3669b0. A
// socketed item lives in its parent's inventory, its stat list linked under the parent's. 0x46eb00 also applies the
// gems.txt mods (0x475140), which a moved gem already carries, so the reroll does the placement and link alone.
inline constexpr uint64_t CreateInventory = 0x3861c0;      // (item): gives the item an empty inventory
inline constexpr uint64_t InventoryRemove = 0x38ba30;      // (item) -> non-zero: out of whatever inventory holds it
inline constexpr uint64_t PlaceInSocket = 0x3856e0;        // (parent's inventory, item, 1) -> non-zero: in a socket
inline constexpr uint64_t StatListUnlink = 0x2f8290;       // (unused, unit): its stat list off its parent's
inline constexpr uint64_t StatListLink = 0x2f81a0;         // (parent, unit, 1): its stat list under the parent's
inline constexpr uint64_t SetItemStorePage = 0x36d730;     // (item, page): item data +0x54
inline constexpr uint64_t SetItemPage = 0x36de20;          // (item, page): item data +0x55 (ItemPage); 0xff socketed
inline constexpr uint64_t ItemStorePage = 0x36ca10;        // (item) -> item data +0x54
inline constexpr uint64_t UnitXY = 0x34a110;               // (unit) -> uint32: x low 16 bits, y high 16
// (game, player, item, item flags, command flags, mode, +0x55, +0x54, xy) sends the item to the clients with those
// values set for the send, then restores them (InvMode.cpp 0x536410 picks the packet by the flags). The cursor socket
// handler 0x4f9f40 refreshes the parent with item flag 1; PD2's 0x3669b0 marks the moved items with command flag 2.
inline constexpr uint64_t SendItem = 0x535f60;
inline constexpr int ItemModeSocketed = 6;

// --- core-ui: automap for level types 36+ (LP2, #212) -------------------------------------------------------------
// (levelType, tileType, style, sequence) -> cel or -1 (0xffffffff): the automap tile lookup. It matches AutoMap.txt
// rows of the type's range (36 ranges, so types 36+ read past them), style 0xff any, start sequence 0xff any, else
// start <= sequence <= end; then cels[Roll(seed) % count]. Decompiled for #212 (Ghidra, D2R.gpr); entry bytes read from
// the 6ab3782c dump. Game check (#212): core-ui's status counters climb in a PD2-only area with the automap open.
inline constexpr uint64_t AutomapCel = 0x32bdf0;
inline constexpr uint8_t AutomapCelEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                              0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20};
inline constexpr int AutomapLevelTypes = 36;  // AutoMap.txt loader 0x32cae0 matches 36 names (table 0x1cfe110)
inline constexpr uint64_t AutomapSeedPtr = 0x2372a40;  // pointer to the {lo, hi} seed 0x32bdf0 rolls (its decompile)

// --- core-ui: HD pictures of 4-character item codes (#310, #298) ---------------------------------------------------
// The items.json loader 0x6635a0 keys each entry by its code's first 3 characters plus a space (0x663a14 writes 0x20
// to byte 3), while the picture lookup 0x662fb0 (case 4) searches by the item record's whole code dword (+0x80). So a
// 4-character code never finds its entry (tpgs draws nothing) and overwrites the entry of the 3-character code it
// starts with (r01s replaces r01's picture). The patch keeps byte 3: the key's last two bytes are loaded as a word and
// byte 3 becomes c[3] | 0x20 (a 3-character code's terminator gives the space D2R wrote; every PD2 code's 4th
// character already has bit 0x20, which tools/pd2_items.py checks). The 2-character path, which jumped to the
// removed store, jumps to D2R's own double-space store at 0x663a07. Decompiled and read from the 6ab3782c dump (#310).
inline constexpr uint64_t ItemsJsonKey = 0x6639f9;
// jmp 0x663a14 / mov [rsp+25],al / movzx eax,byte [rsi+2] / test al,al / jne 0x663a10 / mov word [rsp+26],0x2020 /
// jmp 0x663a19 / mov [rsp+26],al / mov byte [rsp+27],0x20
inline constexpr uint8_t ItemsJsonKeyD2R[] = {0xeb, 0x19, 0x88, 0x44, 0x24, 0x25, 0x0f, 0xb6, 0x46, 0x02, 0x84,
                                              0xc0, 0x75, 0x09, 0x66, 0xc7, 0x44, 0x24, 0x26, 0x20, 0x20, 0xeb,
                                              0x09, 0x88, 0x44, 0x24, 0x26, 0xc6, 0x44, 0x24, 0x27, 0x20};
// jmp 0x663a07 / mov [rsp+25],al / movzx eax,word [rsi+2] / test al,al / jne 0x663a10 / mov word [rsp+26],0x2020 /
// jmp 0x663a19 / or ah,0x20 / mov [rsp+26],ax / nop
inline constexpr uint8_t ItemsJsonKeyFull[] = {0xeb, 0x0c, 0x88, 0x44, 0x24, 0x25, 0x0f, 0xb7, 0x46, 0x02, 0x84,
                                               0xc0, 0x75, 0x09, 0x66, 0xc7, 0x44, 0x24, 0x26, 0x20, 0x20, 0xeb,
                                               0x09, 0x80, 0xcc, 0x20, 0x66, 0x89, 0x44, 0x24, 0x26, 0x90};
static_assert(sizeof(ItemsJsonKeyD2R) == sizeof(ItemsJsonKeyFull));
// The `cheat spawn` handler 0x40f670 builds its item-code key the same way (0x40fc39-0x40fc6e: the first 3 characters,
// spaces after a short code, a space in byte 3), then compares it with each item record's code (+0x80), so a 4-character
// code spawns nothing (#323). The rewrite reads the code as one dword (the token sits in the handler's 128-byte stack
// buffer), fills spaces after a 1- or 2-character code, and sets byte 3 to c[3] | 0x20 as above. On entry r8 holds the
// lowered code and cl its first character; 0x40fc6e is the compare loop. Read from the 6ab3782c dump.
inline constexpr uint64_t SpawnCheatKey = 0x40fc39;
// mov [rsp+50],cl / movzx eax,byte [r8+1] / test al,al / jne +9 / mov word [rsp+51],0x2020 / jmp 0x40fc69 /
// mov [rsp+51],al / movzx eax,byte [r8+2] / test al,al / jne +9 / mov word [rsp+52],0x2020 / jmp 0x40fc6e /
// mov [rsp+52],al / mov byte [rsp+53],0x20
inline constexpr uint8_t SpawnCheatKeyD2R[] = {0x41, 0x0f, 0xb6, 0x40, 0x01, 0x88, 0x4c, 0x24, 0x50, 0x84, 0xc0, 0x75,
                                              0x09, 0x66, 0xc7, 0x44, 0x24, 0x51, 0x20, 0x20, 0xeb, 0x1a, 0x88, 0x44,
                                              0x24, 0x51, 0x41, 0x0f, 0xb6, 0x40, 0x02, 0x84, 0xc0, 0x75, 0x09, 0x66,
                                              0xc7, 0x44, 0x24, 0x52, 0x20, 0x20, 0xeb, 0x09, 0x88, 0x44, 0x24, 0x52,
                                              0xc6, 0x44, 0x24, 0x53, 0x20};
// mov eax,[r8] / mov ecx,0x20202020 / test ah,ah / je one / test eax,0xff0000 / je two / or eax,0x20000000 / jmp store /
// one: mov cl,al / mov eax,ecx / jmp store / two: mov cx,ax / mov eax,ecx / store: mov [rsp+50],eax / 12 nops
inline constexpr uint8_t SpawnCheatKeyFull[] = {0x41, 0x8b, 0x00, 0xb9, 0x20, 0x20, 0x20, 0x20, 0x84, 0xe4, 0x74, 0x0e,
                                               0xa9, 0x00, 0x00, 0xff, 0x00, 0x74, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x20,
                                               0xeb, 0x0b, 0x88, 0xc1, 0x89, 0xc8, 0xeb, 0x05, 0x66, 0x89, 0xc1, 0x89,
                                               0xc8, 0x89, 0x44, 0x24, 0x50, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                                               0x90, 0x90, 0x90, 0x90, 0x90};
static_assert(sizeof(SpawnCheatKeyD2R) == sizeof(SpawnCheatKeyFull));

// --- core-ui: legacy palette of the level entered (#296) -----------------------------------------------------------
// (act byte) -> void: loads data/global/palette/act<N+1>/pal.pl2 (legacy floor, light and colormap tables) and
// updates the palette texture. Only the load screen's end calls it with a level's palette (0xc5f80 passes
// ActPalette), so a level entered without one (a map's portal from Harrogath) keeps the last act's. Decompiled for #296.
inline constexpr uint64_t LoadActPalette = 0x9041a0;
// (act byte) -> void: sets ActPalette. The client's unit placement 0x9bf00 sets it from the level record's Pal,
// the act load 0x86bc0 from the act. The global it writes: 0x2a2adc0.
inline constexpr uint64_t SetActPalette = 0xcd6d0;
inline constexpr uint64_t LevelRecord = 0x32c4a0;  // (dataFlag, level id) -> levels.txt record
inline constexpr int LvlPal = 0x0c;  // uint8 Pal (0x9bf00 reads it for SetActPalette; Teleport is +0x0e)

// --- core-maps: opening a map (MS2, #217; D2Game\src\Skills\SkillItem.cpp) ----------------------------------------
// Item spell dispatchers, (game, player, item, target): the check -> bool, then the use (whose caller frees an item
// whose spell flag is 0, as slot 16's empty entry is). Both index D2R's 20-entry item spell table (0x236fe30) by the
// item's pSpell; slots 16-19 are empty. Decompiled for #217; entry bytes read from the 6ab3782c dump.
// mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi
inline constexpr uint64_t ItemSpellCheck = 0x5817f0;
inline constexpr uint8_t ItemSpellCheckEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                                  0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
// mov [rsp+0x18],rbx / mov [rsp+0x20],rbp / push rsi / push rdi / push r15
inline constexpr uint64_t ItemSpellUse = 0x581680;
inline constexpr uint8_t ItemSpellUseEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c,
                                                0x24, 0x20, 0x56, 0x57, 0x41, 0x57};
inline constexpr uint64_t UnitClassOf = 0x349860;  // (unit, file, line) -> class id (the dispatchers' item row)
inline constexpr int ItemPSpell = 0x94;   // int32 misc pSpell in the items record (ItemRecord)
inline constexpr int ItemLenCalc = 0xb0;  // uint32 misc len calc; the timed potion handler 0x57ecb0 evaluates it
inline constexpr uint64_t ItemCalc = 0x3b4880;  // (dataFlag, player, item, calc) -> value
// (game, player, key, level, max) -> 1 opened: the cube's "Red Portal" output (0x52aa00 passes its key, level, max).
// The player must stand in town; it finds a free spot, makes red portal 60 to the level (same act only), and counts
// the opening in the game's counter for key; with max > 0 it refuses once the counter reaches max. A refusal sends
// client message 0x15.
inline constexpr uint64_t OpenRedPortal = 0x5da800;
// What OpenRedPortal does first, to find the portal's spot: the unit's (x, y), then a search around it, (game,
// room, int xy[2] in/out, 3, 0x400, room** found, 5, 100); *found stays null when there is no free spot. Reads only.
inline constexpr uint64_t UnitCoords = 0x34a040;  // (unit, int32 xy[2])
inline constexpr uint64_t FindFreeSpot = 0x517f90;
inline constexpr uint64_t GameCounter = 0x5d88b0;  // (game + GameCounters, &key) -> int32* counter (made if missing)
inline constexpr int GameCounters = 0x1aac8;
// (questRecord, quest, bit) -> non-zero when set (D2Common QuestRecord.cpp); a player's record for a difficulty is
// at player data + 0x40 + difficulty * 8 (the item-use quest checks in 0x4734f0). Bit 0: the quest is done.
inline constexpr uint64_t QuestFlag = 0x325c50;
inline constexpr int PlayerQuests = 0x40;
inline constexpr uint64_t ClientMessage = 0x491960;  // (player, message, player); 0x15 is "can't do that"
// (*(game + GameClients), packet): sends a chat packet 0x26 (type at +1, unit type +3, unit id +4, colour +8, name
// at +10 up to 60 bytes, text at +0x47 up to 255) to the game's clients; it breaks on longer text. D2R's own
// GameChat (0x4016c0, the debug commands' "Warping to level") sends type 1 with unit type 2, which the client drops
// (#520); type 4 or unit type 0 shows. Called, not hooked.
inline constexpr uint64_t SendChat = 0x477a30;

// --- core-maps: opening an uber fight (UB2, #227; D2Game\src\Game\Clients.cpp) ----------------------------------
// Decompiled for #227 (Ghidra, D2R.gpr). Called, not hooked.
// (game, player, level, 0): moves the player into the level. In the same act it finds the level's arrival spot
// (0x2ef8b0) and teleports there (0x491ca0); else it changes act. The debug "Warping to level" handler 0x402820 calls it.
inline constexpr uint64_t WarpToLevel = 0x488180;
// The game's clients: a list at game +0x138, the next client at +0x628 (0x484d90 walks it); (client, 0) -> its player
inline constexpr int GameClients = 0x138, ClientNext = 0x628;
inline constexpr uint64_t ClientUnit = 0x484c90;

// --- core-maps: tier-0 re-entry portals and the area rules on entry (UB2, #346; D2Game\src\Objects\ObjMode.cpp) ----
// Decompiled for #346 (Ghidra, D2R.gpr). Called, not hooked; one table slot written.
// Object operate table: 75 slots of int32_t fn(ObjectOperate*), indexed by objects.txt OperateFn (objects record
// +0x15e). Its dispatcher 0x592c40 skips a null slot. D2R leaves 60 null; PD2's portal operate 77 takes it (the merge's
// pd2_ids.FUNCS). D2R's own portal operate 15 (0x58f680) needs a linked partner portal, which PD2's grey portals lack.
inline constexpr uint64_t ObjectOperateTable = 0x23a51c0;
inline constexpr int ObjectOperatePortal = 60;
inline constexpr int OperateWaypoint = 23;  // 0x58fe00, the waypoints' OperateFn
inline constexpr uint64_t ObjectRecord = 0x38fd00;  // (dataFlag, objects row) -> objects record (0x168 bytes) or null
inline constexpr int ObjOperateFn = 0x15e;   // uint8 OperateFn
inline constexpr int ObjOpenFrames = 0x9c;   // uint32; >> 8 is the frames the portal maker waits before event 1
// How the portal maker 0x432ce0 makes a portal, without its return portal (0x435dd0):
// (unit type, row, x, y, game, room, 1, 1, 0, 0) -> unit; for an object its InitFn runs (12: mode 1, the red portals')
inline constexpr uint64_t SpawnUnit = 0x490410;
inline constexpr uint64_t RoomAt = 0x38dfb0;          // (room, x, y) -> the room near `room` holding x/y, or null
inline constexpr uint64_t SetObjectMode = 0x34e5f0;   // (object, mode)
inline constexpr int UnitEventOpen = 1;               // the event that opens a portal (mode 1 -> 2)
inline constexpr uint64_t SetPortalLevel = 0x34e9d0;  // (object, level byte): the portal's destination
inline constexpr uint64_t PortalLevel = 0x34ad40;     // (object) -> uint16 destination
// How operate 15 removes a used town portal: (room, unit type, unit id) tells the room's clients, (game, unit) frees
// the unit
inline constexpr uint64_t RoomUnitRemoved = 0x2ef4d0;
inline constexpr uint64_t RemoveUnit = 0x48faa0;
// (room, 0) sets bit 0x400000 of the room's level-gen room (+0x18, flags +0x50; 0x328860), (room, 1) clears it. The
// portal maker sets it on each portal's room, operate 15 clears it on removal: an unmarked room is freed with its units
// once no player is near (#431: the grey portals were gone when the player came back)
inline constexpr uint64_t RoomPortalMark = 0x2f0530;
// The room-release pass 0x4088c0 (PD2's D2Game+0x2cf30) asks (act, room) -> nonzero for each room 10+ ticks without a
// player; a yes frees the room and its units (0x504260 each, then 0x2f0110). Its only caller. 0 when the room is marked.
inline constexpr uint64_t RoomReleasable = 0x2f0630;
inline constexpr uint8_t RoomReleasableEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9,
                                                  0x80, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xda};
// The room walk of 0x593630 (finds an object by class near a unit): (room, &rooms, &count) the room and its
// neighbours, (room) its first unit, (unit) the next unit in its room
inline constexpr uint64_t NearRooms = 0x2efde0;
inline constexpr uint64_t RoomFirstUnit = 0x2efd90;
inline constexpr uint64_t NextUnitInRoom = 0x34b4a0;
// The game's monsters: 128 lists by unit id & 0x7f at game +0x2630 (0x4925b0), the next unit at +0x158 (FindUnit)
inline constexpr int GameMonsters = 0x2630, UnitListNext = 0x158;

// --- core-maps: map stats on monsters and players (MS3, #218) -----------------------------------------------------
// Decompiled for #218 (Ghidra, D2R.gpr); entry bytes read from the 6ab3782c dump.
// (spawn params, monster): D2R's per-class setup of a monster that its spawn core 0x493cc0
// (D2Game\src\Monster\MonsterSpawn.cpp) just made; 0x493cc0 is its only caller, and 0x492cf0 and the other spawners
// all go through 0x493cc0. The monster's stats and its monstats Align alignment (0x48e600) are set by then.
// mov [rsp+0x18],rbx / mov [rsp+0x20],rbp / push rsi / push rdi / push r15
inline constexpr uint64_t MonsterSpawned = 0x494a60;
inline constexpr uint8_t MonsterSpawnedEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c,
                                                  0x24, 0x20, 0x56, 0x57, 0x41, 0x57};
// (game, player, a, b): the player's life regen event (unit event 3, D2Game\src\Player\PlrModes.cpp). It adds itself
// again for the next frame first, so it runs every frame for every player, dead or alive.
// mov [rsp+0x20],rbx / push rbp / sub rsp,0x40 / mov eax,r9d / mov dword [rsp+0x30],0
inline constexpr uint64_t PlayerRegenEvent = 0x42e600;
inline constexpr uint8_t PlayerRegenEventEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x48, 0x83, 0xec, 0x40, 0x41,
                                                    0x8b, 0xc1, 0xc7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00};
inline constexpr int MonAlign = 0x87;  // uint8 monstats Align (rva::MonStatsRecord); 0x493cc0 reads it for 0x48e600
inline constexpr uint64_t SetListState = 0x2f72f0;  // (list, state): list +0x20 = state (0x49bef0, 0x339d70 use it)
// (unit, state, flags) -> the unit's stat list of that state and flags, or null; (item, 0, 0x40) is the item's magic
// list. List +0x30 entries {int32 layer, stat, value, extra}, +0x38 uint64 count (d2rAI-toolkit runeword-stat-list.md)
inline constexpr uint64_t UnitFlagList = 0x2f5b80;
inline constexpr int ListEntries = 0x30, ListCount = 0x38;
inline constexpr int StatMinDamage = 21, StatMaxDamage = 22;

// --- core-maps: area map stats (MS4, #219; D2Game\src\Monster\MonsterRegion.cpp) ----------------------------------
// rva::GameMonRegions: 0x5098b0 builds every level's region once per game (0x508750), from its levels row; these are
// the fields PD2's 0x1038b4d0 rewrites, at 1.13c's offsets.
inline constexpr int RegionCount = 0x10, RegionRarity = 0x11, RegionSpawnCount = 0x12;  // uint8: groups, sum, picks
inline constexpr int RegionGroups = 0x14, RegionGroupSize = 0x34, RegionMaxGroups = 13;  // int16 monstats id, uint8 rarity
inline constexpr int RegionMonDen = 0x2b8;                         // uint32 levels MonDen of the difficulty
inline constexpr int RegionBossMin = 0x2bc, RegionBossMax = 0x2bd;  // uint8 MonUMin / MonUMax
// (game, level id) -> the level's monster level: levels MonLvl / MonLvlEx of the difficulty (0x32c700), plus a terror
// zone's bump (MonsterRegionAdjustMonsterLevel). Monster spawns, uniques and item levels call it; D2R never reads the
// region's own monster level fields (+0x2dc, +0x2e0), which PD2 rewrites.
// mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / push rdi / sub rsp,0x30
inline constexpr uint64_t AreaMonsterLevelFn = 0x508f80;
inline constexpr uint8_t AreaMonsterLevelEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                    0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30};

// --- core-maps: the drop chance area stats (MS4b, #286; D2Game\src\Items\Items.cpp) -------------------------------
// (game, drop, 0) -> item or null: makes an item from a drop request (1.13c's, which PD2 calls through 0x1030ef80).
// The TC drop 0x444680 (game, unit, item row, quality, index, flags) fills the request and calls it at 0x444866; PD2's
// drop hook 0x10372180 replaces that function.
// push rbp / push rbx / push rsi / push r12 / push r15 / lea rbp,[rsp-0x37] / sub rsp,0x90
inline constexpr uint64_t ItemSpawn = 0x43cd50;
inline constexpr uint8_t ItemSpawnEntry[] = {0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8d,
                                             0x6c, 0x24, 0xc9, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00};
inline constexpr uint64_t TcDropReturn = 0x44486b;  // the TC drop's return address from ItemSpawn
inline constexpr int DropUnit = 0x00;   // Unit* the dropper
inline constexpr int DropFlags = 0xc0;  // uint32; 0x4 makes an ethereal-capable item ethereal (0x442d60), 1.13c +0x80
inline constexpr uint32_t DropFlagEthereal = 0x4;
inline constexpr uint64_t ItemQuality = 0x36cf60;     // (item) -> quality (6 rare)
inline constexpr uint64_t ItemSocketable = 0x36b410;  // (item) -> non-zero: can take sockets (1.13c D2Common #10360)
inline constexpr uint64_t SetItemSockets = 0x36e880;  // (item, n): sockets, clamped to the type's max (1.13c #10599)
inline constexpr uint64_t ItemHasSockets = 0x36c750;  // (item) -> non-zero when socketed; cube input flags 4 / 8 test it
inline constexpr uint64_t ItemIsEthereal = 0x3740a0;  // (item) -> non-zero when ethereal; cube input flags 0x10 / 0x20
// (item, &property {prop, param, min, max}): a cube output mod on the item's magic list (mod type 7, 1.13c #10914)
inline constexpr uint64_t ApplyCubeMod = 0x3d3420;
inline constexpr int ItType = 0x12e;     // int16 items type (the type the socketable test reads)
inline constexpr int It2Handed = 0x12b;  // uint8 items 2handed (items field table 0x315fd0)
// cubemain record (rva::CubeRecipe), packed: param +4, value +8; input n at +0x14 + n * 10 (uint16 flags, int32 id,
// uint8 quality at +6; 0x529d50 reads it); output A at +0x5e (uint16 flags, int32 id at +2, uint8 quality at +6, type at
// +9: 0xff usetype, 0xfe useitem), its five mods from +0x77, 12 bytes each {int32 prop, int16 param, min, max, chance}
// (0x5269c0). The field table 0x3e3ff0 holds these less 0xe.
inline constexpr int CubeParam = 0x4, CubeValue = 0x8, CubeInputs = 0x14, CubeInputSize = 10;
inline constexpr int CubeOutQuality = 0x64, CubeOutType = 0x67, CubeOutMods = 0x77, CubeModSize = 12;

// --- core-maps: the extra boss area stat (MS4c, #289; D2Game\src\Unit\SUnit.cpp) -----------------------------------
// (game, room, preset): spawns one room preset unit (1.13c D2Game 0x6fd013b0, which PD2's 0x1038bfc0 wraps) through
// 0x48dc50 (1.13c 0x6fd012d0; type 1 goes to 0x50a050). The room's preset loop 0x48f190 (1.13c 0x6fd01430) is its
// only caller. Preset: +4 int32 id (a monstats id for a monster), +0x20 int32 unit type.
// push rbx / push rsi / push rdi / sub rsp,0x80
inline constexpr uint64_t PresetSpawn = 0x4926b0;
inline constexpr uint8_t PresetSpawnEntry[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00};
inline constexpr int PresetId = 0x4, PresetType = 0x20;

// --- core-maps: map events that only spawn (MS8, #222; D2Game\src\Monster\MonsterRegion.cpp) -----------------------
// (game, room): spawns a room's monsters as it activates (1.13c D2Game 0x6fc67a20, which PD2's event spawner
// 0x1036fef0 wraps). Its first call (1.13c 0x6fc66cd0) counts the room in its level's monster region at +0x04, the
// count PD2's event progress reads.
// mov r11,rsp / push rbx / push rsi / sub rsp,0xb8
inline constexpr uint64_t RoomPopulate = 0x50acf0;
inline constexpr uint8_t RoomPopulateEntry[] = {0x4c, 0x8b, 0xdc, 0x53, 0x56, 0x48, 0x81, 0xec, 0xb8, 0x00, 0x00, 0x00};
// A room's subtile rect, as 0x2f0680 (does the room hold x/y) reads it: int32 x, y, width, height
inline constexpr int RoomX = 0x80, RoomY = 0x84, RoomWidth = 0x88, RoomHeight = 0x8c;

// --- core-maps: engine memory pools (LP3, #213) ----------------------------------------------------------------------
// TlsfAlloctor objects (vtable 0x1d6d770): +0x20 the tlsf control, +0x28 its lock (0x14122ad60 locks, 0x14122ad90
// unlocks). 0x1215650 is tlsf block_insert(control, block); tlsf_add_pool is inlined in 0x1215a30.
// Entity System Pool: every HD scene entity (singleton getter 0x913f60); 0xd714e0 gives it 70 MiB (0x4600000). A PD2
// map preset over about 34k entities runs it dry and the entity vector grow (0x6d17d0) memsets a null.
inline constexpr uint64_t EntityPool = 0x2677438;
// Renderer Pool: 0xf6efe0 gives it 12 MiB (0xc00000); allocations go through 0xf6f060. The full-density Jungle Map
// ran it dry: 0xdafe30 (called from 0xeeb4d0) wrote through the null it returned.
inline constexpr uint64_t RendererPool = 0x2810ff0;
// JSON Pool: 0xa0a230 gives it 40 MiB (0x2800000) for parsed JSON; a 46 MB generated preset ran it dry (null write in
// the memcpy at 0x12d36f3). ObjectPool: 0xa20bb0 gives it 50 MiB (0x3200000).
inline constexpr uint64_t JsonPool = 0x27d1770;
inline constexpr uint64_t ObjectPool = 0x27d7848;
// Jobs: the job system (getter 0x9e8e70, static 0x34c3b40) holds a 10 MiB (0xa00000) TlsfAllocator at +0x1a8 (vtable
// 0x1d95618, set up by 0x9e91e0). Same +0x20 control, but +0x28 is a spinlock: 0x9e8c30 swaps in the lock's own
// address, spins while it is not 0 and stores 0 to release. The full-density Jungle Map ran it dry: 0xd9e740 wrote
// through the null 0x9e8ca0 returned.
inline constexpr uint64_t JobsPool = 0x34c3ce8;
// Texture Cache TextureData Pool: 0xe8b100 gives it 24 MiB (0x1800000); a TlsfAlloctor like the Entity System Pool.
inline constexpr uint64_t TexturePool = 0x27ff098;
inline constexpr uint64_t EntityPoolLock = 0x122ad60;    // (lock*) -> void
inline constexpr uint64_t EntityPoolUnlock = 0x122ad90;  // (lock*) -> void
inline constexpr uint64_t TlsfBlockInsert = 0x1215650;   // (control, block) -> void

// Critical and deadly strike (IR3, docs/research/ir3-combat-stats.md)
// WeaponDamage's (0x44c030) roll: from 0x44c2b4, right after the physical roll is stored at damage +0x18, to 0x44c3d9
// it rolls the weapon mastery's crit (when a5 is 0), passive_critical_strike, then deadly strike, and doubles the
// physical part with result flag 0x2000. Attacker in RSI, damage block in RDI, a5 in R14D; 0x44c3d9 needs R15D 0.
// The 28 bytes replaced: test r14d,r14d / jnz / mov rcx,rsi / call UnitWeapon / test / jz / lea r9d,[r14+2] /
// xor r8d,r8d / mov rdx,rax (nothing outside jumps into them).
inline constexpr uint64_t WeaponCritBlock = 0x44c2b4;
inline constexpr uint64_t WeaponCritBlockEnd = 0x44c3d9;
inline constexpr uint8_t WeaponCritBlockBytes[] = {0x45, 0x85, 0xf6, 0x75, 0x67, 0x48, 0x8b, 0xce, 0xe8, 0xef,
                                                   0x7f, 0xfd, 0xff, 0x48, 0x85, 0xc0, 0x74, 0x5a, 0x45, 0x8d,
                                                   0x4e, 0x02, 0x45, 0x33, 0xc0, 0x48, 0x8b, 0xd0};
// (unit, weapon) -> bool: a missile's crit or deadly strike, rolled by the missile fill 0x3bf9b0 (D2Common Missile.cpp)
// and the server's 0x432f90 / 0x4333f0; a hit doubles the physical part of a missile that carries it.
// mov [rsp+8],rbx / mov [rsp+0x10],rsi / push rdi / sub rsp,0x20
inline constexpr uint64_t MissileCritRoll = 0x3ba820;
inline constexpr uint8_t MissileCritRollEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                                                   0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
// (unit, weapon, 0, kind) -> the weapon mastery's bonus; kind 2 is the crit chance (D2RCore jumps it at its entry)
inline constexpr uint64_t MasteryBonus = 0x33d4f0;

// D2R's op-base read (0x2f9b10, as PD2's 0x102fa640 hooks in 1.13c) is dead: D2RCore jumps the stat getters that
// reach it (0x2f5020 is jmp [0x3e2a218]) into its own code, which never calls it (#446).
inline constexpr uint64_t InventoryBodyItem = 0x3886d0;  // (inventory, bodyloc 0-12) -> the item worn there
// (unit, amount) -> life added: life + amount up to the max, unless the unit is dead or has state 92 (1.13c's
// 0x6fcfb570, which PD2 replaces with 0x10306ca0). Callers: the life leech 0x450c90 (attacker) and the hit's absorb
// heal in 0x44ce80 (victim). mov [rsp+0x18],rbx / push rsi / sub rsp,0x20 / mov esi,edx / mov rbx,rcx
inline constexpr uint64_t AddLife = 0x451820;
inline constexpr uint8_t AddLifeEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x48, 0x83,
                                           0xec, 0x20, 0x8b, 0xf2, 0x48, 0x8b, 0xd9};
inline constexpr uint64_t UnitMaxLife = 0x2f4d20;  // (unit) -> max life, 1/256

// --- core-maps: Lucion's AI (UB6, #437; D2Game\src\Ai) ----------------------------------------------------------------
// The AI table (AiTable below, 155 rows): +0x10 an int, +0x18 a second function. ai_hooks.cpp writes its think slots.
// More of the think's tick (AiTickTarget below): +0x24 non-zero in melee range, +0x28 the monstats record, +0x38
// uint8 difficulty (as D2R's Overseer 0x5c2290 reads them)
inline constexpr int AiTickInMelee = 0x24, AiTickMonStats = 0x28, AiTickDifficulty = 0x38;
// monstats record: aidel uint8[3] (the txt field table 0x39db30), aip1-8 int16[8][3], Skill1-8 int16, Sk1-8mode uint8
inline constexpr int MonAiDel = 0x8a, MonAiParams = 0x90, MonSkills = 0x1c4, MonSkillModes = 0x1d4;
// (game, unit, mode, skill, target, x, y, 0) -> non-zero once cast; on failure it idles 10 (0x4a7bc0)
inline constexpr uint64_t AiUseSkill = 0x4a7bc0;
inline constexpr uint64_t AiIdle = 0x4a6d10;      // (game, unit, frames): mode neutral, think again after frames
inline constexpr uint64_t AiSetDelay = 0x4a6e50;  // (game, unit, frames): think again after frames, mode kept
inline constexpr uint64_t AiAttack = 0x4a78e0;    // (game, unit, mode, target): melee in mode 4 (A1) or 5 (A2)
inline constexpr uint64_t AiApproach = 0x4a8740;  // (game, unit, target, 0): walk at the target
// The mode command AiIdle sends: (unit, mode, &cmd), cmd +0x10 the unit, then (game, &cmd, 1)
inline constexpr uint64_t ModeCmdInit = 0x4471e0, ModeCmdRun = 0x4475c0;
inline constexpr int ModeCmdUnit = 0x10;
// CrushingBlowApply's entry is jmp [rip+0x38a8872]: the qword it jumps through, which a plugin can swap
inline constexpr uint64_t CrushingBlowApplySlot = 0x3e2a448;
// The unit event dispatcher 0x5881e0, (game, event, unit, target, damage, list), is jmp [here]: D2RCore's. The hit
// (0x44ce80) raises domeleedamage (5) / domissiledamage (6) on the attacker through 0x44d570 before the damage lands.
inline constexpr uint64_t EventDispatchSlot = 0x3e2a420;
// A unit's item event nodes (docs/research/p12-ormus-robes.md): the head at unit +0xe0; node +0 event, +0xc stat,
// +0x38 next. The node's function (+0x28) is a D2RCore trampoline into D2RCore's own event table.
inline constexpr int UnitEventNodes = 0xe0, EventNodeEvent = 0x0, EventNodeStat = 0xc, EventNodeNext = 0x38;
inline constexpr uint32_t MonPrimeEvil = 0x80;  // MonFlags bit 7 (1.13c's D2Common #10278 tests the same bit)

// --- core-maps: the key drop rule (UB2, #348) ----------------------------------------------------------------------
// Game from a game id: GameById(GameKey(id)); game +0x08 (u16) holds the id back. The server thread walks the same
// table without a lock (0x406400).
inline constexpr uint64_t GameKey = 0x4061e0;   // (u16 id) -> key, 0 for an empty slot
inline constexpr uint64_t GameById = 0x4062f0;  // (key) -> game or null
inline constexpr int GameIdField = 0x08;
inline constexpr uint64_t InTerrorZone = 0x489b70;  // (game, level id) -> bool: the level is in the active Terror Zone
inline constexpr uint64_t PlayerCount = 0x4251e0;   // (game) -> players, at least the /players setting
inline constexpr uint64_t DataTablesOf = 0x300a90;  // (dataFlag) -> DataTables
inline constexpr int TablesTcNames = 0xd70;         // TreasureClassEx name map, for TcIndexByName
inline constexpr uint64_t TcIndexByName = 0xa121f0;  // (name map, name, 0) -> compiled index, > 0 when found
inline constexpr uint64_t TcRecord = 0x397290;       // (dataFlag, u16 index, 0) -> compiled TreasureClassEx or null
inline constexpr int TcSet = 0x2a, TcUnique = 0x2c;  // u16 Set / Unique columns
inline constexpr uint64_t ItemRowByCode = 0x313ef0;  // (dataFlag, u32 code, 'wss ' = 0x20737377) -> items row, < 0 if none
// (game, unit, items row, quality, set/unique row + 1, flags): drops the item by the unit at its item level, as the TC
// roller 0x4404f0 does
inline constexpr uint64_t DropItemAt = 0x444680;
inline constexpr int32_t QualityNormal = 2;
// The game's dropped-uniques mask (1.13c +0x1b24): bit `row & 31` of u32 `row >> 5`, by uniqueitems row (at most
// 0x1000). The unique roll 0x443910 refuses a set bit, a forced row included, and sets it on a drop.
inline constexpr int GameUniquesDropped = 0x3638;
inline constexpr uint32_t MonDemon = 0x2000;  // MonFlags bit 13 (monstats field table 0x39db30)

// --- core-maps: the Uber Tristram primes' drop (UB3, #228; D2Game\src\Quests\QuestsFX.cpp) --------------------------
// D2R's own drop on the primes' deaths (1.13c 0x6fd0aa30): it flags 704/705/709 dead in the game and, once all three
// are, drops the Hellfire Torch and a Standard of Heroes per player. The unique-monster death switch 0x4a1480 tail-calls
// it (0x4a1645). The patch makes it return at once, as PD2 replaces it with its own drop (0x10377710).
inline constexpr uint64_t PrimesDrop = 0x5a8450;
inline constexpr uint8_t PrimesDropD2R[] = {0x48, 0x89, 0x5c, 0x24, 0x20};  // mov [rsp+20],rbx
inline constexpr uint8_t PrimesDropOff[] = {0xc3, 0x90, 0x90, 0x90, 0x90};  // ret
static_assert(sizeof(PrimesDropD2R) == sizeof(PrimesDropOff));
// --- core-maps: Lucion's death (UB6, #231; D2Game\src\Skills\Skills.cpp, D2Game\src\Player\PlayerPets.cpp) --------
// Decompiled for #231 (Ghidra, D2R.gpr). Called, not hooked.
// The portal maker is rva::PortalMaker (MS9's block below; #231 found the same 0x432ce0): with arg 9 0 it finds a free
// spot near (x, y) in room; the portal opens on event 1, and 0x435dd0 makes its linked return portal in the level.
// Arg 10 1 skips the town check (it reads the player only there); the level must be in room's act.
// 0x5da800 (rva::OpenRedPortal) calls it, then sets the portal's level (rva::SetPortalLevel).
// How the pet kill 0x500fb0 kills a monster: (unit, mode, block) fills a 0x30-byte mode block, block +0x10 gets
// (game, unit) -> the unit's target, then (game, block, 1) sets the mode. Mode 0 is death.
inline constexpr uint64_t MonsterModeBlock = 0x4471e0;
inline constexpr uint64_t UnitTarget = 0x490300;
inline constexpr uint64_t SetMonsterMode = 0x4475c0;
inline constexpr int32_t ModeDeath = 0;
// (unit, flags, on): unit flags +0x124 (bit 0x10000 is dead, rva::UnitDead); 0x20000 is 1.13c's no-treasure-class
// bit, which PD2's add kill 0x10356e00 sets
inline constexpr uint64_t SetUnitFlags = 0x34e190;
inline constexpr uint32_t UnitNoDrop = 0x20000;
// D2Common's DUNGEON_FindActSpawnLocationEx (PD2 calls 1.13c's, ordinal 10816, through 0x1038a000), as WarpToLevel
// calls it: (dataFlag, act, level, spot, int32* x, int32* y, unit size) -> the room holding the free spot near the
// level's spot, its coordinates in x/y, or null. With the levels record's +0x90 set, spot picks a special tile of the
// level's presets (0x3dac20; 13 the level's entry); without it, spot is unused. It builds the level if needed. The game's acts: a pointer array at +0x190,
// its count at +0x198.
inline constexpr uint64_t ActSpawnSpot = 0x2ef8b0;
inline constexpr int GameActs = 0x190, GameActCount = 0x198;
// --- core-maps: the Outer Void's in-level portals (MS7c, #532; D2Common Drlg) -------------------------------------
// Decompiled for #532 (Ghidra, D2R.gpr): the steps ActSpawnSpot's 0x3da650 takes from a level id to a room. Called,
// not hooked. (dataFlag, act +0x70, level id) -> the act's level (made if missing); (dataFlag, level) builds its rooms
// when level +0x10 (the first room ex) is null; (level, tile x, tile y) -> the room ex holding the tile; (dataFlag, room
// ex) -> its room, made if needed. A level's origin is in tiles at +0x24/+0x28.
inline constexpr int ActDrlg = 0x70;
inline constexpr uint64_t LevelGet = 0x3267c0;
inline constexpr uint64_t LevelBuild = 0x3271c0;
inline constexpr uint64_t LevelRoomExAt = 0x3275b0;
inline constexpr uint64_t RoomExRoom = 0x3289a0;
inline constexpr int LevelFirstRoomEx = 0x10, LevelTileX = 0x24, LevelTileY = 0x28;
// (game, unit, room, x, y, 0, 0): moves the unit to a free spot by x/y in room, as WarpToLevel does in its own act
inline constexpr uint64_t TeleportUnit = 0x491ca0;
// A room preset's subtile offsets from the room's RoomX/RoomY, as PresetSpawn reads them
inline constexpr int PresetX = 0x8, PresetY = 0x24;

// --- core-maps: the Uber Tristram primes' minions (UB3, #483) -------------------------------------------------------
// D2Game's AI table: one 0x20-byte row per monai row, the think function (game, monster, tick) first. The rows were
// matched by their functions' source files (AiBaal.cpp holds row 135's). The originals PD2 wraps (0x10361150):
// Mephisto (50), Diablo (51), BaalCrab (135).
inline constexpr uint64_t AiTable = 0x2396ea0;
inline constexpr int AiRowSize = 0x20;
inline constexpr uint64_t AiMephisto = 0x5b1ca0, AiDiablo = 0x5c8d40, AiBaalCrab = 0x5a8b80;
inline constexpr uint64_t AiOverseer = 0x5c2290;  // row 120's think (Lucion, #437)
inline constexpr uint64_t AiTrapMelee = 0x5b5ca0;  // row 87's think (Dclone, #470): melee by aip1, else idle aip2
inline constexpr uint64_t AiTrappedSoul = 0x5cfb20;  // row 99's think (the trapped souls, #472), read from the table
// The think's tick block: the target at +0x10 (1.13c +0x08), its distance at +0x20 (1.13c +0x14), as the Mephisto
// think reads them
inline constexpr int AiTickTarget = 0x10, AiTickDistance = 0x20;
inline constexpr int MonAiDist = 0x8d;  // uint8 aidist, then (N) and (H) (monstats field table 0x39db30)

// --- core-maps: the Temple's chests (UB6, #438; D2Game\src\Objects\ObjMode.cpp) ------------------------------------
// Decompiled for #438 (Ghidra, D2R.gpr). One operate table slot wrapped (rva::ObjectOperateTable).
// D2R's chest operate (OperateFn 4): mode 0 only; it sets mode 1 with event 1 at the open frames (or mode 2), plays
// sound 0xb for a locked chest, then drops the act's chest TC. PD2's LucionChest keeps OperateFn 4.
inline constexpr uint64_t OperateChest = 0x58e2f0;
inline constexpr int ObjectOperateChest = 4;
// How the chest operate plays its unlock sound: rva::ClientMessage(player, 0xb, player), then (player) -> its client
// (player data +0xf0, null for a non-player) and (player, client) sends the sound packet 0x2c
inline constexpr uint64_t PlayerClient = 0x48fde0;
inline constexpr uint64_t SoundPacket = 0x5397f0;
inline constexpr int SoundChestOpen = 0xb;

// --- core-rules: the ethereal bonus (IR8, #243) --------------------------------------------------------------------
// (item): sets the ethereal flag, then base stats times 3/2 (1.13c's D2Common #10890, which PD2 replaces with
// 0x102fb710). Callers: the drop 0x442d60, the ethereal property 0x3ccc60, the cube output 0x5269c0.
// mov [rsp+0x10],rbx / push rdi / sub rsp,0x20 / mov rbx,rcx / test rcx,rcx
inline constexpr uint64_t ItemApplyEthereal = 0x3d3510;
inline constexpr uint8_t ItemApplyEtherealEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83,
                                                     0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9};
inline constexpr uint64_t UnitBaseStat = 0x2f48c0;  // (unit, stat, layer) -> the value on the unit's base list
inline constexpr uint64_t SetItemFlag = 0x36d8f0;   // (item, flag, on): item data +0x18 flags (0x400000 ethereal)
// The item decoder (0x378860, D2Common Items.cpp; client packets and save loads) resets a weapon's base damage from
// weapons.txt and, when ethereal, times 3/2 inline: per stat 21, 22, 23, 24, 159, 160 the 16 bytes
// lea eax,[rax+rax*2] / cdq / sub eax,edx / mov edx,<stat> / sar eax,1 / mov r8d,eax, before SetUnitStat.
inline constexpr uint64_t DecodeEtherealDamage[] = {0x37a451, 0x37a47c, 0x37a4a7, 0x37a4d2, 0x37a4fe, 0x37a52a};

// --- core-maps: the Horazon event's portal and drops (MS9, #223; D2Game\src\Items\ItemMode.cpp, Items.cpp) ----------
// Decompiled for #223 (Ghidra, D2R.gpr). Called, not hooked.
// (game, unit, room, x, y, destLevel, &object, objectId, 0, 1) -> non-zero: the portal pair OpenRedPortal makes. With
// the last 1 the unit is not read (only the town refusal message uses it); the destination must be in the room's act.
// The caller sets the destination after (SetPortalLevel), as OpenRedPortal does.
inline constexpr uint64_t PortalMaker = 0x432ce0;
// (game, dropper, killer, TC record, quality, item level, 0, items* or 0, count* or 0, max): rolls the TC and drops
// its picks by the dropper; with no items array it drops all picks (max under 1 means 6). The item level feeds the
// quality roll 0x4421b0. PD2's map events call 1.13c's same function as (..., tc, 0, ilvl, 0, 0, 0, 1).
inline constexpr uint64_t TcDrop = 0x4404f0;

// --- core-maps: Rathma's stage spawns (UB5, #525; D2Common Drlg, D2Game\src\Game\Clients.cpp) -----------------------
// Decompiled for #525 (Ghidra, D2R.gpr). Called, not hooked. How ActSpawnSpot's 0x3da650 finds and builds a room
// anywhere in a level, as PD2's 0x10389f00 does through 1.13c D2Common:
// (dataFlag, drlg act (act +0x70), level) -> the act's drlg level, made if missing; its room list at +0x10 is empty
// until the level is built. ActDrlg and TeleportUnit are the Outer Void's (#532) above.
inline constexpr uint64_t DrlgLevelOf = 0x3267c0;
inline constexpr int DrlgLevelRooms = 0x10;
inline constexpr uint64_t DrlgRoomAt = 0x3275b0;    // (drlg level, tile x, tile y) -> its drlg room holding them, or null
inline constexpr uint64_t DrlgRoomInit = 0x3289a0;  // (dataFlag, drlg room) -> the room, built if needed

// --- core-maps: the map-area death drops (MS4d; D2Game\src\Monster\MonsterMode.cpp) --------------------------------
// Decompiled for MS4d (Ghidra, D2R.gpr). Called, not hooked.
// (game, monster, flag, death*) -> the monster's own TC drop: its monstats TC for the difficulty and kind (champion,
// unique, quest), dropped by 0x441300. death +0x08 is the monster, +0x10 the killer (the quest-drop test and the
// quality roll's player). 1.13c's 0x6fc95680, which PD2 calls through 0x1030f2b0 for an extra drop; D2R's death path
// 0x4479a0 passes flag 0, as PD2 does.
inline constexpr uint64_t MonsterTcDrop = 0x447b80;
inline constexpr int MonDataTypeFlags = 0x1a;     // u8 in the monster's UnitData: 0x10 minion (1.13c +0x16)
inline constexpr uint8_t MonTypeMinion = 0x10;

// --- core-maps: the Rathma clones' shared life (UB5, #475; D2Game\src\Unit\SUnitDmg.cpp) --------------------------
// Decompiled for #475 (Ghidra, D2R.gpr). DamageEffects (0x44df10) runs its 12 damage types (1.13c's table at D2Game
// 0x6fd22ab0, 0x40 bytes each here) through (&ctx, &type, flag), called as jmp [this slot]: D2RCore's resist step,
// the step PD2 replaces with 0x10302ca0 (1.13c's 0x6fcfbd00).
inline constexpr uint64_t DamageTypeStepSlot = 0x3e2aab0;
inline constexpr int StepGame = 0x00, StepVictim = 0x18;  // ctx: Game*, the difficulty record, attacker, victim
// type: +0 the int32* damage field; +0x28 int32 non-zero on the 9 types 1.13c flags (physical, fire, lightning,
// cold, magic, poison and the three leeches; not the cold, freeze and poison lengths), PD2's param_1[8]
inline constexpr int TypeField = 0x00, TypeMirrored = 0x28;
// (attacker, victim, rate, length, a5, a6, a7): poison, stat 74 (hpregen) at -rate for length frames (PD2's
// 0x103055b0). Its entry tests the length (test r9d,r9d / jle, relative); the hook sits past it: mov rax,rsp /
// push rbp / push rsi / push r14 / push r15 / sub rsp,0x68 / mov r14d,r9d, the arguments still in place.
inline constexpr uint64_t PoisonApply = 0x4518c0, PoisonApplyHook = 0x4518c9;
inline constexpr uint8_t PoisonApplyHookEntry[] = {0x48, 0x8b, 0xc4, 0x55, 0x56, 0x41, 0x56, 0x41,
                                                   0x57, 0x48, 0x83, 0xec, 0x68, 0x45, 0x8b, 0xf1};
inline constexpr int UnitGame = 0xd8;  // Game*, what 0x48ff00 returns

}  // namespace d2r::rva

namespace d2r {

// Argument of rva::ApplyCurse, as the curse srvdo (0x5598f0) fills it: aurastat1 travels in the NewStateList
// request, aurastat2-6 are set on the list after it.
struct CurseParams {
	Game* game;           // 0x00
	Unit* caster;         // 0x08
	int32_t pad10[2];     // 0x10 not read here
	int32_t skill;        // 0x18
	int32_t level;        // 0x1c
	int32_t key;          // 0x20
	int32_t frames;       // 0x24
	int32_t stats[6];     // 0x28 aurastat1-6, -1 for none
	int32_t values[6];    // 0x40 their calcs, already worked out
	int32_t state;        // 0x58 aurastate
};
static_assert(offsetof(CurseParams, stats) == 0x28);
static_assert(offsetof(CurseParams, values) == 0x40);
static_assert(offsetof(CurseParams, state) == 0x58);

}  // namespace d2r

namespace d2r {

// Leap Attack's (0x56b170) cleanup of its damage block: the tagged object at +0x158 (bit 0 set: a heap pointer or
// none; clear: an object in place), then the list if it outgrew its inline space (capacity without the inline bit).
inline void ReleaseDamage(LeapKnockback& damage) noexcept {
	const auto release = [](void* p) {
		void* allocator = Engine<void* (*)() noexcept>(rva::EngineAllocator)();
		(*reinterpret_cast<void (***)(void*, void*) noexcept>(allocator))[4](allocator, p);
	};
	const uint64_t tagged = damage.tagged[0];
	void* object = (tagged & 1) != 0 ? reinterpret_cast<void*>(tagged & ~1ull) : &damage.tagged[0];
	if (object != nullptr) {
		(**reinterpret_cast<void (***)(void*, int32_t) noexcept>(object))(object, 0);
		if ((damage.tagged[0] & 1) != 0) {  // read again: the destructor may leave a heap pointer
			release(reinterpret_cast<void*>(damage.tagged[0] & ~1ull));
		}
		damage.tagged[0] = 1;
	}
	if (static_cast<int64_t>(damage.listCapacity) >= 0) {
		release(damage.listData);
	}
}

}  // namespace d2r
