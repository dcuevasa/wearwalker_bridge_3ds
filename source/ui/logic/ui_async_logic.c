static bool ww_async_has_timed_out(ww_async_task task, char *json, u32 json_size);
static bool ww_async_consume_cancel_before_start(char *json, u32 json_size);

static u64 ww_async_timeout_for_task(ww_async_task task)
{
	switch (task) {
		case WW_TASK_HGSS_STROLL_SEND:
			return WW_ASYNC_TIMEOUT_STROLL_SEND_MS;
		case WW_TASK_HGSS_STROLL_RETURN:
		case WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY:
			return WW_ASYNC_TIMEOUT_STROLL_RETURN_MS;
		default:
			return 0;
	}
}

static bool ww_async_wait_prestart_window(ww_async_task task, char *json, u32 json_size, const char *label)
{
	u64 deadline_ms;

	if (label && label[0])
		ww_async_progress_set(0, label);

	deadline_ms = osGetTime() + WW_ASYNC_PRESTART_WINDOW_MS;
	while (osGetTime() < deadline_ms) {
		if (ww_async_consume_cancel_before_start(json, json_size))
			return false;
		if (ww_async_has_timed_out(task, json, json_size))
			return false;
		svcSleepThread(100000000LL);
	}

	return true;
}

static bool ww_async_has_timed_out(ww_async_task task, char *json, u32 json_size)
{
	u64 started_ms;
	u64 timeout_ms;
	u64 elapsed_ms;

	LightLock_Lock(&g_ww_async.lock);
	started_ms = g_ww_async.started_ms;
	timeout_ms = g_ww_async.timeout_ms;
	LightLock_Unlock(&g_ww_async.lock);

	if (timeout_ms == 0)
		return false;

	elapsed_ms = osGetTime() - started_ms;
	if (elapsed_ms <= timeout_ms)
		return false;

	if (json && json_size > 0) {
		snprintf(
				json,
				json_size,
				"%s timed out after %lu seconds",
				ww_async_task_name(task),
				(unsigned long)(elapsed_ms / 1000u));
	}

	return true;
}

static bool ww_async_can_cancel_before_start(void)
{
	bool can_cancel;

	LightLock_Lock(&g_ww_async.lock);
	can_cancel = g_ww_async.running && !g_ww_async.remote_started;
	LightLock_Unlock(&g_ww_async.lock);

	return can_cancel;
}

static bool ww_async_request_cancel_before_start(void)
{
	bool requested = false;

	LightLock_Lock(&g_ww_async.lock);
	if (g_ww_async.running && !g_ww_async.remote_started) {
		g_ww_async.cancel_requested = true;
		requested = true;
	}
	LightLock_Unlock(&g_ww_async.lock);

	return requested;
}

static bool ww_async_consume_cancel_before_start(char *json, u32 json_size)
{
	bool cancel = false;

	LightLock_Lock(&g_ww_async.lock);
	if (g_ww_async.cancel_requested && !g_ww_async.remote_started) {
		g_ww_async.cancel_requested = false;
		cancel = true;
	}
	LightLock_Unlock(&g_ww_async.lock);

	if (cancel && json && json_size > 0)
		snprintf(json, json_size, "operation cancelled before remote start");

	return cancel;
}

static void ww_async_mark_remote_started(void)
{
	LightLock_Lock(&g_ww_async.lock);
	g_ww_async.remote_started = true;
	LightLock_Unlock(&g_ww_async.lock);
}

static bool ww_async_should_abort_before_remote(ww_async_task task, char *json, u32 json_size)
{
	if (ww_async_consume_cancel_before_start(json, json_size))
		return true;

	return ww_async_has_timed_out(task, json, json_size);
}

