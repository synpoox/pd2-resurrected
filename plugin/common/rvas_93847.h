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
// srvstfunc 66: (game, unit, skill, level) -> 1, registers the skill's auraevent1-4 / auraeventfunc1-4 on unit for
// good (no stat list, no source), through 0x588670 and 0x438230 per event
inline constexpr uint64_t RegisterSkillEvents = 0x42fa90;
// 0x554c40: Teleport to the skill's point; 0 when there is no room, the level forbids it or the point collides (0x804)
inline constexpr int SrvDoTeleport = 27;
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

// --- Functions (signature in the comment) -------------------------------------------------------------------------
inline constexpr uint64_t GetSkillRecord = 0x97790;    // (dataFlag, skillId) -> skills.txt record or null
inline constexpr uint64_t EvalSkillCalc = 0x3b5160;    // (dataFlag, unit, calc, skillId, level) -> value
inline constexpr uint64_t SkillTargetXY = 0x432b70;    // (game, unit, &x, &y) -> non-zero when the skill has a point
inline constexpr uint64_t CreateMissile = 0x5371a0;    // (game, &MissileParams) -> missile unit or null
inline constexpr uint64_t MissileCount = 0x1b89a0;     // (dataFlag) -> rows in missiles.txt
inline constexpr uint64_t SetUnitSkillFlag = 0x34e790; // (unit, on): unit flags (+0x124) bit 0x40, set by do functions
// (unit, skillId) -> srvmissile a/b/c row chosen by the equipped weapon (Double Throw srvdo 154 uses it), -1 on error
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
inline constexpr uint64_t MissileCltFrames = 0x3bb0f0;  // (missile) -> range frames left (client)
inline constexpr uint64_t PathSetTargetUnit = 0x342920;  // (path, unit): path +0x70 target unit
inline constexpr uint64_t PathSteer = 0x340e70;  // (path, unit, 0, 1): re-aim the path at its target unit
inline constexpr uint64_t UnitDistance = 0x324fc0;  // (a, b) -> distance in subtiles
// (dataFlag, missile, owner, calc, -1, -1) -> value; dmg 1 0x453490 evaluates DmgCalc1 this way
inline constexpr uint64_t EvalMissileCalc = 0x3b4a90;
// Power Strike's client do function (cltdo 22, 1.13c D2Client 0x15660) draws the strike only for the local player:
// `je` at this address skips its early return. PD2 has no such check (W6).
inline constexpr uint64_t CltDoStrikeLocalOnly = 0x2aed4a;

// Double Throw (D2R's own srvdo 74 0x567a20, cltdo 42 0x22f3f0, Chain Lightning's srvhit 12 0x45d5e0)
inline constexpr uint64_t UnitWeapon = 0x4242b0;  // (unit) -> weapon item the server attacks with, or null
inline constexpr uint64_t ClientUnitWeapon = 0x34b000;  // (unit) -> the client's weapon item, or null
inline constexpr uint64_t ItemMissileType = 0x371b60;  // (item) -> weapons.txt missiletype row
// (game, missile, unit, skill, level, UnitStateKey(unit), 0, 0, 0, 0, 1) -> missile thrown at the skill's target
inline constexpr uint64_t ThrowSkillMissile = 0x4333f0;
inline constexpr uint64_t ClientThrowSkillMissile = 0x213f50;  // (unit, missile, skill, level, UnitStateKey(unit))
inline constexpr uint64_t MissileBounces = 0x3bc0a0;  // (missile) -> chain count left (Chain Lightning's)
inline constexpr uint64_t SetMissileBounces = 0x3bd8a0;  // (missile, count)
inline constexpr uint64_t MissileSkill = 0x3bc450;  // (missile) -> skill id
inline constexpr uint64_t MissileLevel = 0x3bb470;  // (missile) -> skill level
inline constexpr uint64_t MissileRecord = 0x166040;  // (dataFlag, missile row) -> missiles.txt record or null
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
inline constexpr uint64_t RemoveSkillState = 0x4370b0;  // (unit, state, 1): drop an older list of this state
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
inline constexpr uint64_t MissileTargetType = 0x3bc190;  // (missile) -> target unit type, 6 for none
inline constexpr uint64_t MissileTargetId = 0x3bc280;  // (missile) -> target unit id
inline constexpr uint64_t EngineAllocator = 0x12063c0;  // () -> the allocator; vtable +0x20 frees (Leap Attack's cleanup)
// Desecrate (W13), the way D2R's summon helper 0x4350e0 (Bone Wall, Raise Skeleton) spawns and 0x509520 counts
inline constexpr uint64_t RoomAtXY = 0x38dfb0;  // (room, x, y) -> the room holding x/y near room, or null
inline constexpr uint64_t RoomLevelId = 0x2efc10;  // (room) -> levels.txt id
// (game, room, x, y, monstats id, mode, -1, 0x42) -> monster or null; counts it in its level's monster region
inline constexpr uint64_t SpawnMonster = 0x492cf0;

