void call_set_wearwalker_host()
{
	if (text_input("WearWalker IP/host", g_wearwalker_host, sizeof(g_wearwalker_host))) {
		printf("WearWalker host set to %s\n", g_wearwalker_host);
		ww_save_ui_config();
	}
}

void call_apply_wearwalker_endpoint()
{
	u16 port = wearwalker_wifi_menu_entries[WW_MENU_PORT].num_attr.value;

	ww_apply_endpoint_from_ui(port);
}

void call_apply_settings_endpoint()
{
	u16 port = settings_menu_entries[SETTINGS_MENU_PORT].num_attr.value;

	ww_apply_endpoint_from_ui(port);
}

void call_toggle_simple_mode()
{
	bool was_console_enabled = g_console_enabled;

	g_simple_mode = !g_simple_mode;
	g_console_enabled = !g_simple_mode;
	ww_save_ui_config();
	if (g_console_enabled)
		ww_init_debug_console();
	if (was_console_enabled || g_console_enabled)
		ww_clear_debug_console();
	g_active_menu = ww_get_main_menu();
	g_active_menu->props.selected = 0;
	if (g_console_enabled && g_debug_console_ready) {
		consoleSelect(&g_header_console);
		printf("WearWalker Bridge Test v%s\n", VER);
		consoleSelect(&logs);
	}
	printf("UI mode changed to %s\n", g_simple_mode ? "Simple" : "Debug");
}

void call_save_ui_config_now()
{
	if (ww_save_ui_config())
		printf("Saved settings to %s\n", WW_UI_CONFIG_PATH);
	else
		printf("Failed to save settings\n");
}

void call_set_wearwalker_trainer()
{
	if (text_input("Trainer for set-trainer", g_wearwalker_trainer, sizeof(g_wearwalker_trainer)))
		printf("Trainer command set to %s\n", g_wearwalker_trainer);
}

