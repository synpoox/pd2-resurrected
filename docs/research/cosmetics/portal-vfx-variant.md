# Town portal VFX variant: how D2R picks blue or Chronicle gold

Research for #1714 (map #1713). Build 93847, dump `C:/ghidra-work/D2RLoader_6ab3782c_dump.exe` (file offset == RVA),
Ghidra project `C:\ghidra-work\project\D2R.gpr`. Every RVA below was read in that dump. Static analysis only: no game
run. Anything not read in code is marked **[unverified]**.

## Answer in one paragraph

The index is **stat 364 `customization_index` on the portal owner's state 220 `chronicleportal`**. The server reads it
when it sends the portal ownership packet `0x82` and puts it in byte `+0x4f`. The client writes that byte to the
portal object unit at `+0x1bb`. The HD unit code reads `+0x1bb` only for object class 59 (TownPortal) and uses it as
the index into the streams built from `town_portal.json`'s `VfxVariantComponent.vfx` list. The packet carries 8 bits
(0-255). The only cap at 1 is in the server handler of client packet `0x65`, which rejects `extra > 1`. A server-side
plugin that sets the state and the stat itself skips that cap. The variant list in the JSON is a growable vector
(16 entries inline), so more variants can be added.

## The chain

| Step | Where | What it does |
|---|---|---|
| 1. Data | stock `data/global/excel/states.txt` row 220 | `chronicleportal`: `plrstaydeath=1`, `stat=customization_index`. Rows 218 `chroniclebacklight` (overlay `chronicle_backlight`) and 219 `chroniclefootprints` have no stat. Same in our `files/data/global/excel/states.txt` |
| | stock `itemstatcost.txt` row 364 | `customization_index`: `Send Bits=32`, no `Save Bits` |
| 2. Toggle | `FUN_141484670` (0x1484670), Chronicle widget | Sends client packet `0x65` via `FUN_1400ec780` (#1714). The issue's "flag" `0xdc` is state id 220, and `FUN_1403351b0` (0x3351b0) is the state test (bit array at unit data `+0xaf0`, the state array D2RCore also reads; `knowledge/engine-re.md` section 5) |
| 3. Server handler | `FUN_1404b67b0` (0x4b67b0), in `D2Game\src\Player\PlrMsg.cpp` (string at 0x1d33ac0, line 0x1f02) | Packet is 13 bytes (asserts size `0xd`): `+0` cmd, `+1` u32 state, `+5` u32 on/off, `+9` u32 `extra`. It accepts only state 0xda (reward type 1), 0xdb (type 0) or 0xdc (type 2). **For 0xdc it rejects `extra > 1`** (`0x4b6844: cmp [rsp+0x41],eax; jbe`, with eax = 1). Then `FUN_1403d8870(player, type)` must return at least `1.0f` (`DAT_141cba904`; the fraction of that Chronicle category completed). On "on", it creates a stat list (`FUN_1402f7300`), sets its state (`FUN_1402f72f0`), attaches it (`FUN_1402f2cb0`), sets **stat 0x16c = `extra`** (`FUN_1402f7c00(..., list, 0x16c, extra)`) and turns the state on (`FUN_1403354c0`). On "off", it removes the list (`FUN_1402f7920`, `FUN_1402f4180`) |
| 4. Server sender | `FUN_14047a450` (0x47a450), in `D2Game\src\Game\SCmd.cpp` (line 0xcc1) | Builds packet `0x82`, 0x50 bytes, sent with `FUN_1404817f0(client, buf, 0x50)`: `+1` owner unit id, `+5` owner name (up to 0x3c chars), `+0x42` portal id, `+0x46` remote portal id, `+0x4a` u8, `+0x4b` u16, `+0x4d` u16 (callers' args, meaning not traced), **`+0x4f` = 0 unless the owner has state 0xdc; then it is stat 0x16c of that state's stat list** (`FUN_1402f5940(owner, 0xdc)` finds the list, `FUN_1402f5df0(..., list, 0x16c, 0)` reads the stat), truncated to a byte (`0x47a5e3: mov [rsp+0x7f], al`) |
| 5. Client handler | `FUN_14012de40` (0x12de40) | Reads the same layout: owner name at `+5` (`FUN_1401cb070`), copies `+0x4b`/`+0x4d`/`+0x4a` to the portal unit `+0x1b6`/`+0x1b8`/`+0x1ba`, and **`+0x4f` to `+0x1bb` on both the local portal (`+0x42`) and the remote one (`+0x46`)**; then `FUN_140136940` (RosterLinkTownPortal). The opcode `0x82` is the server's (step 4). No table entry for this handler was found in the dump; the link from opcode to handler rests on the matching 0x50-byte layout **[unverified at runtime]** |
| 6. Render pick | `FUN_140778e00` (0x778e00), HD unit setup | Looks up the unit (`FUN_14009a5d0(id, 2)`), and **only if unit type 2 and class `0x3b` (object 59, TownPortal)** reads `idx = byte [unit+0x1bb]` (`0x77903f`). Then it takes the entity's `VfxVariantStreamComponent` (`FUN_1406e5a10`), asserts `idx < size` (`FUN_14074a400`), asserts the stream's state byte `+200 == 4` (loaded) (`FUN_14074d9c0`), and creates the unit's effect from `streams[idx]` (`FUN_14073e130`, then `FUN_140eb2ba0`; the effect object itself was not traced) |
| 7. Streams | `FUN_140d53bc0` (0xd53bc0) -> `FUN_140d30460` (0xd30460) | For every entity with a `VfxVariantComponent`, emplaces a `VfxVariantStreamComponent` (0x98 bytes) and fills it from the variant list (`FUN_140d4a250`) |
| 8. Component | `FUN_140d3f500` (0xd3f500), `FUN_140d33380` (0xd33380), JSON reader `FUN_140d36f40` (0xd36f40) | `VfxVariantComponent` is 0xb98 bytes: a small vector of 0xb8-byte `VfxDefinitionComponent` entries, inline capacity 16 (`0x8000000000000010`), grown on the heap past that. No size cap was seen. entt storage: `FUN_14096a2c0` (0x96a2c0); type index `FUN_14096f460`; type hash `0xc7b32398` (FNV-1a of `struct VfxVariantComponent`). The hash `0x11058700` from #1714 is `struct VfxDefinitionComponent`, not the variant |

Stock `data/hd/objects/vfx_only/town_portal.json` (CASC, `C:/Games/Diablo II Resurrected`): entity `entity_root`
has `VfxDefinitionComponent` `vfx_town_portal_NewStuff.particles` and `VfxVariantComponent.vfx` = `[0]`
`vfx_town_portal_NewStuff.particles` (blue), `[1]` `vfx_town_portal_chronicle.particles` (gold). Both `.particles`
are listed in `dependencies.particles`.

## Answers to #1714

- **Where the index is picked:** `FUN_140778e00` at 0x77903f reads object unit `+0x1bb` for class 59 only.
- **What feeds it:** server packet `0x82` byte `+0x4f` <- owner's stat 364 under state 220 (`FUN_14047a450`) <- client
  packet `0x65` `extra` (`FUN_1404b67b0`). The Chronicle widget sends `extra` = 1 when it turns the portal on.
- **Meaning of `extra`:** the variant index (stat 364). State 220 on + stat 0 would mean variant 0 (blue).
- **Can it go above 1:** in the data and render path, yes. The packet byte allows 0-255, the variant list grows. Only
  the `0x65` handler caps it (`extra <= 1`). An index at or past the list size hits the asserts in step 6; in a release
  build they likely fall through to an out-of-range read **[unverified: assume a crash; keep every index inside the list]**.
- **Who sees it:** the index rides in packet `0x82`. Whether the server sends it to every client in the game or only
  the owner's is **[unverified]**: `FUN_14047a450` has no direct callers in the dump.
- **Other portals:** the class `0x3b` test means only object 59 uses the variant. Object 60 (permanent portal) and
  PD2's `xactportal_*` objects do not.

## Cheapest plugin route

1. **Data:** add entries to `entity_root`'s `VfxVariantComponent.vfx` in our `town_portal.json` (index 2, 3, ...), each a
   `VfxDefinitionComponent` with its own `.particles`, and list each file in `dependencies.particles`. Keep `[0]` blue
   and `[1]` gold.
2. **Server, no hook:** on the host, give the owner state 220 with stat 364 = N. That is exactly what the `0x65` handler
   does (step 3), so the sender (step 4) and every client after it need no change. The SDK has no state or stat
   service (`third_party/PluginSDK/include/D2RLPlugin/`: no state API in any header), so the plugin calls D2R's own
   functions: `FUN_1402f7300` / `FUN_1402f72f0` / `FUN_1402f2cb0` / `FUN_1402f7c00` / `FUN_1403354c0`, with the
   argument counts read at the handler's call sites (`knowledge/engine-re.md` 4.7). Not yet tried **[unverified]**.
3. **If a hook is wanted instead:**
   - Server sender, the stat read in `FUN_14047a450` (sets `+0x4f` for every receiver);
   - or the `0x65` gate, to let the client packet carry `extra > 1` (the stock widget only sends 0/1 and needs 100%
     Chronicle, so this alone does not help);
   - or client-only, the `0x82` handler (only that player's view changes).

Stat 364 has no `Save Bits`, so its value is not saved; whether state 220 itself survives a save is **[unverified]**. Where the Chronicle
choice is restored at join is **[unverified]**: `FUN_1400942a0` (0x942a0) re-sends `0x65` for 0xda/0xdb on a timer,
which looks like a test sequence, not a join sync. A plugin should apply the state at every game join.

## Signatures (each matches once in the dump)

`??` covers rip-relative and call/jump displacements.

| Site | RVA | Bytes |
|---|---|---|
| Client `0x82` handler entry | 0x12de40 | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 44 8B 79 01 48 8B D9 8B 69 42` |
| Client render read (class 59 test + `movzx r15d, byte [rax+0x1bb]`) | 0x77902c | `83 38 02 0F 85 ?? ?? ?? ?? 83 78 04 3B 0F 85 ?? ?? ?? ?? 44 0F B6 B8 BB 01 00 00` |
| Server `0x82` sender entry | 0x47a450 | `40 55 41 54 41 56 41 57 48 81 EC A8 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4` |
| Server sender stat read (state 0xdc, stat 0x16c) | 0x47a5b4 | `BA DC 00 00 00 C6 44 24 7F 00 48 8B CE E8 ?? ?? ?? ?? 48 85 C0 74 ?? 0F B6 8D 06 01 00 00 45 33 C9 41 B8 6C 01 00 00` |
| Server `0x65` handler entry | 0x4b67b0 | `40 53 56 57 41 55 48 83 EC 58 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 48 49 63 D9 49 8B F0 48 8B FA 4C 8B E9 83 FB 0D` |
| Server `0x65` state switch and `extra <= 1` gate | 0x4b6831 | `2D DA 00 00 00 74 ?? 83 E8 01 74 ?? 83 F8 01 75 ?? B2 02 39 44 24 41 76 ??` |

None of these entries starts with `FF 25` (no D2RLoader hook there). `FUN_1402f5df0` (the stat read) is a `jmp` through
`_DAT_143e2a208` (likely set by D2RCore **[unverified]**), so do not hook it.

## Still open

- In-game proof: `chronicleFill 100`, toggle the portal reward, cast a portal, see gold; then set stat 364 to 2 with a
  third variant in `town_portal.json`.
- Whether packet `0x82` goes to all clients (other players see the owner's colour).
- What happens on an out-of-range index in release (assert fall-through).
- When `FUN_140778e00` runs compared with packet `0x82`: if the HD unit is built before the packet arrives, the
  portal may show variant 0 until it is rebuilt.
- Footprints (219) and backlight (218) carry no stat, so they have no variant index; the "Warlock Weapon VFX" panel
  was not traced.
