// Engine addresses for D2R 3.3.0 build 93847 under D2RLoader 1.3.1 (the frozen install copy, ADR 0004).
//
// One header per D2R build: a new build gets its own rvas_<build>.h and d2r.h includes that one. Every value here was
// found in the Ghidra analysis of the dumped 93847 image and used in game by the Split Throw spike
// (spikes/split-throw/FINDINGS.md). Names match the RVA column of docs/core-rules.md.
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

// --- Offsets ------------------------------------------------------------------------------------------------------
inline constexpr int GameDataFlag = 0x106;  // Game: byte passed to every data-table getter

inline constexpr int UnitType = 0x00;  // int32: 0 player, 1 monster, 2 object, 3 missile, 4 item, 5 tile
inline constexpr int UnitPath = 0x38;  // dynamic path (x +0x2, y +0x6, uint16) or static path (x +0x10, y +0x14)

// skills.txt record (748 bytes in D2R)
inline constexpr int SkSrvMissileA = 0x68;  // int16 srvmissilea; srvmissileb +0x6a, srvmissilec +0x6c
inline constexpr int SkCltMissileA = 0x140; // int16 cltmissilea; cltmissileb +0x142, cltmissilec +0x144
inline constexpr int SkCalc1 = 0x190;       // int32 calc index (pass to EvalCalc); calc2 +0x194, calc3 +0x198
inline constexpr int SkCalc2 = 0x194;
inline constexpr int SkCalc3 = 0x198;

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

}  // namespace d2r
