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

// Self buff, the way Frozen Armor's srvdo 18 (0x553e60) builds its aurastate list
inline constexpr uint64_t RemoveSkillState = 0x4370b0;  // (unit, state, 1): drop an older list of this state
inline constexpr uint64_t NewStateList = 0x433d20;  // (&StateListRequest) -> stat list or null
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
// (game, unit, 0, 0, radius, &LeapKnockback, 0, &scratch): Leap's landing knockback (srvdo 77), callback 0x43a740
inline constexpr uint64_t AreaKnockback = 0x437820;
inline constexpr uint64_t SkillMeleeHit = 0x56b170;  // (game, unit, skillId, level): Leap Attack's hit on the skill target
inline constexpr uint64_t CltDoLeap = 0x22f9a0;  // D2R cltdo 43 (unit, skillId): Leap's client half
inline constexpr uint64_t RemoveUnitEvent = 0x48b890;  // (game, unit, event, 0)
inline constexpr uint64_t AddUnitEvent = 0x48b720;  // (game, unit, event, frame, 0, 0, 0)
inline constexpr uint64_t MonStatsRecord = 0x976e0;  // (dataFlag, classId) -> monstats record or null
inline constexpr uint64_t UnitId = 0x34a330;  // (unit, file, line) -> unit id

// --- Offsets ------------------------------------------------------------------------------------------------------
inline constexpr int GameDataFlag = 0x106;  // Game: byte passed to every data-table getter
inline constexpr int GameFrame = 0x170;  // Game: int32 frame counter (unit events are timed in it)

inline constexpr int UnitType = 0x00;  // int32: 0 player, 1 monster, 2 object, 3 missile, 4 item, 5 tile
inline constexpr int UnitPath = 0x38;  // dynamic path (x +0x2, y +0x6, uint16) or static path (x +0x10, y +0x14)
inline constexpr int PathTargetUnit = 0x70;  // Unit* on a dynamic path: what PathSteer aims at (set by PathSetTargetUnit)
inline constexpr int UnitOwnerType = 0xe8;  // int32 owner unit type (MissileOwner 0x490300 reads both)
inline constexpr int UnitOwnerId = 0xec;    // uint32 owner unit id
inline constexpr int TablesStateCount = 0x298;  // uint64 states.txt row count in DataTables

// Damage block QueuedHit returns: +0 hit flags (uint32), +4 result flags (uint16)
inline constexpr int DamageResult = 0x4;
inline constexpr uint16_t ResultHit = 0x1;
inline constexpr uint16_t ResultKnockback = 0x8;
inline constexpr int UnitClassId = 0x04;  // int32: monstats row of a monster

// Unit skill (rva::GetUnitSkill): flags and the target the hit looks up (0x56ad90)
inline constexpr int USkillFlags = 0x14;  // 0x80 leap starts, 0x100 in the air, 0x1000 path velocity locked
inline constexpr int USkillTargetType = 0x2c;  // 6 = none
inline constexpr int USkillTargetId = 0x30;
inline constexpr int MonVelocity = 0x6a;  // int16 monstats Velocity

// MissileParams::flags
inline constexpr uint32_t MissileFromOrigin = 0x820;      // start at the origin unit, fly to targetX/targetY
inline constexpr uint32_t MissileNotTriggering = 0x20000;  // Multiple Shot's outer missiles, the ones calc3 leaves out

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
inline constexpr int SkAuraState = 0xa0;      // int16 aurastate
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
	uint8_t rest[0xa0 - 0x58];
};
static_assert(sizeof(MissileParams) == 0xa0);
static_assert(offsetof(MissileParams, activation) == 0x54);
static_assert(offsetof(MissileParams, missile) == 0x20);
static_assert(offsetof(MissileParams, targetX) == 0x2c);
static_assert(offsetof(MissileParams, skill) == 0x3c);

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
