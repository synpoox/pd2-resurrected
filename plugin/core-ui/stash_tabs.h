// Stash tabs (#1553): a second stash tab row with the Maps tab (tiers, bases, the cube) and the Bossing tab.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace stash_tabs {

// Registers the click listener and the frame watch. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// `core-ui stash <tab>`: queues the request for the UI thread; false (reply says why): bad word
auto Console(const char* args, char* reply, size_t size) noexcept -> bool;

// Whether the stash has this shared page (true while the page count is unknown).
auto HasPage(uint32_t page) noexcept -> bool;

// The tab state, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace stash_tabs
