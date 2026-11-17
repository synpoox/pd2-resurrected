// PD2 Resurrected's own engine values for D2R 3.3.0 build 93847, on top of d2rAI-toolkit's shared/plugin-common (its d2r.h and
// rvas_93847.h hold every shared address, offset and structure: use those, never a copy here). What stays here is a value
// the toolkit does not hold; each entry says why in a line.
// Include this instead of d2r.h. A value another project needs moves to the toolkit's rvas_93847.h, not a copy there.
//
// Every value was found in the Ghidra analysis of the dumped 93847 image; the comment beside each says where (the
// Split Throw spike's FINDINGS.md on branch research/split-throw, or the D2R function it was read from) and the game
// check that uses it. core-rules' names match the RVA column of docs/features/core-rules.md; another plugin's values sit in a
// section headed by that plugin and the work item that found them (core-ui: LP2, #212).
#pragma once

#include "d2r.h"  // the toolkit's: rva::ImageStamp, Game, Unit, MissileParams, api, Engine, At

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2r::rva {

// --- Kept: values the toolkit has no entry for ----------------------------------------------------------------------

// Missile callback 4 (0x3b7c70) adds callbackArg to the missile's stat 25; the toolkit lists only callback 1.
inline constexpr int MissileCallbackDamagePercent = 4;
// PD2's fragment tests this missile collision mask; the toolkit holds no mask values.
inline constexpr uint16_t CollisionMissile = 0x806;

inline constexpr int32_t ItemTypeMap = 245;   // itemtypes row cmap
inline constexpr int32_t StatMapForceEvent = 553, StatMapLevel = 564;
inline constexpr int32_t ItemStatCostRows = 637;  // itemstatcost.txt rows in files/data (ids 0..636)

// Tyrael's menu record (monster 367, tyrael2), as NpcMenuOpen picks it; the toolkit has no per-NPC records.
inline constexpr int NpcMenuTyrael = 0x15;
inline constexpr int NpcTableTyrael = 30;
inline constexpr uint16_t MonTyrael2 = 0x16f;

// The cube number-of-inputs jump's nop (the toolkit holds D2R's bytes, CubeNumInputsJumpOrig, not our patch).
inline constexpr uint8_t CubeNumInputsJumpOff[] = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};  // 6-byte nop

// --- core-ui: HD pictures of 4-character item codes (#310, #298): our patch bytes over the toolkit's ItemsJsonKeyD2R ---
// jmp 0x663a07 / mov [rsp+25],al / movzx eax,word [rsi+2] / test al,al / jne 0x663a10 / mov word [rsp+26],0x2020 /
// jmp 0x663a19 / or ah,0x20 / mov [rsp+26],ax / nop
inline constexpr uint8_t ItemsJsonKeyFull[] = {0xeb, 0x0c, 0x88, 0x44, 0x24, 0x25, 0x0f, 0xb7, 0x46, 0x02, 0x84,
                                               0xc0, 0x75, 0x09, 0x66, 0xc7, 0x44, 0x24, 0x26, 0x20, 0x20, 0xeb,
                                               0x09, 0x80, 0xcc, 0x20, 0x66, 0x89, 0x44, 0x24, 0x26, 0x90};
static_assert(sizeof(ItemsJsonKeyD2R) == sizeof(ItemsJsonKeyFull));

// mov eax,[r8] / mov ecx,0x20202020 / test ah,ah / je one / test eax,0xff0000 / je two / or eax,0x20000000 / jmp store /
// one: mov cl,al / mov eax,ecx / jmp store / two: mov cx,ax / mov eax,ecx / store: mov [rsp+50],eax / 12 nops
inline constexpr uint8_t SpawnCheatKeyFull[] = {0x41, 0x8b, 0x00, 0xb9, 0x20, 0x20, 0x20, 0x20, 0x84, 0xe4, 0x74, 0x0e,
                                               0xa9, 0x00, 0x00, 0xff, 0x00, 0x74, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x20,
                                               0xeb, 0x0b, 0x88, 0xc1, 0x89, 0xc8, 0xeb, 0x05, 0x66, 0x89, 0xc1, 0x89,
                                               0xc8, 0x89, 0x44, 0x24, 0x50, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                                               0x90, 0x90, 0x90, 0x90, 0x90};