static void ww_async_worker(void *arg)
{
	ww_async_task task = (ww_async_task)(uintptr_t)arg;
	bool success = false;
	char *json = (char *)malloc(WW_API_RESPONSE_MAX);
	wearwalker_snapshot snapshot = {0};
	u32 pending_steps = g_pending_steps;
	u32 pending_watts = g_pending_watts;
	u32 pending_sync = g_pending_sync;
	u32 pending_hgss_steps = g_pending_hgss_steps;
	u32 pending_hgss_watts = g_pending_hgss_watts;
	u32 pending_hgss_course_flags = g_pending_hgss_course_flags;
	bool pending_increment_trip_counter = g_pending_increment_trip_counter;
	u32 pending_send_box = g_pending_send_box;
	u32 pending_send_slot = g_pending_send_slot;
	u32 pending_send_course = g_pending_send_course;
	u32 pending_send_route_seed = g_pending_send_route_seed;
	bool pending_send_route_seed_valid = g_pending_send_route_seed_valid;
	bool pending_send_allow_locked = g_pending_send_allow_locked;
	bool pending_send_clear_buffers = g_pending_send_clear_buffers;
	u32 pending_return_box = g_pending_return_box;
	u32 pending_return_source_slot = g_pending_return_source_slot;
	u32 pending_return_target_slot = g_pending_return_target_slot;
	u32 pending_return_walked_steps = g_pending_return_walked_steps;
	u32 pending_return_bonus_watts = g_pending_return_bonus_watts;
	u32 pending_return_auto_captures = g_pending_return_auto_captures;
	bool pending_return_increment_trip_counter = g_pending_return_increment_trip_counter;
	u16 pending_return_expected_species = g_return_preview_source_species;
	u32 pending_return_exp_gain = g_return_preview_exp_gain;
	u32 pending_return_sync_steps = g_return_preview_sync_steps;
	u32 pending_return_sync_watts = g_return_preview_sync_watts;
	u32 pending_return_sync_flags = g_return_preview_sync_flags;
	u8 pending_return_capture_count = g_return_capture_count;
	ww_return_capture_choice pending_return_captures[WW_RETURN_CAPTURE_MAX];
	char pending_trainer[sizeof(g_pending_trainer)];
	char pending_save_path[sizeof(g_pending_save_path)];
	char pending_nds_path[sizeof(g_pending_nds_path)];

	snprintf(pending_trainer, sizeof(pending_trainer), "%s", g_pending_trainer);
	snprintf(pending_save_path, sizeof(pending_save_path), "%s", g_pending_save_path);
	snprintf(pending_nds_path, sizeof(pending_nds_path), "%s", g_pending_nds_path);
	memcpy(pending_return_captures, g_return_captures, sizeof(pending_return_captures));

	if (!json) {
		LightLock_Lock(&g_ww_async.lock);
		g_ww_async.success = false;
		g_ww_async.finished = true;
		g_ww_async.running = false;
		g_ww_async.cancel_requested = false;
		g_ww_async.remote_started = false;
		g_ww_async.timeout_ms = 0;
		g_ww_async.task = task;
		memset(&g_ww_async.snapshot, 0, sizeof(g_ww_async.snapshot));
		snprintf(g_ww_async.json, sizeof(g_ww_async.json), "out of memory");
		LightLock_Unlock(&g_ww_async.lock);
		return;
	}

	json[0] = '\0';

	switch (task) {
		case WW_TASK_STATUS:
			success = ww_api_get_status(json, WW_API_RESPONSE_MAX);
			if (!success) {
				const char *api_err = ww_api_last_error();
				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"status request failed%s%s",
						api_err ? ": " : "",
						api_err ? api_err : "");
			}
			break;
		case WW_TASK_SNAPSHOT:
			success = ww_api_get_snapshot(&snapshot, json, WW_API_RESPONSE_MAX);
			if (!success) {
				const char *api_err = ww_api_last_error();
				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"snapshot request failed%s%s",
						api_err ? ": " : "",
						api_err ? api_err : "");
			}
			break;
		case WW_TASK_EXPORT_EEPROM:
			success = ww_api_export_eeprom("WWEEPROM.bin");
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "export failed");
			break;
		case WW_TASK_IMPORT_EEPROM:
			success = ww_api_import_eeprom("PWEEPROM_IMPORT.bin");
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "import failed");
			break;
		case WW_TASK_COMMAND_SET_STEPS:
			success = ww_api_command_set_steps(pending_steps, &snapshot, json, WW_API_RESPONSE_MAX);
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "set-steps command failed");
			break;
		case WW_TASK_COMMAND_SET_WATTS:
			success = ww_api_command_set_watts(pending_watts, &snapshot, json, WW_API_RESPONSE_MAX);
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "set-watts command failed");
			break;
		case WW_TASK_COMMAND_SET_SYNC:
			success = ww_api_command_set_sync(pending_sync, &snapshot, json, WW_API_RESPONSE_MAX);
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "set-sync command failed");
			break;
		case WW_TASK_COMMAND_SET_TRAINER:
			success = ww_api_command_set_trainer(pending_trainer, &snapshot, json, WW_API_RESPONSE_MAX);
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "set-trainer command failed");
			break;
			case WW_TASK_HGSS_PATCH_MANUAL: {
				hgss_patch_report report;
				char patch_error[128];

				success = hgss_patch_file(
						pending_save_path,
						pending_hgss_steps,
						pending_hgss_watts,
						pending_hgss_course_flags,
						pending_increment_trip_counter,
						&report,
						patch_error,
						sizeof(patch_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "%s", patch_error[0] ? patch_error : "HGSS patch failed");
					break;
				}

				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Patched %s | steps %lu->%lu | watts %lu->%lu | flags 0x%08lX->0x%08lX | trip %u->%u",
						pending_save_path,
						(unsigned long)report.steps_before,
						(unsigned long)report.steps_after,
						(unsigned long)report.watts_before,
						(unsigned long)report.watts_after,
						(unsigned long)report.course_flags_before,
						(unsigned long)report.course_flags_after,
						report.trip_counter_before,
						report.trip_counter_after);
				break;
			}
			case WW_TASK_HGSS_PATCH_SYNC: {
				u32 sync_steps;
				u32 sync_watts;
				u32 sync_flags;
				hgss_patch_report report;
				char patch_error[128];

				success = ww_api_get_sync_package(json, WW_API_RESPONSE_MAX);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "sync package request failed");
					break;
				}

				if (!ww_json_get_u32_after_token(json, "\"stats\"", "steps", &sync_steps)) {
					snprintf(json, WW_API_RESPONSE_MAX, "sync package missing stats.steps");
					success = false;
					break;
				}

				if (!ww_json_get_u32_after_token(json, "\"stats\"", "watts", &sync_watts)) {
					snprintf(json, WW_API_RESPONSE_MAX, "sync package missing stats.watts");
					success = false;
					break;
				}

				if (!ww_json_get_u32_after_token(json, "\"courseUnlocks\"", "unlockFlags", &sync_flags))
					sync_flags = 0;

				success = hgss_patch_file(
						pending_save_path,
						sync_steps,
						sync_watts,
						sync_flags,
						pending_increment_trip_counter,
						&report,
						patch_error,
						sizeof(patch_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "%s", patch_error[0] ? patch_error : "HGSS sync patch failed");
					break;
				}

				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Patched %s from sync package | steps %lu | watts %lu | flags 0x%08lX | trip %u->%u",
						pending_save_path,
						(unsigned long)sync_steps,
						(unsigned long)sync_watts,
						(unsigned long)sync_flags,
						report.trip_counter_before,
						report.trip_counter_after);
				break;
			}
			case WW_TASK_HGSS_STROLL_SEND: {
				hgss_stroll_send_context send_context;
				hgss_box_slot_summary verify_source_slot;
				hgss_stroll_send_report send_report;
				u8 course_table[WW_OV112_COURSE_TABLE_SIZE];
				char seed_trainer_name[24];
				char backend_error[64];
				char backend_message[192];
				char slot_error[128];
				char patch_error[128];
				char *send_body = NULL;
				u32 eeprom_watts;
				u32 resolved_route_image_index = 0;
				u32 resolved_route_seed = 0;
				u32 sprite_name_patches_applied = 0;
				u32 sprite_name_patches_failed = 0;
				u32 sprite_image_patches_applied = 0;
				u32 sprite_image_patches_failed = 0;
				u64 rom_size = 0;
				u64 rom_mtime = 0;
				bool route_cache_hit = false;
				u8 level;

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				ww_async_progress_set(0, "Preparing send request");

				if (!pending_nds_path[0]) {
					snprintf(json, WW_API_RESPONSE_MAX, "missing HGSS .nds path for dynamic sprite workflow");
					success = false;
					break;
				}

				success = hgss_read_stroll_send_context(
						pending_save_path,
						(u8)pending_send_box,
						(u8)pending_send_slot,
						&send_context,
						slot_error,
						sizeof(slot_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "%s", slot_error[0] ? slot_error : "failed to read source slot");
					break;
				}
				if (!send_context.source_slot.occupied || send_context.source_slot.species_id == 0) {
					snprintf(json, WW_API_RESPONSE_MAX, "selected source slot is empty");
					success = false;
					break;
				}

				ww_sanitize_json_ascii(send_context.trainer_name, seed_trainer_name, sizeof(seed_trainer_name));
				if (!seed_trainer_name[0])
					snprintf(seed_trainer_name, sizeof(seed_trainer_name), "WWBRIDGE");

				level = ww_estimate_level_from_exp(send_context.source_slot.exp);

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				if (!ww_async_wait_prestart_window(task, json, WW_API_RESPONSE_MAX, "Ready to send (B to cancel)")) {
					success = false;
					break;
				}

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				ww_async_progress_set(0, "Syncing trainer with API");

				ww_async_mark_remote_started();

				if (!ww_api_patch_identity(
						seed_trainer_name,
						send_context.trainer_tid,
						send_context.trainer_sid,
						NULL,
						0)) {
					success = ww_api_command_set_trainer(seed_trainer_name, NULL, NULL, 0);
					if (!success && strcmp(seed_trainer_name, "WWBRIDGE") != 0)
						success = ww_api_command_set_trainer("WWBRIDGE", NULL, NULL, 0);
					if (!success) {
						snprintf(
								json,
								WW_API_RESPONSE_MAX,
								"failed to seed EEPROM trainer from HGSS save (trainer=\"%s\")",
								seed_trainer_name);
						break;
					}
				}

				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					free(send_body);
					send_body = NULL;
					success = false;
					break;
				}
				ww_async_progress_set(22, "Trainer synced");
				ww_async_progress_set(28, "Syncing steps with API");

				success = ww_api_command_set_steps(send_context.pokewalker_steps, NULL, NULL, 0);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to seed EEPROM steps from HGSS save");
					break;
				}
				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}
				ww_async_progress_set(36, "Steps synced");
				ww_async_progress_set(42, "Syncing watts with API");

				eeprom_watts = send_context.pokewalker_watts;
				if (eeprom_watts > 0xFFFFu)
					eeprom_watts = 0xFFFFu;

				success = ww_api_command_set_watts(eeprom_watts, NULL, NULL, 0);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to seed EEPROM watts from HGSS save");
					break;
				}
				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}
				ww_async_progress_set(50, "Watts synced");
				ww_async_progress_set(56, "Resolving route from ROM");

				if (pending_send_course >= WW_OV112_COURSE_COUNT) {
					snprintf(json, WW_API_RESPONSE_MAX, "course index out of range for ROM route table");
					success = false;
					break;
				}

				if (!ww_get_course_table_cached(pending_nds_path, course_table, &route_cache_hit)) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to load course table from HGSS ROM/cache");
					success = false;
					break;
				}

				if (!ww_get_file_identity(pending_nds_path, &rom_size, &rom_mtime)) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to read HGSS ROM metadata");
					success = false;
					break;
				}

				send_body = (char *)malloc(WW_STROLL_SEND_BODY_MAX);
				if (!send_body) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to allocate resolved send payload buffer");
					success = false;
					break;
				}

				if (!ww_build_resolved_stroll_send_json(
							&send_context,
							level,
							pending_send_course,
							pending_send_route_seed_valid,
							pending_send_route_seed,
							pending_send_clear_buffers,
							pending_send_allow_locked,
							course_table,
							rom_size,
							rom_mtime,
							&resolved_route_image_index,
							&resolved_route_seed,
							send_body,
							WW_STROLL_SEND_BODY_MAX)) {
					free(send_body);
					send_body = NULL;
					snprintf(json, WW_API_RESPONSE_MAX, "failed to build resolved stroll/send payload");
					success = false;
					break;
				}
				ww_async_progress_set(68, "Route payload ready");
				ww_async_progress_set(82, "Request sent to API");

				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				success = ww_api_stroll_send_resolved_json(send_body, json, WW_API_RESPONSE_MAX);
				free(send_body);
				send_body = NULL;
				if (!success) {
					bool has_error = ww_json_get_string_from(json, "error", backend_error, sizeof(backend_error));
					bool has_message = ww_json_get_string_from(json, "message", backend_message, sizeof(backend_message));

					if (has_error && has_message) {
						snprintf(
								json,
								WW_API_RESPONSE_MAX,
								"stroll send request failed (%s): %s",
								backend_error,
								backend_message);
					} else if (has_message) {
						snprintf(json, WW_API_RESPONSE_MAX, "stroll send request failed: %s", backend_message);
					} else if (!json[0]) {
						snprintf(json, WW_API_RESPONSE_MAX, "stroll send request failed");
					}
					break;
				}
				ww_async_progress_set(86, "Route accepted by API");
				ww_async_progress_set(88, "Patching name sprites");

				ww_apply_dynamic_name_sprite_patches(
						&send_context,
						json,
						pending_send_course,
						&sprite_name_patches_applied,
						&sprite_name_patches_failed);
				ww_async_progress_set(92, "Patching Pokemon sprites");

				ww_apply_dynamic_pokemon_sprite_patches(
						pending_nds_path,
						&send_context,
						json,
						&sprite_image_patches_applied,
						&sprite_image_patches_failed);
				ww_async_progress_set(95, "Sprites patched");
				ww_async_progress_set(97, "Updating save");

				success = hgss_apply_stroll_send(
						pending_save_path,
						(u8)pending_send_box,
						(u8)pending_send_slot,
						send_context.source_slot.species_id,
						true,
						&send_report,
						patch_error,
						sizeof(patch_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "%s", patch_error[0] ? patch_error : "failed to patch save after send");
					break;
				}
				ww_async_progress_set(98, "Save updated");
				ww_async_progress_set(99, "Verifying save update");

				success = hgss_read_box_slot_summary(
						pending_save_path,
						(u8)pending_send_box,
						(u8)pending_send_slot,
						&verify_source_slot,
						slot_error,
						sizeof(slot_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "save read-back verify failed after send: %s", slot_error[0] ? slot_error : "unable to read source slot");
					break;
				}

				if (verify_source_slot.occupied || verify_source_slot.species_id != 0) {
					snprintf(
							json,
							WW_API_RESPONSE_MAX,
							"save verify mismatch after send: source slot still occupied (species %u)",
							(unsigned)verify_source_slot.species_id);
					success = false;
					break;
				}

				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Sent species %u (Lv~%u) from box %lu slot %lu to course %lu (route image %lu, seed %lu) | EEPROM seeded (trainer=%s tid=%u sid=%u steps=%lu watts=%lu) | route cache=%s | dynamic-name patches applied=%lu failed=%lu | dynamic-image patches applied=%lu failed=%lu (ROM=%s) | save updated (%s, pair=%s)",
						(unsigned)send_context.source_slot.species_id,
						(unsigned)level,
						(unsigned long)pending_send_box,
						(unsigned long)pending_send_slot,
						(unsigned long)pending_send_course,
						(unsigned long)resolved_route_image_index,
						(unsigned long)resolved_route_seed,
						send_context.trainer_name,
						(unsigned)send_context.trainer_tid,
						(unsigned)send_context.trainer_sid,
						(unsigned long)send_context.pokewalker_steps,
						(unsigned long)eeprom_watts,
						route_cache_hit ? "hit" : "rebuilt",
						(unsigned long)sprite_name_patches_applied,
						(unsigned long)sprite_name_patches_failed,
						(unsigned long)sprite_image_patches_applied,
						(unsigned long)sprite_image_patches_failed,
						pending_nds_path,
						send_report.source_slot_cleared ? "source cleared" : "source kept",
						send_report.walker_pair_written ? "written" : "missing");
				ww_async_progress_set(100, "Send completed");
				break;
			}
			case WW_TASK_HGSS_STROLL_RETURN: {
				hgss_box_slot_summary source_slot;
				hgss_box_slot_summary verify_source_slot;
				hgss_box_slot_summary verify_target_slot;
				hgss_stroll_return_report report;
				char slot_error[128];
				char verify_error[128];
				char patch_error[128];
				char *sync_json = NULL;
				u32 exp_gain = pending_return_walked_steps;
				u32 sync_steps;
				u32 sync_watts;
				u32 sync_flags;
				u16 expected_source_species = 0;
				u16 capture_species = 0;
				u8 capture_level = 10;
				u16 capture_moves[4] = {0, 0, 0, 0};
				char capture_species_name[32];
				bool reset_stats_ok = false;
				char reset_error[96];

				capture_species_name[0] = '\0';
				reset_error[0] = '\0';

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				success = hgss_read_box_slot_summary(
						pending_save_path,
						(u8)pending_return_box,
						(u8)pending_return_source_slot,
						&source_slot,
						slot_error,
						sizeof(slot_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "%s", slot_error[0] ? slot_error : "failed to read source slot");
					break;
				}
				if (source_slot.occupied && source_slot.species_id != 0)
					expected_source_species = source_slot.species_id;

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				if (!ww_async_wait_prestart_window(task, json, WW_API_RESPONSE_MAX, "Ready to return (B to cancel)")) {
					success = false;
					break;
				}

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				ww_async_progress_set(0, "Requesting return from API");

				ww_async_mark_remote_started();

				success = ww_api_stroll_return(
						pending_return_walked_steps,
						(u16)pending_return_bonus_watts,
						(u8)pending_return_auto_captures,
						false,
						false,
						json,
						WW_API_RESPONSE_MAX);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "stroll return request failed");
					break;
				}
				ww_async_progress_set(45, "Return accepted by API");

				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				sync_json = (char *)malloc(WW_API_RESPONSE_MAX);
				if (!sync_json) {
					snprintf(json, WW_API_RESPONSE_MAX, "out of memory while reading sync package");
					success = false;
					break;
				}

				success = ww_api_get_sync_package(sync_json, WW_API_RESPONSE_MAX);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to fetch sync package after return");
					free(sync_json);
					break;
				}
				ww_async_progress_set(70, "Sync package fetched");

				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					free(sync_json);
					break;
				}

				if (!ww_json_get_u32_after_token(sync_json, "\"stats\"", "steps", &sync_steps) ||
						!ww_json_get_u32_after_token(sync_json, "\"stats\"", "watts", &sync_watts)) {
					snprintf(json, WW_API_RESPONSE_MAX, "sync package missing stats.steps/stats.watts");
					success = false;
					free(sync_json);
					break;
				}

				if (!ww_json_get_u32_after_token(sync_json, "\"courseUnlocks\"", "unlockFlags", &sync_flags))
					sync_flags = 0;

				if (!ww_json_get_u32_after_token(json, "\"returnedPokemon\"", "expGainApplied", &exp_gain) &&
						!ww_json_get_u32_after_token(json, "\"returnedPokemon\"", "expGain", &exp_gain)) {
					exp_gain = pending_return_walked_steps;
				}

				if (!ww_json_get_first_capture(
						json,
						&capture_species,
						&capture_level,
						capture_moves,
						capture_species_name,
						sizeof(capture_species_name))) {
					capture_species = 0;
					capture_level = 0;
					capture_moves[0] = 0;
					capture_moves[1] = 0;
					capture_moves[2] = 0;
					capture_moves[3] = 0;
					capture_species_name[0] = '\0';
				}

				free(sync_json);

				success = hgss_apply_stroll_return(
						pending_save_path,
						(u8)pending_return_box,
						(u8)pending_return_source_slot,
						(u8)pending_return_box,
						(u8)pending_return_target_slot,
						expected_source_species,
						exp_gain,
						pending_return_walked_steps,
						capture_species,
						capture_level,
						capture_moves,
						capture_species_name,
						sync_steps,
						sync_watts,
						sync_flags,
						pending_return_increment_trip_counter,
						&report,
						patch_error,
						sizeof(patch_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "%s", patch_error[0] ? patch_error : "failed to patch save");
					break;
				}

				success = hgss_read_box_slot_summary(
						pending_save_path,
						(u8)pending_return_box,
						(u8)pending_return_source_slot,
						&verify_source_slot,
						verify_error,
						sizeof(verify_error));
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "save read-back verify failed: %s", verify_error[0] ? verify_error : "unable to read source slot");
					break;
				}

				if (!verify_source_slot.occupied ||
						verify_source_slot.species_id != report.source_species_before ||
						verify_source_slot.exp != report.source_exp_after ||
						verify_source_slot.friendship != report.source_friendship_after) {
					snprintf(
							json,
							WW_API_RESPONSE_MAX,
							"save verify mismatch on source slot (species %u, exp %lu, friendship %u)",
							(unsigned)verify_source_slot.species_id,
							(unsigned long)verify_source_slot.exp,
							(unsigned)verify_source_slot.friendship);
					success = false;
					break;
				}

				if (report.capture_species != 0 && report.target_box > 0 && report.target_slot > 0) {
					success = hgss_read_box_slot_summary(
							pending_save_path,
							(u8)report.target_box,
							(u8)report.target_slot,
							&verify_target_slot,
							verify_error,
							sizeof(verify_error));
					if (!success) {
						snprintf(json, WW_API_RESPONSE_MAX, "capture read-back verify failed: %s", verify_error[0] ? verify_error : "unable to read target slot");
						break;
					}

					if (!verify_target_slot.occupied || verify_target_slot.species_id != report.capture_species) {
						snprintf(
								json,
								WW_API_RESPONSE_MAX,
								"save verify mismatch on capture slot (species %u, expected %u)",
								(unsigned)verify_target_slot.species_id,
								(unsigned)report.capture_species);
						success = false;
						break;
					}
				}

				/* Finalize transaction on device: consumed progress is transferred to save, then reset watch stats. */
				if (ww_api_command_set_steps(0, &snapshot, json, WW_API_RESPONSE_MAX)
						&& ww_api_command_set_watts(0, &snapshot, json, WW_API_RESPONSE_MAX)) {
					reset_stats_ok = true;
				} else {
					const char *api_err = ww_api_last_error();
					snprintf(
							reset_error,
							sizeof(reset_error),
							"device stats reset failed%s%s",
							api_err ? ": " : "",
							api_err ? api_err : "");
				}

				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Return applied to %s | source box %lu slot %lu (species %u, exp %lu->%lu, friendship %u->%u, origin %s) | capture %s | save steps %lu watts %lu flags 0x%08lX | watch reset %s%s%s",
						pending_save_path,
						(unsigned long)pending_return_box,
						(unsigned long)pending_return_source_slot,
						(unsigned)report.source_species_before,
						(unsigned long)report.source_exp_before,
						(unsigned long)report.source_exp_after,
						(unsigned)report.source_friendship_before,
						(unsigned)report.source_friendship_after,
						report.source_restored_from_pair ? "walker-pair" : "source-slot",
						report.capture_species ? "written" : (report.capture_skipped_no_space ? "skipped(box-full)" : "none"),
						(unsigned long)report.pokewalker_steps_after,
						(unsigned long)report.pokewalker_watts_after,
						(unsigned long)report.pokewalker_course_flags_after,
						reset_stats_ok ? "ok" : "failed",
						reset_error[0] ? " (" : "",
						reset_error[0] ? reset_error : "");
				if (reset_error[0]) {
					size_t used = strlen(json);
					if (used + 2 < WW_API_RESPONSE_MAX)
						strncat(json, ")", WW_API_RESPONSE_MAX - used - 1);
				}
				break;
			}
			case WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY: {
				hgss_stroll_return_report base_report;
				hgss_box_slot_summary verify_source_slot;
				char patch_error[128];
				char verify_error[128];
				char api_error[96];
				char api_detail[128];
				char capture_summary[384];
				char reset_error[96];
				char *return_json = NULL;
				char *sync_json = NULL;
				u32 api_return_species = pending_return_expected_species;
				u32 api_exp_gain = pending_return_exp_gain;
				u8 captures_written = 0;
				u8 captures_skipped = 0;
				bool reset_stats_ok = false;
				u8 i;

				capture_summary[0] = '\0';
				api_error[0] = '\0';
				api_detail[0] = '\0';
				reset_error[0] = '\0';

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					success = false;
					break;
				}

				return_json = (char *)malloc(WW_API_RESPONSE_MAX);
				sync_json = (char *)malloc(WW_API_RESPONSE_MAX);
				if (!return_json || !sync_json) {
					snprintf(json, WW_API_RESPONSE_MAX, "out of memory during guided return apply");
					if (return_json)
						free(return_json);
					if (sync_json)
						free(sync_json);
					success = false;
					break;
				}

				if (!ww_async_wait_prestart_window(task, json, WW_API_RESPONSE_MAX, "Ready to return (B to cancel)")) {
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}

				if (ww_async_should_abort_before_remote(task, json, WW_API_RESPONSE_MAX)) {
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}

				ww_async_progress_set(0, "Requesting return from API");
				ww_async_mark_remote_started();
				if (!ww_api_stroll_return(
							pending_return_walked_steps,
							(u16)pending_return_bonus_watts,
							0,
							false,
							false,
							return_json,
							WW_API_RESPONSE_MAX)) {
					ww_json_get_string_from(return_json, "error", api_error, sizeof(api_error));
					if (!ww_json_get_string_from(return_json, "detail", api_detail, sizeof(api_detail)))
						ww_json_get_string_from(return_json, "message", api_detail, sizeof(api_detail));
					if (api_error[0] || api_detail[0]) {
						snprintf(
								json,
								WW_API_RESPONSE_MAX,
								"guided return API failed: %s%s%s",
								api_error[0] ? api_error : "return_api_error",
								(api_error[0] && api_detail[0]) ? ": " : "",
								api_detail[0] ? api_detail : "request failed");
					} else {
						snprintf(json, WW_API_RESPONSE_MAX, "guided return API failed");
					}
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}
				ww_async_progress_set(35, "Return accepted by API");

				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}

				if (!ww_json_get_u32_after_token(return_json, "\"returnedPokemon\"", "speciesId", &api_return_species)
						|| api_return_species == 0
						|| api_return_species > 0xFFFFu) {
					api_return_species = pending_return_expected_species;
				}

				if (!ww_json_get_u32_after_token(return_json, "\"returnedPokemon\"", "expGainApplied", &api_exp_gain)
						&& !ww_json_get_u32_after_token(return_json, "\"returnedPokemon\"", "expGain", &api_exp_gain)) {
					api_exp_gain = pending_return_exp_gain;
				}

				ww_async_progress_set(35, "Fetching sync package");
				if (!ww_api_get_sync_package(sync_json, WW_API_RESPONSE_MAX)) {
					snprintf(json, WW_API_RESPONSE_MAX, "guided return sync fetch failed");
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}
				ww_async_progress_set(60, "Sync package fetched");

				if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}

				if (!ww_json_get_u32_after_token(sync_json, "\"stats\"", "steps", &pending_return_sync_steps)
						|| !ww_json_get_u32_after_token(sync_json, "\"stats\"", "watts", &pending_return_sync_watts)) {
					snprintf(json, WW_API_RESPONSE_MAX, "guided return sync package missing stats");
					free(return_json);
					free(sync_json);
					success = false;
					break;
				}

				if (!ww_json_get_u32_after_token(sync_json, "\"courseUnlocks\"", "unlockFlags", &pending_return_sync_flags))
					pending_return_sync_flags = 0;

				free(return_json);
				free(sync_json);

				ww_async_progress_set(70, "Applying returned Pokemon");
				success = hgss_apply_stroll_return(
						pending_save_path,
						(u8)pending_return_box,
						(u8)pending_return_source_slot,
						0,
						0,
						(u16)api_return_species,
						api_exp_gain,
						pending_return_walked_steps,
						0,
						0,
						NULL,
						NULL,
						pending_return_sync_steps,
						pending_return_sync_watts,
						pending_return_sync_flags,
						pending_return_increment_trip_counter,
						&base_report,
						patch_error,
						sizeof(patch_error));
				if (!success) {
					snprintf(
							json,
							WW_API_RESPONSE_MAX,
							"%s",
							patch_error[0] ? patch_error : "failed to apply guided return to save");
					break;
				}

				ww_async_progress_set(28, "Verifying returned Pokemon");
				success = hgss_read_box_slot_summary(
						pending_save_path,
						(u8)pending_return_box,
						(u8)pending_return_source_slot,
						&verify_source_slot,
						verify_error,
						sizeof(verify_error));
				if (!success) {
					snprintf(
							json,
							WW_API_RESPONSE_MAX,
							"guided return verify failed: %s",
							verify_error[0] ? verify_error : "unable to inspect destination slot");
					break;
				}

				if (!verify_source_slot.occupied || verify_source_slot.species_id != (u16)api_return_species) {
					snprintf(
							json,
							WW_API_RESPONSE_MAX,
							"guided return verify mismatch: destination slot has species %u (expected %u)",
							(unsigned)verify_source_slot.species_id,
							(unsigned)api_return_species);
					success = false;
					break;
				}

				for (i = 0; i < pending_return_capture_count && i < WW_RETURN_CAPTURE_MAX; i++) {
					hgss_stroll_return_report capture_report;
					ww_return_capture_choice *capture = &pending_return_captures[i];
					u32 progress = 28u + (u32)i * 20u;

					if (ww_async_has_timed_out(task, json, WW_API_RESPONSE_MAX)) {
						success = false;
						break;
					}

					if (!capture->present || capture->species_id == 0)
						continue;

					if (progress > 94u)
						progress = 94u;
					ww_async_progress_set(progress, "Placing captures");

					success = hgss_apply_stroll_return(
							pending_save_path,
							(u8)pending_return_box,
							(u8)pending_return_source_slot,
							capture->target_box,
							capture->target_slot,
							(u16)api_return_species,
							0,
							0,
							capture->species_id,
							capture->level,
							capture->moves,
							capture->species_name,
							0,
							0,
							0,
							false,
							&capture_report,
							patch_error,
							sizeof(patch_error));
					if (!success) {
						snprintf(
								json,
								WW_API_RESPONSE_MAX,
								"capture %u apply failed: %s",
								(unsigned)(i + 1),
								patch_error[0] ? patch_error : "unknown capture patch error");
						break;
					}

					if (capture_report.capture_species != 0 && capture_report.target_box > 0 && capture_report.target_slot > 0) {
						char row[96];
						size_t used = strlen(capture_summary);

						captures_written++;
						snprintf(
								row,
								sizeof(row),
								" #%u->B%ldS%ld",
								(unsigned)(i + 1),
								(long)capture_report.target_box,
								(long)capture_report.target_slot);
						if (used + strlen(row) + 1 < sizeof(capture_summary))
							strcat(capture_summary, row);
					} else if (capture_report.capture_skipped_no_space) {
						captures_skipped++;
					}
				}

				if (!success)
					break;

				/* Finalize transaction on device after save patches are done. */
				if (ww_api_command_set_steps(0, &snapshot, json, WW_API_RESPONSE_MAX)
						&& ww_api_command_set_watts(0, &snapshot, json, WW_API_RESPONSE_MAX)) {
					reset_stats_ok = true;
				} else {
					const char *api_err = ww_api_last_error();
					snprintf(
							reset_error,
							sizeof(reset_error),
							"device stats reset failed%s%s",
							api_err ? ": " : "",
							api_err ? api_err : "");
				}

				ww_async_progress_set(100, "Return completed");
				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Guided return applied to %s | returned %u to box %lu slot %lu | EXP +%lu | captures written=%u skipped=%u%s | save steps %lu watts %lu flags 0x%08lX | watch reset %s%s%s",
						pending_save_path,
						(unsigned)api_return_species,
						(unsigned long)pending_return_box,
						(unsigned long)pending_return_source_slot,
						(unsigned long)api_exp_gain,
						(unsigned)captures_written,
						(unsigned)captures_skipped,
						capture_summary,
						(unsigned long)base_report.pokewalker_steps_after,
						(unsigned long)base_report.pokewalker_watts_after,
						(unsigned long)base_report.pokewalker_course_flags_after,
						reset_stats_ok ? "ok" : "failed",
						reset_error[0] ? " (" : "",
						reset_error[0] ? reset_error : "");
				if (reset_error[0]) {
					size_t used = strlen(json);
					if (used + 2 < WW_API_RESPONSE_MAX)
						strncat(json, ")", WW_API_RESPONSE_MAX - used - 1);
				}
				break;
			}
		default:
			snprintf(json, WW_API_RESPONSE_MAX, "unknown task");
			break;
	}

	LightLock_Lock(&g_ww_async.lock);
	g_ww_async.success = success;
	g_ww_async.finished = true;
	g_ww_async.running = false;
	g_ww_async.cancel_requested = false;
	g_ww_async.remote_started = false;
	g_ww_async.timeout_ms = 0;
	g_ww_async.task = task;
	g_ww_async.snapshot = snapshot;
	snprintf(g_ww_async.json, sizeof(g_ww_async.json), "%s", json);
	LightLock_Unlock(&g_ww_async.lock);

	free(json);
}

