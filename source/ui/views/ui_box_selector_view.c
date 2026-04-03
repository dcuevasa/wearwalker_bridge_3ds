static void ww_draw_box_selector(void)
{
	const char *title = "Select slot";
	const float origin_x = 10.0f;
	const float origin_y = 56.0f;
	const float cell_w = 49.0f;
	const float cell_h = 30.0f;
	u32 row;
	u32 col;
	char info[96];

	if (g_box_picker_mode == WW_BOX_PICKER_SEND_SOURCE)
		title = "Select source slot (Send)";
	else if (g_box_picker_mode == WW_BOX_PICKER_RETURN_SOURCE)
		title = "Select destination for returned Pokemon";
	else if (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_1)
		title = "Select destination for capture #1";
	else if (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_2)
		title = "Select destination for capture #2";
	else if (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_3)
		title = "Select destination for capture #3";

	draw_top(title);
	snprintf(info, sizeof(info), "Box %lu / %u", (unsigned long)g_box_picker_box, (unsigned)HGSS_BOX_COUNT);
	draw_string(8, 34, 11, info, false, 0);

	for (row = 0; row < 5; row++) {
		for (col = 0; col < 6; col++) {
			u32 index = row * 6 + col;
			u32 slot = index + 1;
			bool selected = slot == g_box_picker_slot;
			bool occupied = g_box_picker_slots[index].occupied && g_box_picker_slots[index].species_id != 0;
			u32 bg = selected
					? COLOR_SEL
					: (occupied ? C2D_Color32(0x2A, 0x74, 0x3C, 0xFF) : C2D_Color32(0x43, 0x43, 0x43, 0xFF));
			char slot_label[8];

			C2D_DrawRectSolid(origin_x + cell_w * col, origin_y + cell_h * row, 0.0f, cell_w - 3.0f, cell_h - 3.0f, bg);
			snprintf(slot_label, sizeof(slot_label), "%02lu", (unsigned long)slot);
			draw_string(origin_x + cell_w * col + 4.0f, origin_y + cell_h * row + 2.0f, 9, slot_label, false, 0);

			if (occupied) {
				snprintf(info, sizeof(info), "#%u", (unsigned)g_box_picker_slots[index].species_id);
				draw_string(origin_x + cell_w * col + 21.0f, origin_y + cell_h * row + 14.0f, 8, info, false, 0);
			}
		}
	}

	if (g_box_picker_reload_ok) {
		hgss_box_slot_summary *slot = &g_box_picker_slots[g_box_picker_slot - 1];
		if (slot->occupied && slot->species_id != 0) {
			const char *species_name = ww_lookup_species_name(slot->species_id);
			const char *nickname = g_box_picker_context_valid[g_box_picker_slot - 1]
					? g_box_picker_context[g_box_picker_slot - 1].nickname
					: "";

			if (nickname && nickname[0]) {
				snprintf(
						info,
						sizeof(info),
						"Slot %lu: %s (%s) lv~%u",
						(unsigned long)g_box_picker_slot,
						nickname,
						species_name,
						(unsigned)ww_estimate_level_from_exp(slot->exp));
			} else {
				snprintf(
						info,
						sizeof(info),
						"Slot %lu: %s lv~%u",
						(unsigned long)g_box_picker_slot,
						species_name,
						(unsigned)ww_estimate_level_from_exp(slot->exp));
			}
		} else {
			snprintf(info, sizeof(info), "Slot %lu: empty", (unsigned long)g_box_picker_slot);
		}
		draw_string(0, 212, 9, info, true, 0);
	} else {
		draw_string(0, 212, 9, g_box_picker_error[0] ? g_box_picker_error : "Could not read slots", true, 0);
	}

	if (g_return_flow_active && g_box_picker_mode == WW_BOX_PICKER_RETURN_SOURCE)
		draw_string(0, 226, 8, "A: choose destination  L/R: change box", true, 0);
	else if (g_return_flow_active
			&& (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_1
					|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_2
					|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_3))
		draw_string(0, 226, 8, "A: choose  B: back  L/R: change box", true, 0);
	else
		draw_string(0, 226, 8, "A: choose  B: cancel  L/R: change box", true, 0);
}

