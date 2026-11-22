// PD2's vendor and gamble price rules D2R lacks (#1539): weapons ignore their stack size, gamble prices ignore the stack
// midpoint, and no vendor takes a rune (#1777) (item_prices.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs both patches (each only when its signature is found exactly once); `report` gets one entry. True when both are in.
auto InstallItemPrices(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
auto ItemPriceCounters(char* out, size_t size) noexcept -> void;
