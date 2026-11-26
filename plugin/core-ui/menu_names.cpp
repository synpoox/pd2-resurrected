// NPC transaction menus (#2249) set the item-name line's color to gold themselves. Inline item-name color markers
// belong to the tooltip font path; the menu's TextBox prints their fallback glyph and selector as `*4`.
#include "menu_names.h"

#include "menu_text.h"
#include "sig_hook.h"

#include <cstdint>
#include <cstdio>

namespace menu_names {
namespace {

// 93847 AddNpcMenuLine (0x1e9030): text at line +0x30, independent color at +0xac, 0x90-byte records.
// Pattern starts past the entry so another hook cannot hide the signature; one match in the known image.
constexpr const char* Pattern = "83 79 1c 0a 41 8b d9 41 8b e8 4c 8b f2 48 8b f1";
constexpr uint8_t Entry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
	                        0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20};
using AddLineFn = void (*)(void*, const char*, int32_t, int32_t, void*, int32_t) noexcept;
AddLineFn g_addLine = nullptr;

void AddLine(void* menu, const char* text, int32_t color, int32_t layout, void* action, int32_t selectable) noexcept {
	g_addLine(menu, pd2::menu_text::GoldName(text, color), color, layout, action, selectable);
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	// The SDK has tooltip listeners, but no callback for NPC transaction-menu text.
	const bool ok = sigscan::HookBySignature(context, "AddNpcMenuLine", Pattern, -18, Entry, sizeof(Entry), AddLine, &g_addLine);
	std::snprintf(report, size, ok ? "gold menu names omit redundant color markers" : "FAILED (menu line signature or bytes differ)");
}

}  // namespace menu_names