static_assert(sizeof(SpawnCheatKeyD2R) == sizeof(SpawnCheatKeyFull));

// --- core-events: the unlimited books' item spells (IR5, #240): entry numbers of our own misc rows -----------------
// The Navigator's entry 17 runs D2R's Town Portal; the Almanac's entry 18 is {0, 0, keep 1}, and core-events' item spell
// hooks identify every inventory and Cube item (PD2: "Identifies all items at once"). keep is also read by the client
// (0x161f20 allows a use only from inputs passing bit (keep != 0) + 1), not by the use-item handler 0x4f40c0.
inline constexpr int32_t UnlimitedPortalSpell = 17, UnlimitedIdentifySpell = 18;
// The item spell USE handler table (.data, 16 pointers of bool fn(game, player, item) noexcept, indexed by an entry's
// use index; the dispatcher 0x581680 calls it for every spell but core-events' 16). Slot 14 is D2R's unused spell 14;
// the Almanac takes it, and core-rules writes its entry 18 as {0, 14, keep 1}. The toolkit holds only the 20-entry
// spell table (ItemSpellTable), not this handler table.
inline constexpr uint64_t ItemSpellUseTable = 0x1d3ea90;
inline constexpr int32_t AlmanacUseSlot = 14;
inline constexpr uint64_t AlmanacUseSlotOriginal = 0x580f10;  // D2R's stub in that slot (calls 0x5d9710, returns 1)

// --- core-events: the Uber Tristram primes' drop (UB3, #228): our patch over the toolkit's PrimesDropOrig ---------------
inline constexpr uint8_t PrimesDropOff[] = {0xc3, 0x90, 0x90, 0x90, 0x90};  // ret
static_assert(sizeof(PrimesDropOrig) == sizeof(PrimesDropOff));

// --- core-rules: the ethereal bonus (IR8, #243): PD2's weapon durability values ----------------------------------------
inline constexpr uint8_t WeaponDurabilityPd2[] = {2, 5};

// --- core-rules: socketed gems and runes apply their mods once (#450; D2Game\src\Items\ItemMode.cpp) -------------------
// 0x475140 (dataFlag, filler, parent item, 0) applies a socketed filler's gems.txt mods; every insert path calls it
// (0x46eb00 and its siblings 0x46e670, 0x46ea70, 0x46e8c0, 0x46ef30, 0x470ca0, 0x4744a0, 0x475090). It takes the mods
// into the filler's own stat list when the filler is a gem (item type row 0x14) or a rune (0x4b), unless that list was
// saved and sent too: the save/packet writer 0x37ffd0 writes the compact form (no stats) when item data +0x18 has bit
// 0x200000 (`flags >> 0x15 & 1`), and the client and the loader then apply the mods once, from gems.txt.
// Our misc.txt has compactsave=0 for every gem (PD2's stacks), so the flag is set here instead. Not in the toolkit yet.
inline constexpr uint64_t SocketFillerMods = 0x475140;
inline constexpr uint8_t SocketFillerModsEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x57, 0x41,
                                                    0x56, 0x48, 0x83, 0xec, 0x30};
inline constexpr uint64_t UnitItemData = 0x34a500;  // (item) -> item data, null for a non-item
inline constexpr int ItemDataFlags = 0x18;          // uint32 item flags
inline constexpr uint32_t ItemFlagCompactSave = 0x200000;
inline constexpr int ItemTypeGem = 0x14, ItemTypeRune = 0x4b;

// Drlg room offsets (D2Common Drlg, read for Rathma's stage spawns, UB5, #525); the toolkit has the level and room-at
// functions (LevelGet, LevelRoomExAt, RoomExRoom) but not these two fields.
inline constexpr int RoomDrlg = 0x18;        // a room's drlg room
inline constexpr int DrlgRoomLevel = 0x90;   // a drlg room's drlg level
inline constexpr int DrlgRoomNext = 0x48;    // a drlg room's next room in its level's list

}  // namespace d2r::rva
