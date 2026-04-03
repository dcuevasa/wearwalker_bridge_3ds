static void ww_draw_route_selector(void)
{
	char title[80];
	char line[192];
	char progress_label[48];
	const char *course_name;
	const char *type0_name;
	const char *type1_name;
	const char *type2_name;
	const char *selected_species_name;
	u8 selected_level;
	u8 type0 = g_route_preview_adv_types[0] < 18 ? g_route_preview_adv_types[0] : 9;
	u8 type1 = g_route_preview_adv_types[1] < 18 ? g_route_preview_adv_types[1] : 9;
	u8 type2 = g_route_preview_adv_types[2] < 18 ? g_route_preview_adv_types[2] : 9;
	u32 progress_pct = 0;
	u32 exp_bar_pct;

	snprintf(
			title,
			sizeof(title),
			"Route %lu details",
			(unsigned long)(g_route_selector_course + 1));
	draw_top(title);

	if (!g_route_selector_ready) {
		draw_string(0, 108, 12, g_route_selector_error[0] ? g_route_selector_error : "Route preview unavailable", true, 0);
		draw_string(0, 224, 8, "DPad left/right route  A send  B back", true, 0);
		return;
	}

	course_name = g_route_course_names[g_route_selector_course];
	type0_name = g_type_names[type0];
	type1_name = g_type_names[type1];
	type2_name = g_type_names[type2];
	selected_species_name = ww_lookup_species_name(g_guided_send_context.source_slot.species_id);
	selected_level = ww_estimate_level_from_exp(g_guided_send_context.source_slot.exp);
	{
		u64 level_floor = (u64)selected_level * (u64)selected_level * (u64)selected_level;
		u64 level_next = selected_level >= 100
				? level_floor
				: (u64)(selected_level + 1) * (u64)(selected_level + 1) * (u64)(selected_level + 1);
		u32 level_span = level_next > level_floor ? (u32)(level_next - level_floor) : 1u;
		u32 level_prog = (u32)(g_guided_send_context.source_slot.exp > level_floor
				? ((u64)g_guided_send_context.source_slot.exp - level_floor)
				: 0u);

		exp_bar_pct = selected_level >= 100 ? 100u : (level_prog >= level_span ? 100u : (level_prog * 100u) / level_span);
	}

	C2D_DrawRectSolid(8.0f, 36.0f, 0.0f, 304.0f, 174.0f, C2D_Color32(0x14, 0x34, 0x3E, 0xFF));
	C2D_DrawRectSolid(16.0f, 52.0f, 0.0f, 96.0f, 84.0f, C2D_Color32(0x1D, 0x45, 0x52, 0xFF));

	if (g_route_preview_area_ready)
		ww_draw_2bpp_sprite(g_route_preview_area_sprite, 32, 24, 22.0f, 58.0f, 2.25f, true, 0u, false);
	else
		draw_string(22.0f, 88.0f, 8.5f, "No area", false, 0);

	snprintf(line, sizeof(line), "Route %lu: %.24s", (unsigned long)(g_route_selector_course + 1), course_name);
	draw_string(118.0f, 54.0f, 10.5f, line, false, 0);
	snprintf(line, sizeof(line), "Watts: %lu", (unsigned long)g_route_selector_current_watts);
	draw_string(118.0f, 74.0f, 10.0f, line, false, 0);

	if (g_route_selector_special_lock) {
		draw_string(118.0f, 94.0f, 8.8f, "Status: LOCKED (special/event)", false, 0);
	} else if (g_route_selector_locked) {
		snprintf(line, sizeof(line), "Status: LOCKED (need %ldW)", (long)g_route_selector_required_watts);
		draw_string(118.0f, 94.0f, 8.8f, line, false, 0);
	} else {
		snprintf(line, sizeof(line), "Status: UNLOCKED (%ldW)", (long)g_route_selector_required_watts);
		draw_string(118.0f, 94.0f, 8.8f, line, false, 0);
	}

	draw_string(118.0f, 112.0f, 8.8f, "Types with advantage:", false, 0);
	snprintf(line, sizeof(line), "%.10s / %.10s / %.10s", type0_name, type1_name, type2_name);
	draw_string(118.0f, 126.0f, 8.8f, line, false, 0);

	snprintf(
			line,
			sizeof(line),
			"Selected: %.14s Lv~%u  Friendship %u",
			selected_species_name,
			(unsigned)selected_level,
			(unsigned)g_guided_send_context.source_slot.friendship);
	draw_string(118.0f, 142.0f, 8.1f, line, false, 0);
	snprintf(line, sizeof(line), "EXP %lu", (unsigned long)g_guided_send_context.source_slot.exp);
	draw_string(118.0f, 156.0f, 8.1f, line, false, 0);
	C2D_DrawRectSolid(118.0f, 170.0f, 0.0f, 180.0f, 8.0f, C2D_Color32(0x2B, 0x46, 0x4C, 0xFF));
	C2D_DrawRectSolid(118.0f, 170.0f, 0.0f, (180.0f * (float)exp_bar_pct) / 100.0f, 8.0f, C2D_Color32(0x6D, 0xC1, 0x8D, 0xFF));
	snprintf(line, sizeof(line), "%u%% to next level", (unsigned)exp_bar_pct);
	draw_string(118.0f, 182.0f, 7.6f, line, false, 0);

	if (g_route_send_busy) {
		ww_async_progress_get(&progress_pct, progress_label, sizeof(progress_label));

		C2D_DrawRectSolid(14.0f, 150.0f, 0.0f, 292.0f, 52.0f, C2D_Color32(0x20, 0x4B, 0x59, 0xEE));
		draw_string(0.0f, 154.0f, 9.5f, "Sending to Pokewalker", true, 0);
		snprintf(line, sizeof(line), "%s (%lu%%)", progress_label, (unsigned long)progress_pct);
		draw_string(0.0f, 168.0f, 8.6f, line, true, 0);
		if (ww_async_can_cancel_before_start())
			draw_string(0.0f, 184.0f, 8.4f, "B: cancel while pending", true, 0);
		else
			draw_string(0.0f, 184.0f, 8.4f, "Please wait...", true, 0);
	} else {
		draw_string(0, 224, 8, "DPad left/right route  A send  B back", true, 0);
	}

	return;
}

