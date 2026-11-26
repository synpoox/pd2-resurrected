#pragma once

#include <cstring>

namespace pd2::menu_text {

// Gold NPC menu lines already carry color 4 separately; their TextBox treats an inline name marker as text.
inline auto GoldName(const char* text, int color) noexcept -> const char* {
	return text != nullptr && color == 4 && std::strncmp(text, "\xee\x81\xbe" "4", 4) == 0 ? text + 4 : text;
}

}  // namespace pd2::menu_text