static bool ww_async_start(ww_async_task task)
{
	Thread thread;
	s32 worker_prio;

	LightLock_Lock(&g_ww_async.lock);
	if (g_ww_async.running) {
		LightLock_Unlock(&g_ww_async.lock);
		return false;
	}

	g_ww_async.running = true;
	g_ww_async.finished = false;
	g_ww_async.success = false;
	g_ww_async.cancel_requested = false;
	g_ww_async.remote_started = false;
	g_ww_async.started_ms = osGetTime();
	g_ww_async.timeout_ms = ww_async_timeout_for_task(task);
	g_ww_async.task = task;
	g_ww_async.json[0] = '\0';
	memset(&g_ww_async.snapshot, 0, sizeof(g_ww_async.snapshot));
	LightLock_Unlock(&g_ww_async.lock);
	ww_async_progress_set(0, "Starting");

	worker_prio = g_ui_thread_prio > 0 ? g_ui_thread_prio - 1 : g_ui_thread_prio;
	thread = threadCreate(ww_async_worker, (void *)(uintptr_t)task, WW_ASYNC_STACK_SIZE, worker_prio, -2, false);
	if (!thread) {
		LightLock_Lock(&g_ww_async.lock);
		g_ww_async.running = false;
		g_ww_async.cancel_requested = false;
		g_ww_async.remote_started = false;
		g_ww_async.timeout_ms = 0;
		LightLock_Unlock(&g_ww_async.lock);
		return false;
	}

	LightLock_Lock(&g_ww_async.lock);
	g_ww_async.thread = thread;
	LightLock_Unlock(&g_ww_async.lock);

	return true;
}

