// Controller QoL's item modifier (#1442): the pad button held with A for its item shortcuts (default LB). Read from its
// own config, `modifier = "..."` in [qol] of d2rloader/config/controller-qol-updates.toml. No engine or Windows types.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pd2::pad_modifier {

// XInput button bits, and the two triggers above them
inline constexpr uint32_t LB = 0x0100, RB = 0x0200, L3 = 0x0040, R3 = 0x0080, LT = 0x10000, RT = 0x20000;

constexpr auto Trim(std::string_view s) noexcept -> std::string_view {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
	return s;
}

constexpr auto Lower(char c) noexcept -> char { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

constexpr auto Is(std::string_view a, std::string_view lower) noexcept -> bool {
	if (a.size() != lower.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) if (Lower(a[i]) != lower[i]) return false;
	return true;
}

// The buttons the config's value names, as Controller QoL's own parser reads them (IsControllerModifierActive in its
// src/controller_input.cpp @8dab5f83); LB for an unknown value
constexpr auto Mask(std::string_view value) noexcept -> uint32_t {
	if (value.empty() || Is(value, "trigger") || Is(value, "l2") || Is(value, "lt")) return LT;
	if (Is(value, "rb") || Is(value, "r1")) return RB;
	if (Is(value, "rt") || Is(value, "r2")) return RT;
	if (Is(value, "l3")) return L3;
	if (Is(value, "r3")) return R3;
	if (Is(value, "any")) return LB | LT;
	return LB;  // "bumper", "lb", "l1", and anything else
}

// The mask for the `modifier = "x"` line of the config text (a line that starts with `modifier`, so `require_modifier`
// and `ground_pickup_button` do not count; a comment does not either); LB when there is none
constexpr auto FromConfig(std::string_view text) noexcept -> uint32_t {
	while (!text.empty()) {
		const size_t eol = text.find('\n');
		std::string_view line = Trim(text.substr(0, eol));
		text = eol == std::string_view::npos ? std::string_view {} : text.substr(eol + 1);
		if (line.substr(0, 8) != "modifier") continue;
		line = Trim(line.substr(8));
		if (line.empty() || line.front() != '=') continue;
		line = Trim(line.substr(1));
		if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = Trim(line.substr(0, hash));
		if (line.size() >= 2 && line.front() == '"' && line.back() == '"') line = line.substr(1, line.size() - 2);
		return Mask(line);
	}
	return LB;
}

}  // namespace pd2::pad_modifier