void call_wearwalker_status()
{
	if (!ww_async_start(WW_TASK_STATUS)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Requesting bridge status in background...\n");
}

void call_wearwalker_snapshot()
{
	if (!ww_async_start(WW_TASK_SNAPSHOT)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Requesting snapshot in background...\n");
}

void call_wearwalker_export_eeprom()
{
	if (!ww_async_start(WW_TASK_EXPORT_EEPROM)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Exporting WearWalker EEPROM in background...\n");
}

void call_wearwalker_import_eeprom()
{
	if (!ww_async_start(WW_TASK_IMPORT_EEPROM)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Importing EEPROM from PWEEPROM_IMPORT.bin in background...\n");
}

void call_wearwalker_command_set_steps()
{
	g_pending_steps = wearwalker_wifi_menu_entries[WW_MENU_CMD_STEPS_VALUE].num_attr.value;

	if (!ww_async_start(WW_TASK_COMMAND_SET_STEPS)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Sending set-steps command...\n");
}

void call_wearwalker_command_set_watts()
{
	g_pending_watts = wearwalker_wifi_menu_entries[WW_MENU_CMD_WATTS_VALUE].num_attr.value;

	if (!ww_async_start(WW_TASK_COMMAND_SET_WATTS)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Sending set-watts command...\n");
}

void call_wearwalker_command_set_sync()
{
	g_pending_sync = wearwalker_wifi_menu_entries[WW_MENU_CMD_SYNC_VALUE].num_attr.value;

	if (!ww_async_start(WW_TASK_COMMAND_SET_SYNC)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Sending set-sync command...\n");
}

void call_wearwalker_command_set_trainer()
{
	snprintf(g_pending_trainer, sizeof(g_pending_trainer), "%s", g_wearwalker_trainer);

	if (!ww_async_start(WW_TASK_COMMAND_SET_TRAINER)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Sending set-trainer command...\n");
}

void call_select_hgss_save()
{
	ww_browser_open_for_filter(WW_BROWSER_FILTER_SAV);
	g_state = IN_FILE_BROWSER;
	printf("Browsing for HGSS save on SD...\n");
}

void call_select_hgss_rom()
{
	ww_browser_open_for_filter(WW_BROWSER_FILTER_NDS);
	g_state = IN_FILE_BROWSER;
	printf("Browsing for HGSS ROM on SD...\n");
}

void call_show_hgss_save_path()
{
	if (!g_selected_hgss_save_path[0]) {
		printf("No HGSS save selected\n");
		return;
	}

	printf("Selected HGSS save: %s\n", g_selected_hgss_save_path);
}

void call_show_hgss_rom_path()
{
	if (!g_selected_hgss_nds_path[0]) {
		printf("No HGSS ROM selected\n");
		return;
	}

	printf("Selected HGSS ROM: %s\n", g_selected_hgss_nds_path);
}

static void ww_return_flow_reset(void)
{
	g_return_flow_active = false;
	g_return_apply_busy = false;
	g_return_step = WW_RETURN_STEP_CONFIRM;
	g_return_manual_capture_targets = false;
	g_return_capture_pick_index = 0;
	g_return_preview_source_species = 0;
	g_return_preview_source_name[0] = '\0';
	g_return_preview_walked_steps = 0;
	g_return_preview_exp_gain = 0;
	g_return_preview_sync_steps = 0;
	g_return_preview_sync_watts = 0;
	g_return_preview_sync_flags = 0;
	g_return_capture_count = 0;
	g_return_error[0] = '\0';
	memset(g_return_captures, 0, sizeof(g_return_captures));
}

static bool ww_return_validate_capture_target(u8 capture_index, u8 box, u8 slot)
{
	u8 i;

	if (capture_index >= g_return_capture_count)
		return false;
	if (box == 0 || box > HGSS_BOX_COUNT || slot == 0 || slot > HGSS_BOX_SLOTS)
		return false;
	if (box == g_pending_return_box && slot == g_pending_return_source_slot)
		return false;
	if (g_box_picker_slots[slot - 1].occupied && g_box_picker_slots[slot - 1].species_id != 0)
		return false;

	for (i = 0; i < g_return_capture_count; i++) {
		if (i == capture_index)
			continue;
		if (!g_return_captures[i].present)
			continue;
		if (g_return_captures[i].target_box == box && g_return_captures[i].target_slot == slot)
			return false;
	}

	return true;
}

static void ww_return_set_capture_auto_all(void)
{
	u8 i;

	for (i = 0; i < g_return_capture_count; i++) {
		g_return_captures[i].target_box = 0;
		g_return_captures[i].target_slot = 0;
	}
}

static bool ww_return_open_capture_picker(u8 capture_index)
{
	ww_box_picker_mode mode = WW_BOX_PICKER_NONE;

	if (capture_index >= g_return_capture_count)
		return false;

	switch (capture_index) {
		case 0:
			mode = WW_BOX_PICKER_RETURN_CAPTURE_1;
			break;
		case 1:
			mode = WW_BOX_PICKER_RETURN_CAPTURE_2;
			break;
		case 2:
			mode = WW_BOX_PICKER_RETURN_CAPTURE_3;
			break;
		default:
			return false;
	}

	g_return_capture_pick_index = capture_index;
	if (!ww_box_selector_open(mode))
		return false;

	printf(
			"Select destination for capture %u: %s\n",
			(unsigned)(capture_index + 1),
			g_return_captures[capture_index].species_name[0]
					? g_return_captures[capture_index].species_name
					: ww_lookup_species_name(g_return_captures[capture_index].species_id));
	return true;
}

static bool ww_return_fetch_trip_preview(void)
{
	char *sync_json = NULL;
	u32 walked_steps = 0;
	bool have_walked_steps = false;
	u32 species_id = 0;

	if (!ww_prepare_selected_save_path())
		return false;

	g_pending_return_box = 1;
	g_pending_return_source_slot = 1;
	g_pending_return_increment_trip_counter = true;

	have_walked_steps = ww_compute_simple_return_walked_steps(&walked_steps);
	g_return_preview_walked_steps = have_walked_steps ? walked_steps : 0;
	g_return_preview_exp_gain = g_return_preview_walked_steps;
	g_pending_return_walked_steps = g_return_preview_walked_steps;
	g_pending_return_bonus_watts = 0;
	g_pending_return_auto_captures = 0;
	g_return_error[0] = '\0';

	sync_json = (char *)malloc(WW_API_RESPONSE_MAX);
	if (!sync_json) {
		snprintf(g_return_error, sizeof(g_return_error), "out of memory");
		printf("Return preview failed: out of memory\n");
		if (sync_json)
			free(sync_json);
		return false;
	}

	if (!ww_api_get_sync_package(sync_json, WW_API_RESPONSE_MAX)) {
		snprintf(g_return_error, sizeof(g_return_error), "sync package unavailable");
		printf("Return preview failed: sync package unavailable\n");
		free(sync_json);
		return false;
	}

	if (!ww_json_get_u32_after_token(sync_json, "\"stats\"", "steps", &g_return_preview_sync_steps)
			|| !ww_json_get_u32_after_token(sync_json, "\"stats\"", "watts", &g_return_preview_sync_watts)) {
		snprintf(g_return_error, sizeof(g_return_error), "sync package missing steps/watts");
		printf("Return preview failed: sync package missing steps/watts\n");
		free(sync_json);
		return false;
	}
	if (!ww_json_get_u32_after_token(sync_json, "\"courseUnlocks\"", "unlockFlags", &g_return_preview_sync_flags))
		g_return_preview_sync_flags = 0;

	if (!ww_json_get_u32_after_token(sync_json, "\"stroll\"", "walkingSpecies", &species_id)
			|| species_id == 0
			|| species_id > 0xFFFFu) {
		snprintf(g_return_error, sizeof(g_return_error), "no walking Pokemon in device");
		printf("Return preview failed: no walking Pokemon in sync package\n");
		free(sync_json);
		return false;
	}
	g_return_preview_source_species = (u16)species_id;

	if (!ww_json_get_string_after_token(
				sync_json,
				"\"stroll\"",
				"walkingSpeciesName",
				g_return_preview_source_name,
				sizeof(g_return_preview_source_name))) {
		snprintf(
				g_return_preview_source_name,
				sizeof(g_return_preview_source_name),
				"%s",
				ww_lookup_species_name(g_return_preview_source_species));
	}

	g_return_capture_count = ww_json_get_caught_captures_from_sync(sync_json, g_return_captures);
	if (g_return_capture_count > WW_RETURN_CAPTURE_MAX)
		g_return_capture_count = WW_RETURN_CAPTURE_MAX;

	printf(
			"Return preview ready: %s +%lu EXP, captures=%u\n",
			g_return_preview_source_name,
			(unsigned long)g_return_preview_exp_gain,
			(unsigned)g_return_capture_count);

	free(sync_json);
	return true;
}

static bool ww_start_guided_return_apply(void)
{
	if (!ww_prepare_selected_save_path())
		return false;
	if (g_pending_return_box == 0 || g_pending_return_box > HGSS_BOX_COUNT)
		return false;
	if (g_pending_return_source_slot == 0 || g_pending_return_source_slot > HGSS_BOX_SLOTS)
		return false;

	g_pending_return_walked_steps = g_return_preview_walked_steps;
	g_pending_return_bonus_watts = 0;
	g_pending_return_auto_captures = 0;
	g_pending_return_increment_trip_counter = true;

	if (!ww_async_start(WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY)) {
		printf("WearWalker request already running\n");
		return false;
	}

	g_return_apply_busy = true;
	g_return_step = WW_RETURN_STEP_APPLYING;
	printf("Applying guided return to selected HGSS save...\n");
	return true;
}

void call_start_guided_return(void)
{
	if (!ww_prepare_selected_save_path())
		return;

	ww_return_flow_reset();
	g_return_flow_active = true;
	g_return_step = WW_RETURN_STEP_CONFIRM;
	g_return_error[0] = '\0';
	g_state = IN_RETURN_SELECTOR;

	printf("Guided return: confirm irreversible action to continue\n");
}

void call_start_guided_send(void)
{
	if (!ww_box_selector_open(WW_BOX_PICKER_SEND_SOURCE))
		return;

	printf("Guided send: select a Pokemon from your boxes\n");
}

void call_pick_stroll_send_slot()
{
	if (!ww_box_selector_open(WW_BOX_PICKER_SEND_SOURCE))
		return;

	printf("Visual picker: select source slot for send\n");
}

void call_pick_stroll_return_slot()
{
	if (!ww_box_selector_open(WW_BOX_PICKER_RETURN_SOURCE))
		return;

	printf("Visual picker: select source slot for return\n");
}

static bool ww_prepare_selected_save_path(void)
{
	struct stat info;

	if (!g_selected_hgss_save_path[0]) {
		printf("Select a HGSS .sav first\n");
		return false;
	}

	if (stat(g_selected_hgss_save_path, &info) != 0) {
		printf("Selected save path is not accessible\n");
		return false;
	}

	if (!S_ISREG(info.st_mode)) {
		printf("Selected save path is not a file\n");
		return false;
	}

	snprintf(g_pending_save_path, sizeof(g_pending_save_path), "%s", g_selected_hgss_save_path);
	return true;
}

static bool ww_prepare_selected_nds_path(void)
{
	struct stat info;

	if (!g_selected_hgss_nds_path[0]) {
		printf("Select a HGSS .nds first\n");
		return false;
	}

	if (!ww_name_has_nds_extension(g_selected_hgss_nds_path)) {
		printf("Selected ROM must use .nds extension\n");
		return false;
	}

	if (stat(g_selected_hgss_nds_path, &info) != 0) {
		printf("Selected ROM path is not accessible\n");
		return false;
	}

	if (!S_ISREG(info.st_mode)) {
		printf("Selected ROM path is not a file\n");
		return false;
	}

	snprintf(g_pending_nds_path, sizeof(g_pending_nds_path), "%s", g_selected_hgss_nds_path);
	return true;
}

static bool ww_prepare_hgss_patch_request(void)
{
	if (!ww_prepare_selected_save_path())
		return false;

	g_pending_increment_trip_counter = hgss_patch_menu_entries[HGSS_MENU_TRIP_COUNTER].num_attr.value != 0;
	return true;
}

static bool ww_compute_simple_return_walked_steps(u32 *out_walked_steps)
{
	char *sync_json;
	hgss_stroll_send_context source_context;
	char slot_error[128];
	u32 sync_steps;

	if (!out_walked_steps)
		return false;

	if (!hgss_read_stroll_send_context(
					g_pending_save_path,
					(u8)g_pending_return_box,
					(u8)g_pending_return_source_slot,
					&source_context,
					slot_error,
					sizeof(slot_error))) {
		printf("Simple mode: %s\n", slot_error[0] ? slot_error : "failed reading save context");
		return false;
	}

	sync_json = (char *)malloc(WW_API_RESPONSE_MAX);
	if (!sync_json)
		return false;

	if (!ww_api_get_sync_package(sync_json, WW_API_RESPONSE_MAX)) {
		free(sync_json);
		return false;
	}

	if (!ww_json_get_u32_after_token(sync_json, "\"stats\"", "steps", &sync_steps)) {
		free(sync_json);
		return false;
	}

	free(sync_json);

	if (sync_steps > source_context.pokewalker_steps)
		*out_walked_steps = sync_steps - source_context.pokewalker_steps;
	else
		*out_walked_steps = 0;

	return true;
}

void call_patch_hgss_manual()
{
	if (!ww_prepare_hgss_patch_request())
		return;

	printf("Using HGSS save: %s\n", g_pending_save_path);

	g_pending_hgss_steps = hgss_patch_menu_entries[HGSS_MENU_MANUAL_STEPS].num_attr.value;
	g_pending_hgss_watts = hgss_patch_menu_entries[HGSS_MENU_MANUAL_WATTS].num_attr.value;
	g_pending_hgss_course_flags = hgss_patch_menu_entries[HGSS_MENU_MANUAL_FLAGS].num_attr.value;

	if (!ww_async_start(WW_TASK_HGSS_PATCH_MANUAL)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Patching selected HGSS save with manual values...\n");
}

void call_patch_hgss_from_sync()
{
	if (!ww_prepare_hgss_patch_request())
		return;

	printf("Using HGSS save: %s\n", g_pending_save_path);

	if (!ww_async_start(WW_TASK_HGSS_PATCH_SYNC)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Fetching sync package and patching selected HGSS save...\n");
}

void call_apply_stroll_send_endpoint()
{
	u16 port = hgss_stroll_send_menu_entries[SEND_MENU_PORT].num_attr.value;

	ww_apply_endpoint_from_ui(port);
}

void call_show_stroll_send_slot()
{
	hgss_stroll_send_context context;
	char error[128];
	u32 box = hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value;
	u32 source_slot = hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value;

	if (!ww_prepare_selected_save_path())
		return;

	if (!hgss_read_stroll_send_context(g_pending_save_path, (u8)box, (u8)source_slot, &context, error, sizeof(error))) {
		printf("%s\n", error[0] ? error : "failed to read source slot");
		return;
	}

	if (!context.source_slot.occupied || context.source_slot.species_id == 0) {
		printf(
				"Source box %lu slot %lu is empty (return will try walker-pair restore)\n",
				(unsigned long)box,
				(unsigned long)source_slot);
		return;
	}

	printf(
			"Source box %lu slot %lu | species %u (%s) | exp %lu | lv~%u\n",
			(unsigned long)box,
			(unsigned long)source_slot,
			(unsigned)context.source_slot.species_id,
			ww_lookup_species_name(context.source_slot.species_id),
			(unsigned long)context.source_slot.exp,
			(unsigned)ww_estimate_level_from_exp(context.source_slot.exp));
	printf(
			"Nickname: %s | Held item id: %u | Friendship: %u\n",
			context.nickname[0] ? context.nickname : "(none)",
			(unsigned)context.held_item,
			(unsigned)context.source_slot.friendship);
	printf(
			"Moves: %u, %u, %u, %u\n",
			(unsigned)context.moves[0],
			(unsigned)context.moves[1],
			(unsigned)context.moves[2],
			(unsigned)context.moves[3]);
}

void call_stroll_send_from_save()
{
	if (!ww_prepare_selected_save_path())
		return;
	if (!ww_prepare_selected_nds_path())
		return;

	printf("Using HGSS save: %s\n", g_pending_save_path);
	printf("Using HGSS ROM: %s\n", g_pending_nds_path);

	g_pending_send_box = hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value;
	g_pending_send_slot = hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value;
	if (g_simple_mode)
		g_pending_send_course = g_route_selector_course;
	else
		g_pending_send_course = hgss_stroll_send_menu_entries[SEND_MENU_ROUTE].num_attr.value;
	if (g_simple_mode && g_state == IN_ROUTE_SELECTOR) {
		g_pending_send_route_seed = g_route_selector_preview_seed;
		g_pending_send_route_seed_valid = g_route_selector_preview_seed != 0;
	} else {
		g_pending_send_route_seed = 0;
		g_pending_send_route_seed_valid = false;
	}
	g_pending_send_allow_locked = hgss_stroll_send_menu_entries[SEND_MENU_ALLOW_LOCKED].num_attr.value != 0;
	g_pending_send_clear_buffers = hgss_stroll_send_menu_entries[SEND_MENU_CLEAR_BUFFERS].num_attr.value != 0;
	if (g_simple_mode) {
		g_pending_send_allow_locked = false;
		g_pending_send_clear_buffers = true;
		hgss_stroll_send_menu_entries[SEND_MENU_ROUTE].num_attr.value = g_pending_send_course;
	}

	if (!ww_async_start(WW_TASK_HGSS_STROLL_SEND)) {
		printf("WearWalker request already running\n");
		return;
	}

	if (g_simple_mode && g_state == IN_ROUTE_SELECTOR)
		g_route_send_busy = true;

	printf("Sending selected box Pokemon to stroll...\n");
}

void call_apply_stroll_return_endpoint()
{
	u16 port = hgss_stroll_return_menu_entries[RETURN_MENU_PORT].num_attr.value;

	ww_apply_endpoint_from_ui(port);
}

void call_show_stroll_return_slot()
{
	hgss_stroll_send_context context;
	char error[128];
	u32 box = hgss_stroll_return_menu_entries[RETURN_MENU_BOX].num_attr.value;
	u32 source_slot = hgss_stroll_return_menu_entries[RETURN_MENU_SOURCE_SLOT].num_attr.value;

	if (!ww_prepare_selected_save_path())
		return;

	if (!hgss_read_stroll_send_context(g_pending_save_path, (u8)box, (u8)source_slot, &context, error, sizeof(error))) {
		printf("%s\n", error[0] ? error : "failed to read source slot");
		return;
	}

	if (!context.source_slot.occupied || context.source_slot.species_id == 0) {
		printf("Source box %lu slot %lu is empty\n", (unsigned long)box, (unsigned long)source_slot);
		return;
	}

	printf(
			"Return source box %lu slot %lu | species %u (%s) | exp %lu | friendship %u\n",
			(unsigned long)box,
			(unsigned long)source_slot,
			(unsigned)context.source_slot.species_id,
			ww_lookup_species_name(context.source_slot.species_id),
			(unsigned long)context.source_slot.exp,
			(unsigned)context.source_slot.friendship);
	printf(
			"Nickname: %s | Held item id: %u | Moves: %u, %u, %u, %u\n",
			context.nickname[0] ? context.nickname : "(none)",
			(unsigned)context.held_item,
			(unsigned)context.moves[0],
			(unsigned)context.moves[1],
			(unsigned)context.moves[2],
			(unsigned)context.moves[3]);
}

void call_stroll_return_to_save()
{
	u32 simple_walked_steps = 0;
	bool have_simple_walked_steps = false;

	if (!ww_prepare_selected_save_path())
		return;

	printf("Using HGSS save: %s\n", g_pending_save_path);

	g_pending_return_box = hgss_stroll_return_menu_entries[RETURN_MENU_BOX].num_attr.value;
	g_pending_return_source_slot = hgss_stroll_return_menu_entries[RETURN_MENU_SOURCE_SLOT].num_attr.value;
	g_pending_return_target_slot = hgss_stroll_return_menu_entries[RETURN_MENU_TARGET_SLOT].num_attr.value;
	g_pending_return_walked_steps = hgss_stroll_return_menu_entries[RETURN_MENU_WALKED_STEPS].num_attr.value;
	g_pending_return_bonus_watts = hgss_stroll_return_menu_entries[RETURN_MENU_BONUS_WATTS].num_attr.value;
	g_pending_return_auto_captures = hgss_stroll_return_menu_entries[RETURN_MENU_AUTO_CAPTURES].num_attr.value;
	g_pending_return_increment_trip_counter = hgss_stroll_return_menu_entries[RETURN_MENU_INCREMENT_TRIP].num_attr.value != 0;
	if (g_simple_mode) {
		have_simple_walked_steps = ww_compute_simple_return_walked_steps(&simple_walked_steps);
		g_pending_return_walked_steps = have_simple_walked_steps ? simple_walked_steps : 0;
		g_pending_return_bonus_watts = 0;
		g_pending_return_auto_captures = 0;
		g_pending_return_increment_trip_counter = true;
		g_pending_return_target_slot = 0;
		printf(
				"Simple mode return: walked steps=%lu (%s), bonus=0, auto-captures=0\n",
				(unsigned long)g_pending_return_walked_steps,
				have_simple_walked_steps ? "sync delta" : "fallback");
	}

	if (!ww_async_start(WW_TASK_HGSS_STROLL_RETURN)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Applying stroll return and patching selected HGSS save...\n");
}

