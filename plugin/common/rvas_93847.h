// Engine addresses for D2R 3.3.0 build 93847 under D2RLoader 1.3.1 (the frozen install copy, ADR 0004).
//
// One header per D2R build: a new build gets its own rvas_<build>.h and d2r.h includes that one. Every value here was
// found in the Ghidra analysis of the dumped 93847 image; the comment beside each says where (the Split Throw spike,
// spikes/split-throw/FINDINGS.md, or the D2R function it was read from) and the game check that uses it. Names match
// the RVA column of docs/core-rules.md.
#pragma once

#include <cstddef>
#include <cstdint>

namespace d2r::rva {

// PE TimeDateStamp of the image these values belong to (Battle.net 93847 and Steam 94995 share it under D2RLoader
// 1.3.1). On any other image the plugins install nothing (ADR 0004).
inline constexpr uint32_t ImageStamp = 0x6ab3782c;

// --- Tables -------------------------------------------------------------------------------------------------------
// Server skill "do" functions, indexed by skills.txt srvdofunc: 191 slots of 8-byte pointers. D2R fills slots up to
// 171; 172-190 are null and free. The dispatcher rejects values over 190 (core-rules-inventory Q1).
inline constexpr uint64_t SrvDoTable = 0x238ea00;
inline constexpr int SrvDoSlots = 191;
inline constexpr int SrvDoFirstFree = 172;
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

// --- Functions (signature in the comment) -------------------------------------------------------------------------
inline constexpr uint64_t GetSkillRecord = 0x97790;    // (dataFlag, skillId) -> skills.txt record or null
inline constexpr uint64_t EvalSkillCalc = 0x3b5160;    // (dataFlag, unit, calc, skillId, level) -> value
inline constexpr uint64_t SkillTargetXY = 0x432b70;    // (game, unit, &x, &y) -> non-zero when the skill has a point
inline constexpr uint64_t CreateMissile = 0x5371a0;    // (game, &MissileParams) -> missile unit or null
inline constexpr uint64_t MissileCount = 0x1b89a0;     // (dataFlag) -> rows in missiles.txt
inline constexpr uint64_t SetUnitSkillFlag = 0x34e790; // (unit, on): unit flags (+0x124) bit 0x40, set by do functions
// (unit, skillId) -> srvmissile a/b/c row by the skill's charges (1, 2, 3+), srvmissilea for a skill with no charges,
// -1 on error. Vanilla's charge-missile helper (1.13c D2Game 0x91340, which PD2's srvprg 139/171/172 call); D2R's
// srvdo 37 uses it.
inline constexpr uint64_t PickSkillMissile = 0x571700;
inline constexpr uint64_t UnitDataFlag = 0x34a0e0;          // (unit) -> data flag byte; the client's DataFlag
inline constexpr uint64_t ClientSkillTargetXY = 0x214c80;   // (unit, &x, &y) -> non-zero when the skill has a point
inline constexpr uint64_t ClientCreateMissile = 0x1b7760;   // (&MissileParams, 0) -> missile unit or null

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
inline constexpr uint64_t MissileRecord = 0x166040;  // (dataFlag, missile row) -> missiles.txt record or null
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
inline constexpr uint64_t SkillTargetUnit = 0x48fe20;  // (game, unit) -> the skill's target unit, null for none or self
inline constexpr uint64_t ClientSkillTargetUnit = 0x9a570;  // (unit) -> target unit (client)
// (attacker, defender) -> the damage block of the attacker's queued hit on defender (unit +0x108 list), or null
inline constexpr uint64_t QueuedHit = 0x44d660;
inline constexpr uint64_t UnitSeed = 0x34a1e0;  // (unit) -> &seed {lo, hi}
inline constexpr uint64_t UnitStat = 0x2f5020;  // (unit, stat, param) -> value
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
inline constexpr uint64_t RemoveUnitEvent = 0x48b890;  // (game, unit, event, 0)
inline constexpr uint64_t AddUnitEvent = 0x48b720;  // (game, unit, event, frame, 0, 0, 0)
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
inline constexpr int GameDataFlag = 0x106;  // Game: byte passed to every data-table getter
inline constexpr int GameFrame = 0x170;  // Game: int32 frame counter (unit events are timed in it)

inline constexpr int UnitType = 0x00;  // int32: 0 player, 1 monster, 2 object, 3 missile, 4 item, 5 tile
inline constexpr int UnitMonster = 1;
inline constexpr int UnitMode = 0x0c;  // int32 current mode (monmode for a monster)
inline constexpr int MonModeDeath = 0, MonModeDead = 12;
inline constexpr int UnitPath = 0x38;  // dynamic path (x +0x2, y +0x6, uint16) or static path (x +0x10, y +0x14)
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
inline constexpr int SkSrvMissileA = 0x68;  // int16 srvmissilea; srvmissileb +0x6a, srvmissilec +0x6c
inline constexpr int SkCltMissileA = 0x140; // int16 cltmissilea; cltmissileb +0x142, cltmissilec +0x144
inline constexpr int SkCalc1 = 0x190;       // int32 calc index (pass to EvalCalc); calc2 +0x194, calc3 +0x198
inline constexpr int SkCalc2 = 0x194;
inline constexpr int SkCalc3 = 0x198;
inline constexpr int SkCalc4 = 0x19c;  // skills field table at 0x306193
inline constexpr int SkAuraRangeCalc = 0x84;  // int32 aurarangecalc, Chain Lightning's chain range when sHitPar1 is 0
inline constexpr int SkParam1 = 0x1b8;  // int32 Param1-8, field table at 0x305e68
inline constexpr int SkParam2 = 0x1bc;
inline constexpr int SkParam3 = 0x1c0;
inline constexpr int SkParam4 = 0x1c4;
inline constexpr int SkParam5 = 0x1c8;
inline constexpr int SkParam6 = 0x1cc;
inline constexpr int SkRollFlags = 0x29;  // uint8 Leap Attack's hit masks with MeleeRollFlagMask for MeleeRoll's flag
inline constexpr int SkSrvPrgFunc1 = 0x50;  // int16 srvprgfunc1-3, read by SrvPrgDispatch
inline constexpr int SkPrgCalc1 = 0x58;     // int32 prgcalc1-3 (calc index), read by srvdo 37's 0x571590
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
inline constexpr int SkSumMode = 0x113;  // uint8 summode (Raise Skeleton's srvdo 0x55d1c0 reads pettype at 0x112)
inline constexpr int SkPetType = 0x112;  // int8 pettype, read by Spirit Wolf's srvdo 119 (0x575990)
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

struct Game;
struct Unit;

// Missile creation parameters for rva::CreateMissile. 1.10's 0x5c-byte block, shifted by 0x10 after the three pointers.
struct MissileParams {
	uint32_t flags;           // 0x00 (seen: 0x820/0x420 caster -> point, 0x21 start at x/y, 1 placed at x/y)
	uint32_t pad04;
	Unit* owner;              // 0x08
	Unit* origin;             // 0x10 (start at this unit's position when x/y are 0)
	Unit* target;             // 0x18
	int32_t missile;          // 0x20 missiles.txt row
	int32_t x, y;             // 0x24 start (0: the origin's position)
	int32_t targetX, targetY; // 0x2c aim point
	int32_t gfxArg;           // 0x34
	int32_t velocity;         // 0x38
	int32_t skill;            // 0x3c
	int32_t skillLevel;       // 0x40
	uint8_t pad44[0x54 - 0x44];
	int32_t activation;       // 0x54 frames before the missile starts (skills.txt calc2 in Multiple Shot)
	int32_t pad58;
	int32_t range;            // 0x5c frames the missile lives (Inferno's calc1)
	uint8_t pad60[0x68 - 0x60];
	int32_t sourceKey;        // 0x68 UnitStateKey of the missile a chain starts from (Chain Lightning's srvhit 12)
	int32_t pad6c;
	int32_t callback;         // 0x70 index into MissileCreateCallbacks, run on the new missile with callbackArg (0: none)
	int32_t callbackArg;      // 0x74 (Firestorm's 1 and DiabWall's 7 take the maker's index and seed it from that)
	uint8_t rest[0xa0 - 0x78];
};
static_assert(sizeof(MissileParams) == 0xa0);
static_assert(offsetof(MissileParams, activation) == 0x54);
static_assert(offsetof(MissileParams, missile) == 0x20);
static_assert(offsetof(MissileParams, targetX) == 0x2c);
static_assert(offsetof(MissileParams, skill) == 0x3c);
static_assert(offsetof(MissileParams, sourceKey) == 0x68);
static_assert(offsetof(MissileParams, range) == 0x5c);
static_assert(offsetof(MissileParams, callback) == 0x70);

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

}  // namespace d2r

