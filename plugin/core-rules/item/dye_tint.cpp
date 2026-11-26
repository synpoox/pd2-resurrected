// Item dye tint: PD2's transform_dye recolours the item sprite (#1529).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"


// PD2's dye (transform_dye, set by the corruption recipes) recolours the item sprite, never its cell (#1529). PD2 stores the
// colors.txt row + 1 (0 = none; the Annihilus corruptions' 10 is cred). No PluginSDK service draws items, so two draw paths
// are hooked, each by a unique signature:
// - Legacy: D2R picks an item's colour in one function (93847: 0x3710d0, D2Common Items.cpp): (player, item, out packed
//   index, 0 body / 1 inventory) -> the 256-byte palette shift of (items.txt Transform +0x151 or InvTrans +0x152, colors.txt
//   row), or null; its unique and set branches read invtransform the same way. A dyed item takes that path with the dye.
// - HD: the shift above becomes a colour transform class (itemtransforms.json) that recolours only the hues it targets, so
//   most HD art keeps its colour. The HD item draw (0xc7020: item, sprite, packed xy, scale, alpha; inventory and cursor)
//   fills a draw record and passes it to the UI sprite draw (0x858510: sprite, xy, record, scale). For a dyed item the
//   record drops the transform (+0x1d) and takes the dye as its RGBA multiplier (+0x00, weight +0x0c = 1), as in game.
//   Only that one call (0xc71f3) is redirected, not 0x858510's entry, which other plugins hook (better-automap). Both
//   callers of 0xc7020 run it only in HD (they test 0x846210 first and draw legacy items elsewhere), so no mode test here.
using ItemColorFn = const uint8_t* (*)(Unit* player, Unit* item, uint8_t* index, int32_t inventory) noexcept;
using PaletteShiftFn = const uint8_t* (*)(int32_t type, int32_t color) noexcept;
using ItemDrawFn = void (*)(Unit* item, void* sprite, uint64_t xy, float scale, void* alpha) noexcept;
using SpriteDrawFn = void (*)(void* sprite, int32_t* xy, float* record, float scale) noexcept;
ItemColorFn g_itemColorOriginal = nullptr;
PaletteShiftFn g_paletteShift = nullptr;
ItemDrawFn g_itemDrawOriginal = nullptr;
SpriteDrawFn g_spriteDrawOriginal = nullptr;
thread_local int32_t g_drawDye = -1;  // the dye of the item whose HD draw is running, -1 none

// RGB multiplier per colors.txt row, in its order (the test checks the codes).
constexpr float DyeRgb[21][3] = {
    {1.0f, 1.0f, 1.0f},     // whit
    {0.8f, 0.8f, 0.8f},     // lgry
    {0.5f, 0.5f, 0.5f},     // dgry
    {0.25f, 0.25f, 0.25f},  // blac
    {0.5f, 0.7f, 1.0f},     // lblu
    {0.3f, 0.4f, 0.8f},     // dblu
    {0.3f, 0.6f, 1.0f},     // cblu
    {1.0f, 0.5f, 0.5f},     // lred
    {0.6f, 0.15f, 0.15f},   // dred
    {1.0f, 0.2f, 0.2f},     // cred
    {0.5f, 1.0f, 0.5f},     // lgrn
    {0.2f, 0.55f, 0.2f},    // dgrn
    {0.3f, 1.0f, 0.4f},     // cgrn
    {1.0f, 1.0f, 0.55f},    // lyel
    {0.75f, 0.7f, 0.2f},    // dyel
    {1.0f, 0.85f, 0.45f},   // lgld
    {0.75f, 0.6f, 0.25f},   // dgld
    {0.8f, 0.5f, 1.0f},     // lpur
    {0.5f, 0.25f, 0.7f},    // dpur
    {1.0f, 0.6f, 0.2f},     // oran
    {1.0f, 1.0f, 1.0f},     // bwht
};

auto DyeOf(Unit* item) noexcept -> int32_t {
	const int32_t color = item != nullptr ? Stat(item, StatDye) - 1 : -1;
	return color >= 0 && color < 21 ? color : -1;
}

