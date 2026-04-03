static void ww_draw_simple_top_panel(void)
{
	char line[128];
	u32 i;

	C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, 240, C2D_Color32(0x10, 0x25, 0x2C, 0xFF));
	if (g_state == IN_ROUTE_SELECTOR) {
		C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, 8, C2D_Color32(0x17, 0x4A, 0x57, 0xFF));
	} else {
		C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, 18, C2D_Color32(0x17, 0x4A, 0x57, 0xFF));
		ww_draw_string_width(8, 3, 8.8f, "Simple", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
	}

	if (g_state == IN_BOX_SELECTOR) {
		hgss_box_slot_summary *slot = &g_box_picker_slots[g_box_picker_slot - 1];
		const char *species_name = ww_lookup_species_name(slot->species_id);
		const char *nickname = g_box_picker_context_valid[g_box_picker_slot - 1]
				? g_box_picker_context[g_box_picker_slot - 1].nickname
				: "";

		if (!slot->occupied || slot->species_id == 0) {
			g_box_picker_sprite_ready = false;
			g_box_picker_color_icon_ready = false;
			g_box_picker_sprite_species = 0xFFFF;
		}

		if (slot->occupied && slot->species_id != 0 && g_box_picker_sprite_species != slot->species_id) {
			u8 *large_narc_data = NULL;
			u32 large_narc_size = 0;
			u8 *poke_icon_narc_data = NULL;
			u32 poke_icon_narc_size = 0;
			u8 frame1[0x300];

			g_box_picker_sprite_ready = false;
			g_box_picker_color_icon_ready = false;
			g_box_picker_color_icon_width = 0;
			g_box_picker_color_icon_height = 0;
			memset(g_box_picker_color_icon_indices, 0, sizeof(g_box_picker_color_icon_indices));
			memset(g_box_picker_color_icon_palette, 0, sizeof(g_box_picker_color_icon_palette));
			g_box_picker_sprite_species = slot->species_id;

			if (g_selected_hgss_nds_path[0]
					&& (ww_load_narc_from_nds(g_selected_hgss_nds_path, "a/0/2/0", &poke_icon_narc_data, &poke_icon_narc_size)
							|| ww_load_narc_from_nds(
									g_selected_hgss_nds_path,
									"poketool/icongra/poke_icon/poke_icon",
									&poke_icon_narc_data,
									&poke_icon_narc_size)
							|| ww_load_narc_from_nds(
									g_selected_hgss_nds_path,
									"poketool/icongra/poke_icon/poke_icon.narc",
									&poke_icon_narc_data,
									&poke_icon_narc_size))
					&& ww_extract_species_color_icon_from_narc(
							poke_icon_narc_data,
							poke_icon_narc_size,
							slot->species_id,
							g_box_picker_color_icon_indices,
							g_box_picker_color_icon_palette,
							&g_box_picker_color_icon_width,
							&g_box_picker_color_icon_height)) {
				g_box_picker_color_icon_ready = true;
			}

			if (poke_icon_narc_data)
				free(poke_icon_narc_data);

			if (!g_box_picker_color_icon_ready
					&& g_selected_hgss_nds_path[0]
					&& ww_load_narc_from_nds(g_selected_hgss_nds_path, "a/2/5/6", &large_narc_data, &large_narc_size)
					&& ww_extract_species_large_frames(
							large_narc_data,
							large_narc_size,
							slot->species_id,
							g_box_picker_sprite,
							frame1)) {
				ww_remap_sprite_2bpp_contrast(g_box_picker_sprite, sizeof(g_box_picker_sprite));
				g_box_picker_sprite_ready = true;
			}

			if (large_narc_data)
				free(large_narc_data);
		}

		C2D_DrawRectSolid(12, 40, 0, 140, 188, C2D_Color32(0x13, 0x34, 0x3F, 0xFF));
		if (g_box_picker_color_icon_ready) {
			ww_draw_indexed_icon(
					g_box_picker_color_icon_indices,
					g_box_picker_color_icon_width,
					g_box_picker_color_icon_height,
					g_box_picker_color_icon_palette,
					14.0f,
					46.0f,
					3.30f,
					true);
		} else if (g_box_picker_sprite_ready) {
			ww_draw_2bpp_sprite(
					g_box_picker_sprite,
					64,
					48,
					18.0f,
					52.0f,
					1.8f,
					false,
					0u,
					false);
		}

		snprintf(line, sizeof(line), "Box %lu Slot %lu", (unsigned long)g_box_picker_box, (unsigned long)g_box_picker_slot);
		ww_draw_string_width(170, 50, 12.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);

		if (slot->occupied && slot->species_id != 0) {
			u8 level_est = ww_estimate_level_from_exp(slot->exp);
			u64 level_floor = (u64)level_est * (u64)level_est * (u64)level_est;
			u64 level_next = level_est >= 100
					? level_floor
					: (u64)(level_est + 1) * (u64)(level_est + 1) * (u64)(level_est + 1);
			u32 level_span = level_next > level_floor ? (u32)(level_next - level_floor) : 1u;
			u32 level_prog = (u32)(slot->exp > level_floor ? ((u64)slot->exp - level_floor) : 0u);
			u32 exp_bar_pct = level_est >= 100 ? 100u : (level_prog >= level_span ? 100u : (level_prog * 100u) / level_span);

			snprintf(line, sizeof(line), "%s", species_name);
			ww_draw_string_width(170, 76, 12.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);

			snprintf(line, sizeof(line), "Nickname: %s", (nickname && nickname[0]) ? nickname : "(none)");
			ww_draw_string_width(170, 98, 10.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);

			snprintf(line, sizeof(line), "Lv~%u  Friendship %u", (unsigned)level_est, (unsigned)slot->friendship);
			ww_draw_string_width(170, 116, 10.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);

			snprintf(line, sizeof(line), "EXP %lu", (unsigned long)slot->exp);
			ww_draw_string_width(170, 134, 9.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
			C2D_DrawRectSolid(170.0f, 148.0f, 0.0f, 170.0f, 8.0f, C2D_Color32(0x2B, 0x46, 0x4C, 0xFF));
			C2D_DrawRectSolid(170.0f, 148.0f, 0.0f, (170.0f * (float)exp_bar_pct) / 100.0f, 8.0f, C2D_Color32(0x6D, 0xC1, 0x8D, 0xFF));
			snprintf(line, sizeof(line), "%u%% to next level", (unsigned)exp_bar_pct);
			ww_draw_string_width(170, 160, 8.7f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
		} else {
			ww_draw_string_width(170, 90, 12.8f, "Empty slot", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
		}

		ww_draw_string_width(170, 186, 9.6f, "A select  B back  L/R box", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
		return;
	}

	if (g_state == IN_ROUTE_SELECTOR) {
		u32 slot_index;
		u32 progress_pct = 0;
		char progress_label[48];

		if (g_route_send_busy) {
			u32 dot_count = (g_ui_anim_tick / 16u) % 4u;
			char dots[5];

			ww_async_progress_get(&progress_pct, progress_label, sizeof(progress_label));

			for (i = 0; i < sizeof(dots) - 1; i++)
				dots[i] = (i < dot_count) ? '.' : '\0';
			dots[sizeof(dots) - 1] = '\0';

			C2D_DrawRectSolid(12, 40, 0, 376, 188, C2D_Color32(0x13, 0x34, 0x3F, 0xFF));
			snprintf(line, sizeof(line), "Sending Pokemon%s", dots);
			ww_draw_string_width(0, 84, 21, line, true, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
			snprintf(line, sizeof(line), "%s (%lu%%)", progress_label, (unsigned long)progress_pct);
			ww_draw_string_width(0, 116, 13, line, true, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
			C2D_DrawRectSolid(44.0f, 142.0f, 0.0f, 312.0f, 18.0f, C2D_Color32(0x2B, 0x46, 0x4C, 0xFF));
			C2D_DrawRectSolid(44.0f, 142.0f, 0.0f, (312.0f * (float)progress_pct) / 100.0f, 18.0f, C2D_Color32(0x6D, 0xC1, 0x8D, 0xFF));
			ww_draw_string_width(0, 172, 10.5f, "Returning to menu after completion", true, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
			return;
		}

		C2D_DrawRectSolid(6, 6, 0, 388, 228, C2D_Color32(0x13, 0x34, 0x3F, 0xFF));
		ww_draw_string_width(12, 8, 10.8f, "Encounter Pokemon (6)", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);

		for (slot_index = 0; slot_index < WW_ROUTE_PREVIEW_SLOT_COUNT; slot_index++) {
			u32 row = slot_index / 3u;
			u32 col = slot_index % 3u;
			float x = 8.0f + (float)col * 130.0f;
			float y = 20.0f + (float)row * 56.0f;
			u32 row_color = g_route_preview_selected_group[slot_index] >= 0
					? C2D_Color32(0x2A, 0x64, 0x52, 0xFF)
					: C2D_Color32(0x1A, 0x44, 0x50, 0xFF);

			C2D_DrawRectSolid(x, y, 0.0f, 126.0f, 50.0f, row_color);
			if (g_route_preview_slots[slot_index].color_icon_ready) {
				ww_draw_indexed_icon(
						g_route_preview_slots[slot_index].color_icon_indices,
						g_route_preview_slots[slot_index].color_icon_width,
						g_route_preview_slots[slot_index].color_icon_height,
						g_route_preview_slots[slot_index].color_icon_palette,
						x + 4.0f,
						y + 3.0f,
						1.08f,
						true);
			} else if (g_route_preview_slots[slot_index].sprite_ready) {
				ww_draw_2bpp_sprite(
						g_route_preview_slots[slot_index].sprite_frame0,
						32,
						24,
						x + 5.0f,
						y + 6.0f,
						1.20f,
						true,
						0u,
						false);
			}

			snprintf(
					line,
					sizeof(line),
					"%lu. %.11s",
					(unsigned long)(slot_index + 1),
					g_route_preview_slots[slot_index].species_name);
			ww_draw_string_width(x + 45.0f, y + 6.0f, 8.9f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);

			snprintf(
					line,
					sizeof(line),
					"%u%% | %u steps",
					(unsigned)g_route_preview_slots[slot_index].chance,
					(unsigned)g_route_preview_slots[slot_index].min_steps);
			ww_draw_string_width(x + 45.0f, y + 22.0f, 8.3f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
			snprintf(line, sizeof(line), "Lv %u", (unsigned)g_route_preview_slots[slot_index].level);
			ww_draw_string_width(x + 45.0f, y + 34.0f, 7.9f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
		}

		ww_draw_string_width(12, 132, 11.4f, "Route Items (10)", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
		for (i = 0; i < WW_OV112_ITEM_COUNT; i++) {
			u32 row = i / 5u;
			u32 col = i % 5u;
			float x = 8.0f + (float)col * 77.0f;
			float y = 144.0f + (float)row * 42.0f;

			C2D_DrawRectSolid(x, y, 0.0f, 74.0f, 40.0f, C2D_Color32(0x1A, 0x44, 0x50, 0xFF));
			if (g_route_preview_items[i].icon_ready) {
				ww_draw_indexed_icon(
						g_route_preview_items[i].icon_indices,
						g_route_preview_items[i].icon_width,
						g_route_preview_items[i].icon_height,
						g_route_preview_items[i].icon_palette,
						x + 2.0f,
						y + 2.0f,
						0.95f,
						true);
			} else {
				ww_draw_item_token_sprite(g_route_preview_items[i].item_id, x + 4.0f, y + 8.0f, 1.55f);
			}
			snprintf(
					line,
					sizeof(line),
					"%.8s",
					g_route_preview_items[i].item_name);
			ww_draw_string_width(x + 34.0f, y + 4.0f, 9.4f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
			snprintf(
					line,
					sizeof(line),
					"%u%% %ust",
					(unsigned)g_route_preview_items[i].chance,
					(unsigned)g_route_preview_items[i].min_steps);
			ww_draw_string_width(x + 34.0f, y + 22.0f, 8.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
		}

		return;
	}

	if (g_state == IN_RETURN_SELECTOR) {
		switch (g_return_step) {
			case WW_RETURN_STEP_CONFIRM:
				ww_draw_string_width(12, 52, 12, "Return confirmation", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				ww_draw_string_width(12, 76, 10, "This action is irreversible once confirmed.", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				break;
			case WW_RETURN_STEP_CAPTURE_POLICY:
				ww_draw_string_width(12, 52, 11.5f, "Trip results loaded", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				snprintf(
						line,
						sizeof(line),
						"Returned: %s",
						g_return_preview_source_name[0]
								? g_return_preview_source_name
								: ww_lookup_species_name(g_return_preview_source_species));
				ww_draw_string_width(12, 74, 10, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				snprintf(line, sizeof(line), "Captures: %u", (unsigned)g_return_capture_count);
				ww_draw_string_width(12, 94, 10, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				break;
			case WW_RETURN_STEP_REVIEW:
				ww_draw_string_width(12, 52, 11.5f, "Ready to apply return", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				snprintf(
						line,
						sizeof(line),
						"Box %lu Slot %lu | EXP +%lu",
						(unsigned long)g_pending_return_box,
						(unsigned long)g_pending_return_source_slot,
						(unsigned long)g_return_preview_exp_gain);
				ww_draw_string_width(12, 74, 9.8f, line, false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				break;
			case WW_RETURN_STEP_APPLYING:
				ww_draw_string_width(12, 52, 12, "Applying guided return", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
				break;
		}

		return;
	}

	ww_draw_string_width(12, 52, 11, "Use Send or Return from the main menu.", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
	ww_draw_string_width(12, 74, 10, "Set HGSS save/ROM only in Settings.", false, 0, TOP_SCREEN_WIDTH, COLOR_TEXT);
}