static bool ww_async_poll_completion(void)
{
	Thread thread;
	ww_async_task task;
	bool success;
	char json[WW_ASYNC_LOG_MAX];
	wearwalker_snapshot snapshot;

	LightLock_Lock(&g_ww_async.lock);
	if (!g_ww_async.finished) {
		LightLock_Unlock(&g_ww_async.lock);
		return false;
	}

	thread = g_ww_async.thread;
	task = g_ww_async.task;
	success = g_ww_async.success;
	snapshot = g_ww_async.snapshot;
	snprintf(json, sizeof(json), "%s", g_ww_async.json);

	g_ww_async.thread = NULL;
	g_ww_async.finished = false;
	LightLock_Unlock(&g_ww_async.lock);

	if (thread) {
		threadJoin(thread, U64_MAX);
		threadFree(thread);
	}

	ww_async_progress_set(100, success ? "Completed" : "Failed");

	if (!success) {
		printf("WearWalker %s failed (%s)\n", ww_async_task_name(task), json[0] ? json : "unknown error");
		return true;
	}

	switch (task) {
		case WW_TASK_STATUS:
			printf("Bridge status: %.220s\n", json);
			break;
		case WW_TASK_SNAPSHOT:
		case WW_TASK_COMMAND_SET_STEPS:
		case WW_TASK_COMMAND_SET_WATTS:
		case WW_TASK_COMMAND_SET_SYNC:
		case WW_TASK_COMMAND_SET_TRAINER:
			printf("Trainer: %s\n", snapshot.trainer);
			printf("Steps: %lu\n", (unsigned long)snapshot.steps);
			printf("Watts: %lu\n", (unsigned long)snapshot.watts);
			break;
		case WW_TASK_EXPORT_EEPROM:
			printf("EEPROM exported to WWEEPROM.bin\n");
			break;
		case WW_TASK_IMPORT_EEPROM:
			printf("EEPROM imported successfully\n");
			break;
		case WW_TASK_HGSS_PATCH_MANUAL:
		case WW_TASK_HGSS_PATCH_SYNC:
		case WW_TASK_HGSS_STROLL_SEND:
		case WW_TASK_HGSS_STROLL_RETURN:
		case WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY:
			printf("%s\n", json);
			break;
		default:
			printf("WearWalker request completed\n");
			break;
	}

	return true;
}

static void ww_async_shutdown(void)
{
	Thread thread;

	LightLock_Lock(&g_ww_async.lock);
	thread = g_ww_async.thread;
	g_ww_async.thread = NULL;
	g_ww_async.running = false;
	g_ww_async.finished = false;
	g_ww_async.cancel_requested = false;
	g_ww_async.remote_started = false;
	g_ww_async.timeout_ms = 0;
	LightLock_Unlock(&g_ww_async.lock);

	if (thread) {
		threadJoin(thread, U64_MAX);
		threadFree(thread);
	}
}