// --- Offsets ------------------------------------------------------------------------------------------------------
inline constexpr int GameDataFlag = 0x106;  // Game: byte passed to every data-table getter
inline constexpr int GameFrame = 0x170;  // Game: int32 frame counter (unit events are timed in it)

inline constexpr int UnitType = 0x00;  // int32: 0 player, 1 monster, 2 object, 3 missile, 4 item, 5 tile
inline constexpr int UnitMonster = 1;
inline constexpr int UnitMode = 0x0c;  // int32 current mode (monmode for a monster)
inline constexpr int MonModeDeath = 0, MonModeDead = 12;
inline constexpr int UnitPath = 0x38;  // dynamic path (x +0x2, y +0x6, uint16) or static path (x +0x10, y +0x14)
inline constexpr int PathTargetUnit = 0x70;  // Unit* on a dynamic path: what PathSteer aims at (set by PathSetTargetUnit)
inline constexpr int UnitOwnerType = 0xe8;  // int32 owner unit type (MissileOwner 0x490300 reads both)
inline constexpr int UnitOwnerId = 0xec;    // uint32 owner unit id
inline constexpr int TablesStateCount = 0x298;  // uint64 states.txt row count in DataTables
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
inline constexpr int UnitClassId = 0x04;  // int32 row in the unit's table: monstats for a monster, missiles.txt for a missile
inline constexpr int UnitIdField = 0x08;  // uint32, the id NearestTarget skips

// Unit skill (rva::GetUnitSkill): flags and the target the hit looks up (0x56ad90)
inline constexpr int USkillFlags = 0x14;  // 0x80 leap starts, 0x100 in the air, 0x1000 path velocity locked
inline constexpr int USkillTargetType = 0x2c;  // 6 = none
inline constexpr int USkillTargetId = 0x30;
inline constexpr int MonVelocity = 0x6a;  // int16 monstats Velocity

inline constexpr int MsHitPar1 = 0x70;  // int32 missiles.txt sHitPar1 (Chain Lightning's chain range, 0 = the skill's)
inline constexpr int MsHitPar2 = 0x74;  // int32 sHitPar2, sHitPar3 +0x78 (1.13c's 0x50 / 0x54 + 0x24)
inline constexpr int MsHitPar3 = 0x78;
inline constexpr int MsCltHitPar1 = 0x90;  // int32 cHitPar1-3 (1.13c's 0x6c-0x74 + 0x24)
inline constexpr int MsCltHitPar2 = 0x94;
inline constexpr int MsCltHitPar3 = 0x98;
// Damage block a pSrvDmgFunc edits: 1.13c's 0x08-0x2c fields sit 0x10 later (phys 0x08 -> 0x18 in srvdmg 1 and 13,
// poison length 0x2c -> 0x3c in srvdmg 11).
inline constexpr int DmgPhys = 0x18, DmgFire = 0x20, DmgLight = 0x2c, DmgMagic = 0x30, DmgCold = 0x34, DmgPoison = 0x38;
inline constexpr int DmgPoisonLength = 0x3c;
inline constexpr int StatDamagePercent = 25;  // itemstatcost damagepercent, D2R and PD2
inline constexpr int StatPoisonMin = 57;  // poisonmindam, poisonmaxdam 58, poisonlength 59: D2R and PD2
inline constexpr int StatPoisonMax = 58;
inline constexpr int StatPoisonLength = 59;
inline constexpr int StatLife = 6;  // itemstatcost hitpoints (current life, in 1/256), D2R and PD2
inline constexpr int StatMaxLife = 7;  // maxhp
inline constexpr uint32_t NearestTargetFlags = 0x8a783;  // Chain Lightning's search: 0x88583 | the 0xa783 the search adds

// Leeches in the same damage block (built by 0x465d60)
inline constexpr int DamageLifeLeech = 0x120;  // mana leech +0x124, stamina leech +0x128
inline constexpr int DamageManaLeech = 0x124;
inline constexpr int DamageStaminaLeech = 0x128;

