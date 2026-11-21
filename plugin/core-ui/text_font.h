// D2R's text-draw font block (D2WinFont, 0xa4 bytes): the defaults its builder 0x86d1b0 writes, then the fields set
// here (offsets from the style reflection at 0x8bd030: pointSize +4, fontColor +8 (r, g, b, a ints), alignment +0x28
// (h, v; left 0, center 1, right 2, from 0x8ba810), dropShadow offset +0x70 and colour +0x78). Last int: the font
// preset, 0xe for none. Used by the item quantity.
#pragma once

#include <cstdint>

namespace textfont {

struct Font {
	int32_t v[0x29];
};
static_assert(sizeof(Font) == 0xa4);

constexpr auto Make(int32_t pointSize, int32_t horizontal, int32_t vertical) noexcept -> Font {
	Font f {};
	f.v[1] = pointSize;
	for (int i = 2; i <= 9; ++i) {
		f.v[i] = 0xff;  // fontColor white (PD2's font colour 0), and the colour after it at its default
	}
	f.v[0xa] = horizontal;
	f.v[0xb] = vertical;
	f.v[0xc] = 0x3f800000;  // 1.0f, as the builder
	f.v[0xd] = 0x3f800000;
	f.v[0x10] = -1;
	f.v[0x11] = 0x100;
	f.v[0x1c] = 3;  // drop shadow offset x, y
	f.v[0x1d] = 3;
	f.v[0x21] = 200;  // shadow colour 0, 0, 0, 200 (the builder's default)
	f.v[0x25] = 0xff;
	f.v[0x28] = 0xe;
	return f;
}

}  // namespace textfont