static void ww_draw_return_selector(void)
{
	char line[192];
	u32 i;

	draw_top("Guided Stroll Return");
	C2D_DrawRectSolid(8.0f, 36.0f, 0.0f, 304.0f, 196.0f, C2D_Color32(0x14, 0x34, 0x3E, 0xFF));

	if (g_return_step == WW_RETURN_STEP_CONFIRM) {
		draw_string(16.0f, 52.0f, 11.0f, "Return Pokemon from Pokewalker?", false, 0);
		draw_string(16.0f, 76.0f, 9.8f, "This action cannot be undone.", false, 0);
		draw_string(16.0f, 94.0f, 9.4f, "The app will fetch trip results", false, 0);
		draw_string(16.0f, 108.0f, 9.4f, "from API and then ask placements.", false, 0);
		if (g_return_error[0]) {
			draw_string(16.0f, 128.0f, 8.8f, "Last error:", false, 0);
			draw_string(16.0f, 140.0f, 8.5f, g_return_error, false, 0);
			draw_string(16.0f, 162.0f, 9.2f, "A: retry", false, 0);
			draw_string(16.0f, 176.0f, 9.2f, "B: cancel", false, 0);
		} else {
			draw_string(16.0f, 138.0f, 9.2f, "A: confirm and continue", false, 0);
			draw_string(16.0f, 152.0f, 9.2f, "B: cancel", false, 0);
		}
		draw_string(16.0f, 186.0f, 8.6f, "After confirmation, continue until save patch completes.", false, 0);
		return;
	}

	if (g_return_step == WW_RETURN_STEP_CAPTURE_POLICY) {
		snprintf(
				line,
				sizeof(line),
				"Returned: %s (+%lu EXP)",
				g_return_preview_source_name[0] ? g_return_preview_source_name : ww_lookup_species_name(g_return_preview_source_species),
				(unsigned long)g_return_preview_exp_gain);
		draw_string(16.0f, 52.0f, 10.8f, line, false, 0);
		snprintf(
				line,
				sizeof(line),
				"Destination selected: Box %lu Slot %lu",
				(unsigned long)g_pending_return_box,
				(unsigned long)g_pending_return_source_slot);
		draw_string(16.0f, 70.0f, 9.4f, line, false, 0);

		snprintf(line, sizeof(line), "Captures reported by trip: %u", (unsigned)g_return_capture_count);
		draw_string(16.0f, 92.0f, 9.4f, line, false, 0);

		for (i = 0; i < g_return_capture_count && i < WW_RETURN_CAPTURE_MAX; i++) {
			snprintf(
					line,
					sizeof(line),
					"%lu) %s Lv%u",
					(unsigned long)(i + 1),
					g_return_captures[i].species_name[0] ? g_return_captures[i].species_name : ww_lookup_species_name(g_return_captures[i].species_id),
					(unsigned)g_return_captures[i].level);
			draw_string(22.0f, 108.0f + (float)i * 14.0f, 8.8f, line, false, 0);
		}

		if (g_return_capture_count == 0) {
			draw_string(16.0f, 158.0f, 9.2f, "No captures to place.", false, 0);
			draw_string(16.0f, 176.0f, 9.2f, "A: continue", false, 0);
		} else {
			draw_string(16.0f, 158.0f, 9.0f, "A: auto-place captures", false, 0);
			draw_string(16.0f, 172.0f, 9.0f, "X: choose slots manually", false, 0);
		}
		draw_string(16.0f, 198.0f, 8.5f, "B: change returned Pokemon slot", false, 0);
		return;
	}

	if (g_return_step == WW_RETURN_STEP_REVIEW) {
		snprintf(
				line,
				sizeof(line),
				"Return %s to Box %lu Slot %lu",
				g_return_preview_source_name[0] ? g_return_preview_source_name : ww_lookup_species_name(g_return_preview_source_species),
				(unsigned long)g_pending_return_box,
				(unsigned long)g_pending_return_source_slot);
		draw_string(16.0f, 52.0f, 10.6f, line, false, 0);
		snprintf(line, sizeof(line), "Capture mode: %s", g_return_manual_capture_targets ? "manual" : "auto");
		draw_string(16.0f, 66.0f, 9.0f, line, false, 0);

		draw_string(16.0f, 82.0f, 9.2f, "Capture placement plan:", false, 0);
		if (g_return_capture_count == 0) {
			draw_string(22.0f, 98.0f, 9.0f, "(none)", false, 0);
		} else {
			for (i = 0; i < g_return_capture_count && i < WW_RETURN_CAPTURE_MAX; i++) {
				if (g_return_captures[i].target_box == 0 || g_return_captures[i].target_slot == 0) {
					snprintf(
							line,
							sizeof(line),
							"%lu) %s -> auto",
							(unsigned long)(i + 1),
							g_return_captures[i].species_name[0]
									? g_return_captures[i].species_name
									: ww_lookup_species_name(g_return_captures[i].species_id));
				} else {
					snprintf(
							line,
							sizeof(line),
							"%lu) %s -> Box %u Slot %u",
							(unsigned long)(i + 1),
							g_return_captures[i].species_name[0]
									? g_return_captures[i].species_name
									: ww_lookup_species_name(g_return_captures[i].species_id),
							(unsigned)g_return_captures[i].target_box,
							(unsigned)g_return_captures[i].target_slot);
				}
				draw_string(22.0f, 98.0f + (float)i * 14.0f, 8.7f, line, false, 0);
			}
		}

		draw_string(16.0f, 176.0f, 9.2f, "A: apply return and patch save", false, 0);
		draw_string(16.0f, 194.0f, 8.8f, "B: adjust capture placement", false, 0);
		return;
	}

	if (g_return_step == WW_RETURN_STEP_APPLYING) {
		u32 progress_pct = 0;
		char progress_label[48];

		ww_async_progress_get(&progress_pct, progress_label, sizeof(progress_label));
		draw_string(0.0f, 84.0f, 14.0f, "Applying Return", true, 0);
		snprintf(line, sizeof(line), "%s (%lu%%)", progress_label, (unsigned long)progress_pct);
		draw_string(0.0f, 114.0f, 10.0f, line, true, 0);
		C2D_DrawRectSolid(40.0f, 142.0f, 0.0f, 240.0f, 16.0f, C2D_Color32(0x2B, 0x46, 0x4C, 0xFF));
		C2D_DrawRectSolid(40.0f, 142.0f, 0.0f, (240.0f * (float)progress_pct) / 100.0f, 16.0f, C2D_Color32(0x6D, 0xC1, 0x8D, 0xFF));
		if (ww_async_can_cancel_before_start())
			draw_string(0.0f, 172.0f, 9.0f, "B: cancel while pending", true, 0);
		else
			draw_string(0.0f, 172.0f, 9.0f, "Please wait, returning to main menu...", true, 0);
		return;
	}

	draw_string(16.0f, 84.0f, 10.0f, "Return flow is idle.", false, 0);
}

