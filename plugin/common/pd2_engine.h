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
#include <cstring>

namespace d2r::rva {

// --- Tables -------------------------------------------------------------------------------------------------------
// D2R's own slot PD2's rules call through (the Poison Dagger do function)
inline constexpr int SrvDoPoisonDaggerSlot = 32;  // 0x55cc50: D2R's Poison Dagger do function, applies the queued hit

// Charge, D2R's own: srvst 31 (0x564dc0) aims the charge path, srvdo 67 (0x5622c0) runs it and hits at the end
inline constexpr int SrvStVanillaCharge = 31, SrvDoVanillaCharge = 67;
// MissileParams::callback, run by the create (0x3bac90) from the table at 0x2381f30 as fn(missile, callbackArg).
// 1 (0x3b75c0, D2R's Firestorm, srvdo 117 / cltdo 67): frames capped at 77, seed from the arg, a type 10 path; it is
// PD2's maker callback 0x1036f220 but for the seed (DiabWall's 7 also skips the path 20% of the time)
inline constexpr int MissileCallbackMaker = 1;
inline constexpr int AuraEventRevive = 32;

inline constexpr uint16_t CollisionMissile = 0x806;  // PD2's fragment tests this mask

// Assassin charge-up finishers (W29, docs/research/w29-assassin-finishers.md)
inline constexpr int SrvDoChargeUpSlot = 34;  // 0x570a70: target, roll, queue the hit, AddCharge if it hits, apply

// (game, unit, target, &damage, srcdam): weapon damage at srcdam / 128, queued on the unit for target
inline constexpr uint64_t QueueMeleeHit = 0x44b600;
inline constexpr uint64_t ApplyQueuedHit = 0x44b2b0;  // (game, unit, target, 0, 1)
// D2R's own Nova do (srvdo) and client do (cltdo) slots PD2's PoisonEggSpawn calls
inline constexpr int SrvDoVanillaNova = 22, CltDoVanillaNova = 25;

// --- Offsets ------------------------------------------------------------------------------------------------------

// Cold length (1.13c's 0x30) and hit class (1.13c's 0x64), as ElementalDamage (0x437d00) and Hunger (0x576630) fill
// them
inline constexpr int DamageColdLength = 0x118;
inline constexpr int DamageHitClass = 0x144;  // uint32; D2R's Static Field ORs 0xd into it (0x4d, as PD2's)

inline constexpr int SkPetMax = 0x114;   // int32 petmax (calc index), which srvdo 119 hands the pet add

}  // namespace d2r::rva

namespace d2r::rva {

inline constexpr int NpcMenuTyrael = 0x15;  // Tyrael's record (monster 367, tyrael2), as NpcMenuOpen picks it
inline constexpr int NpcTableTyrael = 30;
inline constexpr uint16_t MonTyrael2 = 0x16f;

// --- Cube (D2Game\src\Player\PlrTrade.cpp; core-events' op 30) --------------------------------------------------------

inline constexpr uint8_t CubeNumInputsJumpD2R[] = {0x0f, 0x85, 0xbd, 0x06, 0x00, 0x00};
inline constexpr uint8_t CubeNumInputsJumpOff[] = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};  // 6-byte nop

inline constexpr uint64_t UnitXY = 0x34a110;               // (unit) -> uint32: x low 16 bits, y high 16

// --- core-ui: HD pictures of 4-character item codes (#310, #298) ---------------------------------------------------

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

// --- core-events: opening a map (MS2, #217; D2Game\src\Skills\SkillItem.cpp) ----------------------------------------

// The unlimited books' item spells (IR5, #240; the merge's misc pSpell for rtp and rid): the Navigator's entry 17 runs
// D2R's Town Portal; the Almanac's entry 18 is {0, 0, keep 1}, and core-events' item spell hooks identify every inventory
// and Cube item (PD2: "Identifies all items at once"), as D2R's use function 1 (0x57ec30) identifies one. keep is also
// read by the client (0x161f20 allows a use only from inputs passing bit (keep != 0) + 1; right-click does, as for
// the Cube), not by the use-item handler 0x4f40c0.
inline constexpr int32_t UnlimitedPortalSpell = 17, UnlimitedIdentifySpell = 18;
// The item spell USE handler table (.data, 16 pointers of bool fn(game, player, item) noexcept, indexed by an entry's
// use index; the dispatcher 0x581680 calls it for every spell but core-events' 16, so a slot write needs no hook on the
// dispatchers Cartographer owns). Slot 14 is D2R's unused spell 14 (no D2R or PD2 misc row has pSpell 14); the Almanac
// takes it, and core-rules writes its entry 18 as {0, 14, keep 1}.
inline constexpr uint64_t ItemSpellUseTable = 0x1d3ea90;
inline constexpr int32_t AlmanacUseSlot = 14;
inline constexpr uint64_t AlmanacUseSlotOriginal = 0x580f10;  // D2R's stub in that slot (calls 0x5d9710, returns 1)

