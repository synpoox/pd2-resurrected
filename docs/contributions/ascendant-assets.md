# RuffnecKk's Dark and Light Ascendant assets

This contribution offers my Ascendant presentation work for PD2 Resurrected:
a hooded Dark caster with a fitted staff and custom animations, a Light caster
using the Summoner presentation, and a matching HUD portrait for each.

The assets are arranged under `files/data/` so they can be installed with the
mod. They are **not selected by the current game data**: both Ascendants still
use the existing Council Member model until the maintainer connects these
appearances. This contribution deliberately leaves the existing hiring, skill,
balance, AI and save behavior intact.

## Contents and attribution

| Asset | Location below `files/data/` | Notes |
| --- | --- | --- |
| Dark actor | `hd/character/enemy/ruff_a4_dark.json` | Corrected private body and staff references |
| Dark body | `hd/character/enemy/ruff_a4_dark/equipment/body_lod0.model` through `body_lod3.model` | Adapted Dark Wanderer body with the final restored idle robe |
| Dark staff | `hd/character/enemy/ruff_a4_dark/equipment/staff.json` and `staff_lod0.model` through `staff_lod4.model` | Fitted Summoner staff |
| Dark rig and animations | `hd/character/enemy/ruff_a4_dark/{skeleton,animation}/` | One-handed staff setup, ready/walk/cast and corrected death sequence |
| Dark state machine | `hd/character/enemy/ruff_a4_dark/state_machine.json` | Uses the private animation bank and cast effect |
| Dark cast effect | `hd/vfx/particles/character/enemy/ruff_a4_dark/FX_Dark_Ascendant_Cast.particles` | Uses the contributed gradient texture |
| Light actor and state machine | `hd/character/enemy/ruff_a4_light.json` and `ruff_a4_light/state_machine.json` | Reuses D2R's Summoner model, rig, textures and animation bank |
| Dark HUD portrait | `hd/global/ui/hireables/ruff_a4_portrait{,.lowend}.sprite` | Brighter art with the stock Rogue gold frame; 120 x 120 and 60 x 60 |
| Light HUD portrait | `hd/global/ui/hireables/ruff_a4_light{,.lowend}.sprite` | Brighter art with the same stock Rogue gold frame; 120 x 120 and 60 x 60 |

Contribution, fitting, animation work and portrait presentation: **RuffnecKk**.
The underlying Dark Wanderer and Summoner game assets are Blizzard's D2R assets;
these are adaptations, not independently created replacement models. Original
game asset rights remain with their owners. Authored contribution code is
offered under this repository's MIT license. No new ownership of Blizzard's
assets is asserted.