// missiles.txt record (1.13c's shifted by 0x24)
inline constexpr int MsDParam1 = 0x9c;    // int32 dParam1
inline constexpr int MsDmgCalc1 = 0xb4;   // int32 DmgCalc1 (calc index), read by dmg 1 0x453490
inline constexpr int MsCollideKill = 0x1ae;  // uint8 CollideKill (1.13c's 0x186 + 0x28; the field table at 0x3c4233)
inline constexpr int MsSkill = 0x1b6;     // int16 Skill (skills.txt row), read off a live explodingarrow row
// int16 HitSubMissile1 and CltHitSubMissile1 (1.13c's 0x24 / 0x2c), as D2R's clthit 26 (0x1adcb0) reads them
inline constexpr int MsHitSubMissile1 = 0x46;
inline constexpr int MsCltHitSubMissile1 = 0x4e;

// MissileParams::flags
inline constexpr uint32_t MissileFromOrigin = 0x820;      // start at the origin unit, fly to targetX/targetY
inline constexpr uint32_t MissileNotTriggering = 0x20000;  // Multiple Shot's outer missiles, the ones calc3 leaves out
inline constexpr uint32_t MissileAtXY = 0x1;  // start at x/y
inline constexpr uint32_t MissileAimOffset = 0x2;  // targetX/targetY are offsets from the start (Nova's srvhit 5)

// Stats Frozen Armor's srvdo 18 sets on its state list (itemstatcost ids)
inline constexpr int StatListSkill = 0x15e;  // 350: the skill that made the list
inline constexpr int StatListLevel = 0x15f;  // 351: its level
inline constexpr int StatListKey = 0xca;     // 202: rva::StateListKey's value

// skills.txt record (748 bytes in D2R)
inline constexpr int SkSrvMissileA = 0x68;  // int16 srvmissilea; srvmissileb +0x6a, srvmissilec +0x6c
inline constexpr int SkCltMissileA = 0x140; // int16 cltmissilea; cltmissileb +0x142, cltmissilec +0x144
inline constexpr int SkCalc1 = 0x190;       // int32 calc index (pass to EvalCalc); calc2 +0x194, calc3 +0x198
inline constexpr int SkCalc2 = 0x194;
inline constexpr int SkCalc3 = 0x198;
inline constexpr int SkCalc4 = 0x19c;  // skills field table at 0x306193
inline constexpr int SkAuraRangeCalc = 0x84;  // int32 aurarangecalc, Chain Lightning's chain range when sHitPar1 is 0
inline constexpr int SkParam3 = 0x1c0;  // int32 Param3
inline constexpr int SkParam4 = 0x1c4;  // int32 Param4 (Param1 0x1b8), field table at 0x305e68
inline constexpr int SkSrcDam = 0x24d;  // uint8 SrcDam (0 = 128), read by Leap Attack's 0x56b170
inline constexpr int SkSrvOverlay = 0x6e;  // int16 srvoverlay (skills field table 0x302380)
inline constexpr int SkAuraFilter = 0x70;  // int32 aurafilter, the area unit mask (clthit 26 reads it)
inline constexpr int SkCltCalc2 = 0x170;  // int32 cltcalc2 (calc index)
inline constexpr int SkCltCalc3 = 0x174;
inline constexpr int SkSumMode = 0x113;  // uint8 summode (Raise Skeleton's srvdo 0x55d1c0 reads pettype at 0x112)
inline constexpr int SkAuraState = 0xa0;      // int16 aurastate
inline constexpr int SkAuraLenCalc = 0x80;    // int32 auralencalc: 1.13c's 0x60, before aurarangecalc as there
inline constexpr int SkAuraStat1 = 0x74;      // int16 aurastat1-6, read by AddAuraStats (0x574480)
inline constexpr int SkAuraStatCalc1 = 0x88;  // int32 aurastatcalc1-6 (calc index, pass to EvalCalc), same reader
// Two record fields Frozen Armor passes to SendSkillState by address; what they hold is not read yet.
inline constexpr int SkSendStateA = 0xa4;
inline constexpr int SkSendStateB = 0xac;

}  // namespace d2r::rva

namespace d2r {

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
	uint8_t pad58[0x68 - 0x58];
	int32_t sourceKey;        // 0x68 UnitStateKey of the missile a chain starts from (Chain Lightning's srvhit 12)
	uint8_t rest[0xa0 - 0x6c];
};
static_assert(sizeof(MissileParams) == 0xa0);
static_assert(offsetof(MissileParams, activation) == 0x54);
static_assert(offsetof(MissileParams, missile) == 0x20);
static_assert(offsetof(MissileParams, targetX) == 0x2c);
static_assert(offsetof(MissileParams, skill) == 0x3c);
static_assert(offsetof(MissileParams, sourceKey) == 0x68);

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
