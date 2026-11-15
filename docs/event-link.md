# Event link: PD2 events beside a standalone Cartographer (#944)

Part of the Cartographer migration ([#942](https://github.com/synpoox/pd2-resurrected/issues/942), [plan](cartographer-migration.md)).
Code: `plugin/common/event_link.h`. Check: `plugin/tests/event_link_check.cpp`.

## Rule

Cartographer is **not modified** and stays PD2-neutral. The PD2 side (core-events, today core-maps' lane C/D code) adapts to
what Cartographer already exposes plus D2RLoader's PluginSDK services. Inspected: d2r-tools 629f08b. Cartographer
publishes no service or event (`PluginCommunicationService`, `ItemInteractionService` and `LifecycleService` are unused
there), so there is nothing to consume yet; the consumer runs observe-only.

## Contract (version 1)

| Piece | Rule |
|---|---|
| Capabilities | `Activation`, `AreaIdentity`, `RoomProgress`, `MonsterSetup`, `Teardown` (bit flags). |
| Discovery | A provider would publish `{version, caps}` as a service through `PluginCommunicationService` (provider-local name). The consumer acquires it at load, keeps the handle until unload, and treats a missing service as `NoProvider`. |
| Versions | A newer provider table must keep serving older callers; new fields go at the end. `Check` refuses a provider older than `ContractVersion`. |
| Refusal | `Activate(provider, need, consume)` runs `consume` (take the item, charge) only on `Ok`. `NoProvider`, `OldVersion` and `MissingCaps` touch nothing. |
| Area identity | `AreaId = {level id, session generation}` from the SDK's `LevelChanged` event (UI thread). A level id reused by the next map in the same session is a different area only if the provider says so (`AreaIdentity`); without it the consumer keys on level id plus session. |
| Progress | `AreaTracker` keeps one entry per area. Entering, killing in or closing a second area never edits the source's entry (source/destination independence for Horazon and Bitter Depths). Monster deaths come from the SDK's `MonsterDeathListener` (server thread, observation-only). |
| Threads and lifetime | Lifecycle callbacks arrive on the UI thread, death callbacks on the server thread: the real plugin guards the tracker with a mutex. Event pointers are valid only inside the callback. Listeners and service handles are released at unload. |
| Metadata | Event metadata (the Catalyst's forced-event marker) must sit on a stat **with a layer** (`MarkerLayer`). Cartographer turns every layer-0 item stat into a player buff and drops a layered stat (`itemStatsToProperties`), so the marker never becomes a buff and no Cartographer change is needed. A layer-0 marker would become a native player stat (the check proves it). |

## Not decided here

- Which ItemStatCost row carries the marker. The ticket asks for an unused existing row, not a new one. The row needs a Save Param Bits setting so it can hold a layer, and the choice and the game proof that a layered stat survives the Catalyst recipe and a save belong to the Catalyst work item (migration ticket 10), which owns the recipe.
- `RoomProgress` and `MonsterSetup` (spawn-time) hooks. The SDK offers death and level-change events only. Room activation and monster setup stay on core-maps' native hooks until a provider publishes them.

## Check

```
cmake -S plugin -B plugin/build-checks -A x64 -DCARTOGRAPHER_SRC=<d2r-tools>/plugins/cartographer/src
cmake --build plugin/build-checks --config Release --target event_link_check
plugin/build-checks/Release/event_link_check.exe
```

Without `CARTOGRAPHER_SRC` the Cartographer checks print `SKIPPED`.