auto DyedItemColor(Unit* player, Unit* item, uint8_t* index, int32_t inventory) noexcept -> const uint8_t* {
	const int32_t color = DyeOf(item);
	const auto* row = color >= 0 ? d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::ItemRecord)(
	                                   d2r::api.unitDataFlag(item), At<int32_t>(item, d2r::rva::UnitClassId))
	                             : nullptr;
	if (row == nullptr) {
		return g_itemColorOriginal(player, item, index, inventory);
	}
	// The unique branch packs the body type, which charms lack (Transform 0), so a type-0 item packs its InvTrans.
	const uint8_t type = inventory != 0 ? row[0x152] : row[0x151];
	const uint8_t packed = row[0x151] != 0 ? row[0x151] : type;
	*index = packed >= 1 && packed <= 8 ? static_cast<uint8_t>(packed << 5 | color) : 0;
	return g_paletteShift(type, color);
}

auto DyedItemDraw(Unit* item, void* sprite, uint64_t xy, float scale, void* alpha) noexcept -> void {
	g_drawDye = DyeOf(item);
	g_itemDrawOriginal(item, sprite, xy, scale, alpha);
	g_drawDye = -1;
}

auto DyedSpriteDraw(void* sprite, int32_t* xy, float* record, float scale) noexcept -> void {
	const int32_t color = g_drawDye;
	if (color < 0 || record == nullptr) {
		return g_spriteDrawOriginal(sprite, xy, record, scale);
	}
	g_drawDye = -1;  // the item draw's one sprite: nothing it draws later is tinted
	alignas(16) float copy[0x2a];  // the record (0xa8 bytes, the item draw's local); D2R's stays as it was
	std::memcpy(copy, record, sizeof(copy));
	std::memcpy(copy, DyeRgb[color], sizeof(DyeRgb[color]));
	copy[3] = 1.0f;
	reinterpret_cast<uint8_t*>(copy)[0x1d] = 0;
	g_spriteDrawOriginal(sprite, xy, copy, scale);
}



// "" when both parts are in, else which part failed (for the install report).
auto InstallDyeTint(const D2RL::PluginContext* context) noexcept -> const char* {
	uint64_t shift = 0;
	static constexpr uint8_t ColorEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x44, 0x89, 0x4c, 0x24, 0x20, 0x55, 0x56, 0x57,
	                                         0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
	// mov r11,rsp / push rbp / push rdi / push r15 / lea rbp,[r11-0xb8]
	static constexpr uint8_t DrawEntry[] = {0x4c, 0x8b, 0xdc, 0x55, 0x57, 0x41, 0x57, 0x49, 0x8d, 0xab, 0x48, 0xff, 0xff, 0xff};
	if (sigscan::ScanUnique(context->exeBase, "48 89 5c 24 10 57 48 83 ec 20 48 63 d9 48 63 fa 83 fb 09 7d 05 83 ff 15 7c 1e", 0, &shift) == 1) {
		g_paletteShift = reinterpret_cast<PaletteShiftFn>(context->exeBase + shift);
	}
	const bool legacy = g_paletteShift != nullptr &&
	                    sigscan::HookBySignature(
	                        context, "ItemColor",
	                        "48 89 5c 24 08 44 89 4c 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8b ec 48 83 ec 30 45 8b e1 4d 8b e8 48 8b fa 4c 8b f9 48 85 d2 0f 84",
	                        0, ColorEntry, sizeof(ColorEntry), DyedItemColor, &g_itemColorOriginal);
	// the call first: the item draw hook only marks the draw the redirected call tints
	// The HD item draw's one call of the UI sprite draw (93847: 0xc71f3, `E8 rel32` at +22 of this match)
	const bool hd = RedirectCall(context, "f3 0f 11 44 24 70 0f 28 de 4c 8d 44 24 60 48 8d 54 24 28 49 8b cf e8 ?? ?? ?? ?? c6 47 04 00",
	                             22, reinterpret_cast<uint64_t>(&DyedSpriteDraw), reinterpret_cast<uint64_t*>(&g_spriteDrawOriginal)) &&
	                sigscan::HookBySignature(context, "HdItemDraw",
	                                         "4c 8b dc 55 57 41 57 49 8d ab 48 ff ff ff 48 81 ec a0 01 00 00 41 0f 29 73 b8", 0,
	                                         DrawEntry, sizeof(DrawEntry), DyedItemDraw, &g_itemDrawOriginal);
	return legacy && hd ? ""
	       : hd         ? " FAILED (legacy colour: signature or bytes differ)"
	       : legacy     ? " FAILED (HD sprite: signature, bytes or relay)"
	                    : " FAILED (legacy colour and HD sprite)";
}