// --- Item rules: Ormus' Robes' property and skill on cast (docs/research/p12-ormus-robes.md) ---------------------
namespace d2r::rva {

// properties.txt func1-7: int32_t fn(dataFlag, a1, a2, item, Property*, set, stat, val, first func's result, a8, a9,
// a10), from the property apply 0x3d4620; 37 slots (bound dword 0x2386bd8 = 37), 26-35 null
inline constexpr uint64_t PropertyFuncTable = 0x2386ab0;
inline constexpr int PropertyFuncSlots = 37;
// (dataFlag, a1, a2, item, Property*, set, stat, layer, value, a8, a9, a10): put one stat on the item's list, as
// every D2R property function does (value shifted by the stat's ValShift)
inline constexpr uint64_t PropertySetStat = 0x3d5940;
inline constexpr uint64_t ItemRandom = 0x3d5860;  // (item, min, max) -> min..max from the item's seed
inline constexpr uint64_t UnitLayerStat = 0x2f5c60;  // (unit, stat, layer) -> value: what D2R's item events read
// A unit's event listeners, seen in game (a 0x70-byte node per listener, most recent first): +0x00 event, +0x04 kind
// (1 skill, 2 item stat), item stat nodes +0x08 layer and +0x0c stat, +0x28 the function D2RCore calls, +0x38 next
inline constexpr int UnitEventList = 0xe0;
inline constexpr int EventNodeEvent = 0x00, EventNodeKind = 0x04, EventNodeLayer = 0x08, EventNodeStat = 0x0c;
inline constexpr int EventNodeNext = 0x38;
inline constexpr int EventNodeKindItem = 2;
// (unit, skill, level, x, y, flag) -> non-zero cast: a skill cast from unit at a point as an event; refuses while the
// unit has state 54 (PD2's event 33 does the same through 1.13c's pair)
inline constexpr uint64_t CastEventSkillAt = 0x589820;
// Server skill dispatcher (D2Game\src\Skills\Skills.cpp): (game, unit, skillId, level, a5, a6, a7) -> non-zero ran;
// calls srvdofunc. mov [rsp+0x10],rbx / mov [rsp+0x20],r9d / mov [rsp+0x18],r8d / push rbp
inline constexpr uint64_t SrvSkillDispatch = 0x43acb0;
inline constexpr uint8_t SrvSkillDispatchEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x44, 0x89, 0x4c,
                                                    0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55};
inline constexpr int UnitFlags = 0x128;  // uint32, set by SetUnitFlag
inline constexpr uint64_t SetUnitFlag = 0x34e140;  // (unit, flag, on)
inline constexpr uint32_t UnitFlagEventCast = 0x10000000;  // D2R's item events hold it while they cast
inline constexpr int PlayerModeCast = 10;  // SC
inline constexpr int SkItemTarget = 0x178;  // uint8 ItemTarget (skills field table 0x302380)
inline constexpr int SkReqLevel = 0x214;    // uint16 reqlevel
inline constexpr int SkLocalDelay = 0x238;  // int32 compiled localdelay formula, -1 for none
// itemstatcost in DataTables: records (0x144 bytes) and count; itemevent1 int16 at +0x44 (the registration reads it)
inline constexpr int TablesItemStatCost = 0x1258, TablesItemStatCostCount = 0x1260;
inline constexpr int IscRecordSize = 0x144, IscItemEvent1 = 0x44;
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
inline constexpr uint32_t ListFlagTimed = 0x2, ListFlagCurse = 0x20;

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
