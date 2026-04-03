#include "ui/components/ui_sprite_components.h"

#include <citro2d.h>
#include <string.h>

static void ww_build_sprite_palette(u32 color_seed, bool colorize, u32 out_colors[4])
{
	static const u8 accents[][3] = {
		{0xD6, 0x4E, 0x5A},
		{0x5A, 0xA6, 0xE0},
		{0x68, 0xB7, 0x5D},
		{0xE3, 0xB1, 0x4F},
		{0xA8, 0x76, 0xE3},
		{0xE2, 0x84, 0x59},
		{0x59, 0xC3, 0xB1},
		{0xD0, 0x63, 0xA6},
		{0x6E, 0x8D, 0xE8},
		{0xC5, 0x6E, 0x58},
		{0x65, 0xB6, 0x7A},
		{0xB5, 0x8B, 0x58},
	};
	const u8 *accent;
	u32 idx;
	u32 r;
	u32 g;
	u32 b;

	if (!out_colors)
		return;

	if (!colorize) {
		out_colors[0] = C2D_Color32(0xEC, 0xF4, 0xF5, 0xFF);
		out_colors[1] = C2D_Color32(0xBE, 0xCF, 0xD2, 0xFF);
		out_colors[2] = C2D_Color32(0x67, 0x7E, 0x83, 0xFF);
		out_colors[3] = C2D_Color32(0x20, 0x2D, 0x31, 0xFF);
		return;
	}

	idx = color_seed % (sizeof(accents) / sizeof(accents[0]));
	accent = accents[idx];
	r = accent[0];
	g = accent[1];
	b = accent[2];

	out_colors[0] = C2D_Color32(
			(u8)((r * 18u + 255u * 82u) / 100u),
			(u8)((g * 18u + 255u * 82u) / 100u),
			(u8)((b * 18u + 255u * 82u) / 100u),
			0xFF);
	out_colors[1] = C2D_Color32(
			(u8)((r * 45u + 255u * 55u) / 100u),
			(u8)((g * 45u + 255u * 55u) / 100u),
			(u8)((b * 45u + 255u * 55u) / 100u),
			0xFF);
	out_colors[2] = C2D_Color32(
			(u8)((r * 80u + 255u * 20u) / 100u),
			(u8)((g * 80u + 255u * 20u) / 100u),
			(u8)((b * 80u + 255u * 20u) / 100u),
			0xFF);
	out_colors[3] = C2D_Color32(
			(u8)((r * 52u) / 100u),
			(u8)((g * 52u) / 100u),
			(u8)((b * 52u) / 100u),
			0xFF);
}

void ww_draw_2bpp_sprite(
		const u8 *sprite,
		u32 width,
		u32 height,
		float x,
		float y,
		float scale,
		bool transparent_zero,
		u32 color_seed,
		bool colorize)
{
	u32 shade_colors[4];
	u32 py;
	u32 blocks_y;
	u32 expected_bytes;

	ww_build_sprite_palette(color_seed, colorize, shade_colors);

	if (!sprite || width == 0 || height == 0)
		return;

	blocks_y = (height + 7u) / 8u;
	expected_bytes = width * blocks_y * 2u;

	for (py = 0; py < height; py++) {
		u32 px = 0;
		u32 block_y = py >> 3;
		u32 bit_y = py & 0x7u;

		while (px < width) {
			u8 shade;
			u32 run_start = px;
			u32 entry_index = block_y * width + px;
			u32 byte_index = entry_index * 2u;
			u16 column_word;

			if (byte_index + 1u >= expected_bytes)
				return;

			column_word = (u16)sprite[byte_index] | ((u16)sprite[byte_index + 1u] << 8);
			shade = (u8)(((column_word >> bit_y) & 0x1u) | (((column_word >> (bit_y + 8u)) & 0x1u) << 1u));

			if (transparent_zero && shade == 0) {
				px++;
				continue;
			}

			px++;
			while (px < width) {
				u8 next_shade;
				u32 next_entry = block_y * width + px;
				u32 next_byte_index = next_entry * 2u;
				u16 next_word;

				if (next_byte_index + 1u >= expected_bytes)
					return;

				next_word = (u16)sprite[next_byte_index] | ((u16)sprite[next_byte_index + 1u] << 8);
				next_shade = (u8)(((next_word >> bit_y) & 0x1u) | (((next_word >> (bit_y + 8u)) & 0x1u) << 1u));

				if (transparent_zero && next_shade == 0)
					break;
				if (next_shade != shade)
					break;

				px++;
			}

			C2D_DrawRectSolid(
					x + (float)run_start * scale,
					y + (float)py * scale,
					0.0f,
					(float)(px - run_start) * scale,
					scale,
					shade_colors[shade]);
		}
	}
}

void ww_draw_indexed_icon(
		const u8 *indices,
		u32 width,
		u32 height,
		const u32 *palette,
		float x,
		float y,
		float scale,
		bool transparent_zero)
{
	u32 py;

	if (!indices || !palette || width == 0 || height == 0)
		return;

	for (py = 0; py < height; py++) {
		u32 px = 0;

		while (px < width) {
			u8 color_index = indices[py * width + px] & 0x0Fu;
			u32 run_start = px;

			if (transparent_zero && color_index == 0) {
				px++;
				continue;
			}

			px++;
			while (px < width) {
				u8 next_color = indices[py * width + px] & 0x0Fu;

				if (transparent_zero && next_color == 0)
					break;
				if (next_color != color_index)
					break;

				px++;
			}

			C2D_DrawRectSolid(
					x + (float)run_start * scale,
					y + (float)py * scale,
					0.0f,
					(float)(px - run_start) * scale,
					scale,
					palette[color_index]);
		}
	}
}

void ww_draw_item_token_sprite(u16 item_id, float x, float y, float scale)
{
	u8 token[16];
	u32 px;

	memset(token, 0, sizeof(token));

	for (px = 0; px < 8; px++) {
		u16 column_word = 0;
		u32 py;

		for (py = 0; py < 8; py++) {
			u8 shade;

			if (px == 0 || px == 7 || py == 0 || py == 7) {
				shade = 3;
			} else {
				u32 bit = ((u32)item_id >> ((px + py * 3u) & 0x0Fu)) & 0x1u;
				shade = bit ? 2 : 1;
			}

			if (shade & 0x1u)
				column_word |= (u16)(1u << py);
			if (shade & 0x2u)
				column_word |= (u16)(1u << (py + 8u));
		}

		token[px * 2u] = (u8)(column_word & 0xFFu);
		token[px * 2u + 1u] = (u8)(column_word >> 8);
	}

	ww_draw_2bpp_sprite(token, 8, 8, x, y, scale, false, ((u32)item_id) ^ 0x9E3779B9u, true);
}
