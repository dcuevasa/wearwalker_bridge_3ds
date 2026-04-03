#ifndef UI_SPRITE_COMPONENTS_H
#define UI_SPRITE_COMPONENTS_H

#include <3ds.h>

void ww_draw_2bpp_sprite(
const u8 *sprite,
u32 width,
u32 height,
float x,
float y,
float scale,
bool transparent_zero,
u32 color_seed,
bool colorize);

void ww_draw_item_token_sprite(u16 item_id, float x, float y, float scale);

void ww_draw_indexed_icon(
const u8 *indices,
u32 width,
u32 height,
const u32 *palette,
float x,
float y,
float scale,
bool transparent_zero);

#endif
