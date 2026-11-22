// Stash affinity (#1553): a map, bossing or map-material item quick-moved from the inventory into the stash goes to its
// reserved shared stash page and cell (common/stash_home.h), and the Maps tab's selected base's maps are swapped onto
// the Maps page.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace stash_affinity {

// Starts the game-thread watch with the first game. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The routing counters, for the status command.
void Counters(char* out, size_t size) noexcept;

// Any thread: the map base the Maps tab shows (its item code; 0 for none). The game thread moves the last base's maps
// back to their tier's page and this one's onto the Maps page.
void ShowBase(uint32_t code) noexcept;

// Any thread: whether the Maps or Bossing tab is on screen (and its shared page), so the game thread tidies their pages
// and the message hook refuses a drop into a cell that is not the item's own (#1726).
void SetTabOpen(bool open, uint32_t page = 0) noexcept;

// Any thread: the shared page of the open Maps or Bossing tab, 0 when neither is open.
auto ViewPage() noexcept -> uint32_t;

// Any thread (#1744): queues a hand drop's stack of the cursor item (runtime id) onto a stored stack (runtime id) for
// the game thread, which merges them through Merge when the stack sits on its own cell of the open tab's page and both
// are the same stackable item; otherwise the cursor item stays. False when nothing could be queued.
auto StackOnto(uint32_t cursorId, uint32_t targetId) noexcept -> bool;

// Any thread (#1766): queues for the game thread a resend of the item (runtime id) a refused move left pending on the
// client: the cursor item, or a quick-moved item still in the inventory or Cube. False when nothing could be queued.
auto Resend(uint32_t itemId) noexcept -> bool;

// Any thread (#1772): queues for the game thread a click's take (0x46) of a reserved-page stack past WithdrawMax: the stack
// moves to the inventory, and the watch's next pass splits WithdrawMax of it onto the cursor and sends the rest home
// (Withdraw). False when nothing could be queued.
auto TakeOut(uint32_t itemId) noexcept -> bool;

// Any thread: the base the Maps tab shows (0 for none).
auto WantedBase() noexcept -> uint32_t;

}  // namespace stash_affinity