// --- core-events: tier-0 re-entry portals and the area rules on entry (UB2, #346; D2Game\src\Objects\ObjMode.cpp) ----
inline constexpr int ObjectOperatePortal = 60;
// PD2's map-event altar operate 79 takes slot 35 (MS8, pd2_ids.FUNCS): D2R's null slot of the 1.09 Steeg Stone (337),
// an object no level places
inline constexpr int ObjectOperateAltar = 35;
inline constexpr uint64_t RoomAt = 0x38dfb0;          // (room, x, y) -> the room near `room` holding x/y, or null

// --- core-events: the key drop rule (UB2, #348) ----------------------------------------------------------------------
inline constexpr uint64_t DataTablesOf = 0x300a90;  // (dataFlag) -> DataTables

// --- core-events: the Uber Tristram primes' drop (UB3, #228; D2Game\src\Quests\QuestsFX.cpp) --------------------------
inline constexpr uint8_t PrimesDropD2R[] = {0x48, 0x89, 0x5c, 0x24, 0x20};  // mov [rsp+20],rbx
inline constexpr uint8_t PrimesDropOff[] = {0xc3, 0x90, 0x90, 0x90, 0x90};  // ret
static_assert(sizeof(PrimesDropD2R) == sizeof(PrimesDropOff));
// --- core-events: Lucion's death (UB6, #231; D2Game\src\Skills\Skills.cpp, D2Game\src\Player\PlayerPets.cpp) --------
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

// --- core-events: the Temple's chests (UB6, #438; D2Game\src\Objects\ObjMode.cpp) ------------------------------------
inline constexpr int ObjectOperateChest = 4;

// --- core-rules: the ethereal bonus (IR8, #243) --------------------------------------------------------------------

inline constexpr uint8_t WeaponDurabilityPd2[] = {2, 5};

// --- core-events: Rathma's stage spawns (UB5, #525; D2Common Drlg, D2Game\src\Game\Clients.cpp) -----------------------
// Decompiled for #525 (Ghidra, D2R.gpr). Called, not hooked. How ActSpawnSpot's 0x3da650 finds and builds a room
// anywhere in a level, as PD2's 0x10389f00 does through 1.13c D2Common:
// (dataFlag, drlg act (act +0x70), level) -> the act's drlg level, made if missing; its room list at +0x10 is empty
// until the level is built. ActDrlg and TeleportUnit are the Outer Void's (#532) above.
inline constexpr uint64_t DrlgLevelOf = 0x3267c0;
inline constexpr int DrlgLevelRooms = 0x10;
inline constexpr int RoomDrlg = 0x18;        // a room's drlg room
inline constexpr int DrlgRoomLevel = 0x90;   // a drlg room's drlg level
inline constexpr int DrlgRoomNext = 0x48;    // a drlg room's next room in its level's list
inline constexpr uint64_t DrlgRoomAt = 0x3275b0;    // (drlg level, tile x, tile y) -> its drlg room holding them, or null
inline constexpr uint64_t DrlgRoomInit = 0x3289a0;  // (dataFlag, drlg room) -> the room, built if needed

}  // namespace d2r::rva

namespace d2r {

// Leap Attack's (0x56b170) cleanup of its damage block: the tagged object at +0x158 (bit 0 set: a heap pointer or
// none; clear: an object in place), then the list if it outgrew its inline space (capacity without the inline bit).
inline void ReleaseDamageRva(LeapKnockback& damage) noexcept {
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

// --- core-rules: socketed gems and runes apply their mods once (#450; D2Game\src\Items\ItemMode.cpp) -----------------
// 0x475140 (dataFlag, filler, parent item, 0) applies a socketed filler's gems.txt mods; every insert path calls it
// (0x46eb00 and its siblings 0x46e670, 0x46ea70, 0x46e8c0, 0x46ef30, 0x470ca0, 0x4744a0, 0x475090). It takes the mods
// into the filler's own stat list when the filler is a gem (item type row 0x14) or a rune (0x4b), unless that list was
// saved and sent too: the save/packet writer 0x37ffd0 writes the compact form (no stats) when item data +0x18 has bit
// 0x200000 (`flags >> 0x15 & 1`), and the client and the loader then apply the mods once, from gems.txt.
// Our misc.txt has compactsave=0 for every gem (PD2's stacks), so the flag is set here instead.
inline constexpr uint64_t SocketFillerMods = 0x475140;
inline constexpr uint8_t SocketFillerModsEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x57, 0x41,
                                                    0x56, 0x48, 0x83, 0xec, 0x30};
inline constexpr uint64_t UnitItemData = 0x34a500;  // (item) -> item data, null for a non-item
inline constexpr int ItemDataFlags = 0x18;          // uint32 item flags
inline constexpr uint32_t ItemFlagCompactSave = 0x200000;
inline constexpr int ItemTypeGem = 0x14, ItemTypeRune = 0x4b;