[`art/ascendants/manifest.json`](../../art/ascendants/manifest.json) records the
source revision, source archive hash, and size/SHA-256 of all 23 runtime assets
and three descriptor inputs. Dark's animation bank and four body LODs come as
one set from the final approved
[My Reimagined revision `537a302`](https://github.com/BRODIABLO/My-Reimagined/commit/537a302c4ca1102927aa77f5cbc08f03bcaa903f).
They restore the original idle robe, retarget the stock Summoner cast, and
disable both foot IK controls only during Dark's cast. The actual Git LFS
binaries were resolved and verified against that commit's object IDs and sizes.
The final animation SHA-256 is
`704e84d2741062f976c048d530ff72f3c66b1d4e6263b2f4ca816976de1cda9b`.
Supporting Dark files retain the corrected packaged appearance. JSON line endings are
normalized to LF without changing parsed content; the manifest also records
the original source hashes. Binary assets are copied exactly. The authoring Blender scene,
Reimagined DLL, private skills, localization tables and complete Reimagined
descriptor catalogs are not part of this contribution.

All four portrait sprites are the actual Git LFS binaries from
[My Reimagined revision `a186ea0`](https://github.com/BRODIABLO/My-Reimagined/commit/a186ea0299c6f44c8a74ae4a97dec4b1b6118324).
Both hirelings use brighter art and the same stock Rogue gold frame. The
standard sprites are 120 x 120 and the low-end sprites are 60 x 60; hashes,
dimensions and source object IDs are recorded in the contribution manifest.
The five approved Dark cast/robe files remain pinned to `537a302`.

## Merge the two model registrations and one texture descriptor

New model paths need catalog entries. `art/ascendants/model-registrations.json`
contains only the two private body/staff records, with their LODs and mesh
metadata. The helper rebuilds relative offsets and preserves every existing
model, LOD and mesh record in the recipient's effective catalog. It rejects a
conflicting entry or hash collision. Applying the same registration twice is
idempotent. The texture helper also rejects conflicting or duplicate names.

Read the **effective recipient files**, including the base game when the mod
has no override. The following writes only to fresh output paths:

```powershell
python tools/ascendant_assets.py
python tools/ascendant_assets.py `
  --catalog <effective-hd>/model_lod_desc.bin `
  --output out/ascendant-descriptors/model_lod_desc.bin `
  --texture-cache <effective-hd>/texture_desc_cache.json `
  --texture-output out/ascendant-descriptors/texture_desc_cache.json
```

Review and place those outputs under `files/data/hd/` when activating the
appearances. If the recipient maintains an authoring `model_lod_desc.json`,
also pass `--lod-json <input> --lod-output <fresh-output>` to merge the two
matching metadata entries. The JSON is not a substitute for the binary catalog.

The gradient descriptor matches its 256 x 16, single-level RGBA texture. Its
thumbnail is inherited from the stock flame-gradient descriptor; it is an
authoring preview, not a preview of the edited Dark gradient.

All other referenced models, textures and Light animation files are inherited
from the recipient's D2R base. Preserve exact path spelling. The individual
`.animation` paths in the state machines resolve into `combined.animations`
banks; individual clip files should not be manufactured.

## Connect the appearances

At contribution base `e8b391a`, all Dark/Light hireling rows use the same
`act4hire` monster class (1073) and the map has `act4hire: ascendant`. One static
map entry cannot select two different models. The maintainer can use an
existing variant-selection mechanism or give Light a separate monster class.
For the latter approach:

1. Keep `act4hire` and its ordinal for Dark. Map its exact ID to `ruff_a4_dark`.
2. Append a copy of its `monstats` row with a distinct `Id` and `BaseId`, such as
   `act4hireLight`. Reuse `MonStatsEx=act4hire` for the unchanged legacy visuals.
   Resolve the new class by compiled row ordinal; do not assume a free ID or
   insert a row ahead of existing classes.
3. Point only Light's hireling IDs **40, 42 and 44** at that new class. Keep
   Dark IDs **39, 41 and 43**. Include every progression row for each ID.
4. Add `act4hireLight: ruff_a4_light` to `hd/character/monsters.json`. Retain the
   existing `$2` legacy art and skill timings. Update `merge/monsters-hd.tsv` if
   retaining rebuild metadata: `act4hire =ruff_a4_dark`, and the new Light ID
   `=ruff_a4_light`.
5. Audit native assumptions about the Ascendant class, including mastery and
   Holy Light consumers, before enabling the second class. Test an existing
   Dark and Light save as well as newly recruited mercenaries. A changed class
   layout is not qualified by merely loading the asset JSON.

## Connect the portraits

Dark uses icon name `ruff_a4_portrait`; Light uses `ruff_a4_light`. Keep the
normal and `.lowend` sprite together for each icon.

The existing `hireable` pet-type row has four class-specific icon slots. Adding
sprite files does not make the HUD distinguish the two Ascendants. Reuse a
recipient portrait service if available. Otherwise the source integration's
approach was to append two private `pettype` rows (`drawhp=1`, `baseicon` equal
to the icon name), then select the appropriate display row from the actual
hireling's class in the current table bank. Resolve rows by icon name, not a
copied row number. Leave other hirelings and summons on their original route.

The source project's native hook is not included: it uses Reimagined-specific
class layouts and native contracts. Porting its addresses without verification
would not make a merge safe. Use the recipient SDK/service conventions and
signature-based hook rules if native work is needed.

## Validation and acceptance

Run the portable contribution checks with no toolkit dependency:

```powershell
python tools/ascendant_assets.py
python -m unittest discover -s tools/tests -p test_ascendant_assets.py -v
```

These check file integrity and descriptor-merge behavior. They do not prove
rendering, animation timing or HUD selection in PD2 Resurrected.

Vincent approved the final Dark appearance in game in **My Reimagined**, as
reported by the originating character task on 2026-11-22. Earlier source
evidence also records Dark death, revive and casting checks in Reimagined.
This approval applies to the five-file final revision identified above; it is
not a PD2 Resurrected game check. The My Reimagined deployment manifest is
mod-specific and is not included in this contribution.
Before activating and accepting the integration, check both variants in Hell
on a level 90+ character: ready/walk/run, all casts, hit/death/resurrection,
body/staff visibility at distance, HUD portraits at both texture settings,
Save and Exit/reload, and an existing hireling save. Check the other mercenary
portraits and the legacy view too. Multiplayer remains a separate check.

The repository's `unit` and `table-diff` merge gates still apply. At this base,
fork PRs are explicitly refused by both workflows because they require the
owner's private toolkit and runner. The maintainer will need to import the
contribution branch into this repository to run those gates. This contribution
does not weaken or bypass them.
