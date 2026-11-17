# Event link: PD2 events beside a standalone Cartographer (#944)

Part of the Cartographer migration ([#942](https://github.com/synpoox/pd2-resurrected/issues/942), [plan](../cartographer/migration.md)).
Code: `plugin/common/event_link.h`. Check: `plugin/tests/event_link_check.cpp`. Horazon is the first open-extra consumer (`cartographer.open-extra.v1`, #1028).

## Rule

Cartographer is **not modified** and stays PD2-neutral. The PD2 side (core-events, today core-events' lane C/D code) adapts to
what Cartographer already exposes plus D2RLoader's PluginSDK services. Inspected: d2r-tools 629f08b. Cartographer
publishes no service or event (`PluginCommunicationService`, `ItemInteractionService` and `LifecycleService` are unused
there), so there is nothing to consume yet; the consumer runs observe-only.

## Contract (version 1)

| Piece | Rule |
|---|---|
| Capabilities | `Activation`, `AreaIdentity`, `RoomProgress`, `MonsterSetup`, `Teardown` (bit flags). |
| Discovery | A provider publishes the service named `event-link.v1` (a breaking change takes a new name; the SDK limits names to 127 bytes) through `PluginCommunicationService`: a table `{uint32 size; uint32 version; uint32 caps;}`, new fields only at the end, read only up to `size`. The consumer acquires it at load, keeps the handle until unload, and treats a missing service as `NoProvider`. |
| Versions | A newer provider table must keep serving older callers; new fields go at the end. `Check` refuses a provider older than `ContractVersion`. |
| Refusal | `Activate(provider, need, consume)` runs `consume` (take the item, charge) only on `Ok`. `NoProvider`, `OldVersion` and `MissingCaps` touch nothing. |
| Area identity | `AreaId = {level id, session generation}` from the SDK's `LevelChanged` event (UI thread). A `MonsterDeathEvent` has a level id but no session, so the consumer caches the session from the last `LevelChanged` event and builds the `AreaId` from it, both under one mutex. Cartographer reuses a slot's level id within one game: without `AreaIdentity` the consumer calls `AreaTracker::Opened` on every fresh open, which resets that entry (tested). |
| Progress | `AreaTracker` keeps one entry per area. Entering, killing in or closing a second area never edits the source's entry (source/destination independence for Horazon and Bitter Depths). Monster deaths come from the SDK's `MonsterDeathListener` (server thread, observation-only). |
| Threads and lifetime | Lifecycle callbacks arrive on the UI thread, death callbacks on the server thread: the real plugin guards the tracker with a mutex. Event pointers are valid only inside the callback. A map that carries a layered marker makes Cartographer log one warning ("a stat with a layer cannot be a map property") on open: expected, not a bug. Listeners and service handles are released at unload. |
| Metadata | Event metadata (the Catalyst's forced-event marker) must sit on a stat **with a layer** (`MarkerLayer`). Cartographer turns every layer-0 item stat into a player buff and drops a layered stat (`itemStatsToProperties`), so the marker never becomes a buff and no Cartographer change is needed. A layer-0 marker would become a native player stat (the check proves it). |

## Not decided here

- Which ItemStatCost row carries the marker. The ticket asks for an unused existing row, not a new one. The row needs a Save Param Bits setting so it can hold a layer. Ticket 4 publishes the reserved metadata definition (the row); ticket 10 owns the game proof that a layered stat survives the Catalyst recipe and a save.
- `RoomProgress` and `MonsterSetup` (spawn-time) hooks. The SDK offers death and level-change events only. Room activation and monster setup stay on core-events' native hooks until a provider publishes them.

## Check

```
cmake -S plugin -B plugin/build-checks -A x64 -DCARTOGRAPHER_SRC=<d2r-tools>/plugins/cartographer/src
cmake --build plugin/build-checks --config Release --target event_link_check
plugin/build-checks/Release/event_link_check.exe
```

Without `CARTOGRAPHER_SRC` the Cartographer checks print `SKIPPED` and the summary line counts them (`12 passed, 0 failed, 7 skipped`; with Cartographer `19 passed, 0 failed, 0 skipped`).
