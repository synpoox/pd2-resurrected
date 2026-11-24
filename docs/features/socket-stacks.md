# Socketing from a stack

PD2's gems and runes stack; D2R's never do, so D2R's socket insert puts the whole cursor unit in the socket. core-rules
(`plugin/core-rules/socket_rules.cpp`, [#2119](https://github.com/synpoox/pd2-resurrected/issues/2119)) makes a stack of N
put one in and leave N-1 on the cursor, for the host and for joined players.

## How it works

1. **Insert hook** (`SocketInsert`, 93847 `0x46eb00`, by signature; `(game, player, filler id, parent id, flag) -> int`,
   1 placed / 0 refused). For a filler held on the cursor (mode 4) with quantity (stat 70) over 1, it sets the quantity to
   1, runs D2R's insert, and, when the filler went in (mode 6), queues the N-1 rest. A refused insert gets its quantity back.
2. **The rest** is made on the next game update through the SDK (`createItem`: cursor, else inventory, else ground).
   - The local player's handle comes from `LocalPlayerReady` (or the UI thread).
   - A joined player's handle comes from its peer (`getPeerPlayer`). Each client's core-rules opens a private SDK network
     channel (`ChannelId` 1, token `PD2R2119`) once its player is ready, and names its player unit id to the host. The host
     keeps unit id -> peer, cleared when a game starts; the first claim of an id wins.
3. A stacked insert is **refused** (D2R's own put-back, so nothing is lost; the stack may land in the inventory) when
   no rest can be made: a joined player with no known peer, or a full rest queue (16).

Proof: game-checked 2026-11-23 by the owner. Single player: a stack of 2 El left 1 on the cursor. Hosted TCP/IP (Slot2 host,
Slot3 client): the client's stack socketed one and kept the rest. Status line: `stacks socketed one N (remote refused M)`; "remote refused" also counts local inserts refused by a full queue.

## Pitfalls (read before changing it)

- **The hook must return the insert's result.** The packet handler (`0x4f9f40`) tests the int return at `0x4fa29c` and runs
  its put-back on 0. A `void` hook left `eax` garbage: the placed rune was put back too (asserts `added` at PlrMsg.cpp:5589
  and `!ITEMSGetContainer(item)` at ItemMode.cpp:2700, then a crash at `D2R.exe+0x372378`).
- **Gate on the cursor mode.** Six other callers reach `0x46eb00`, the save loaders among them (PlrSave.cpp, PlrSave2.cpp):
  they re-socket children when a save loads. Splitting there loses items.
- **Test success by item mode 6, not the page.** A cursor item already has item page `0xff`, so the page test is always true.
- **`createItem` needs `qualityRecordId = RandomQualityRecord`.** 0 is refused with `InvalidArgument` on every container.
- **The SDK answers `Busy` inside the packet handler**, so the rest can't be made in the same tick through the SDK.
- **No native same-tick rest (tried, reverted; [#2142](https://github.com/synpoox/pd2-resurrected/issues/2142)).** ItemSpawn,
  the imbue's three ready calls (`0x46ea70`, `0x53bb50`, `0x43c910`), the placer `0x471500` and `SendItem` `0x535f60`
  (flags 1, action 2) left the rest on the server's cursor (seen after a rejoin) but never shown: `0x535f60` updates an item the
  client already knows, and imbue places into the inventory, not the cursor. A native version needs D2R's
  new-item-to-cursor send.
- **Runewords.** When the insert completes a runeword, the handler takes the filler back to the cursor and replays the insert
  later (`0x4b1a29`), so the socketed unit must stay the packet's filler id: never socket a substitute unit. With the SDK rest
  the take-back finds the cursor taken, so the rest goes to the inventory (a split stack, nothing lost).
- **Multiplayer trust.** The SDK cannot map a `PlayerHandle` back to a unit id, so a client's claim is not checked. First
  claim wins, so a modified client can only claim an id nobody holds yet.
- **Matching plugin sets.** A TCP/IP client with a different plugin set (or plugin versions) than the host could not join.
  Test slots must carry the same companions before a hosted check.

## Known gaps

- Leaving the game in the one update between the insert and the queued job loses the rest, as with the stash-tab rests.
- A joined player whose peer drops between the insert and the job loses the rest (a log warning names it).
- If the host's `LocalPlayerReady` ever fires mid-game, the peer map is cleared and joined players' stacked inserts are
  refused (nothing is lost) until they rejoin.
- Saves written while #2119 was live may hold a socketed gem or rune with quantity over 1; it is harmless (compact, the
  quantity is not saved) and is never split (the cursor gate).
- A client whose channel connection drops mid-game does not reconnect until the next game: its stacked inserts are refused
  (the stack stays whole) for the rest of that game.
- A client that never connected keeps retrying `connectToHost` for up to about 600 UI frames (~10 s), even after the game
  ends; a retry that overlaps the next game's connect gets `Conflict` and stops. Harmless, but bounded only by that budget.
