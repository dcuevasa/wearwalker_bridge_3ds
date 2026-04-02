#include "ui.h"
#include "hgss_patcher.h"
#include "hgss_storage.h"
#include "utils.h"
#include "wearwalker_api.h"

#include <3ds.h>
#include <3ds/util/decompress.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

void call_set_wearwalker_host();
void call_apply_wearwalker_endpoint();
void call_wearwalker_status();
void call_wearwalker_snapshot();
void call_wearwalker_export_eeprom();
void call_wearwalker_import_eeprom();
void call_set_wearwalker_trainer();
void call_wearwalker_command_set_steps();
void call_wearwalker_command_set_watts();
void call_wearwalker_command_set_sync();
void call_wearwalker_command_set_trainer();
void call_select_hgss_save();
void call_select_hgss_rom();
void call_show_hgss_save_path();
void call_show_hgss_rom_path();
void call_patch_hgss_manual();
void call_patch_hgss_from_sync();
void call_apply_stroll_send_endpoint();
void call_show_stroll_send_slot();
void call_stroll_send_from_save();
void call_apply_stroll_return_endpoint();
void call_show_stroll_return_slot();
void call_stroll_return_to_save();

#define WW_SAVE_PATH_MAX 512
#define WW_BROWSER_ENTRY_NAME_MAX 256
#define WW_BROWSER_MAX_ENTRIES 384
#define WW_ASYNC_LOG_MAX 1024

typedef struct {
	char name[WW_BROWSER_ENTRY_NAME_MAX];
	bool is_dir;
} ww_browser_entry;

typedef enum {
	WW_BROWSER_FILTER_SAV = 0,
	WW_BROWSER_FILTER_NDS,
} ww_browser_filter;

static char g_wearwalker_host[64] = WW_API_DEFAULT_HOST;
static char g_wearwalker_trainer[32] = "WWBRIDGE";
static u32 g_pending_steps = 1000;
static u32 g_pending_watts = 100;
static u32 g_pending_sync = 0;
static char g_pending_trainer[32] = "WWBRIDGE";
static char g_selected_hgss_save_path[WW_SAVE_PATH_MAX];
static char g_selected_hgss_nds_path[WW_SAVE_PATH_MAX];
static char g_pending_save_path[WW_SAVE_PATH_MAX];
static char g_pending_nds_path[WW_SAVE_PATH_MAX];
static u32 g_pending_hgss_steps = 2400;
static u32 g_pending_hgss_watts = 100;
static u32 g_pending_hgss_course_flags = 0;
static bool g_pending_increment_trip_counter = true;
static u32 g_pending_send_box = 1;
static u32 g_pending_send_slot = 1;
static u32 g_pending_send_course = 0;
static bool g_pending_send_allow_locked = false;
static bool g_pending_send_clear_buffers = true;
static u32 g_pending_return_box = 1;
static u32 g_pending_return_source_slot = 1;
static u32 g_pending_return_target_slot = 0;
static u32 g_pending_return_walked_steps = 1200;
static u32 g_pending_return_bonus_watts = 0;
static u32 g_pending_return_auto_captures = 1;
static bool g_pending_return_increment_trip_counter = true;
static ww_browser_entry g_browser_entries[WW_BROWSER_MAX_ENTRIES];
static u32 g_browser_entry_count;
static s32 g_browser_selected;
static u32 g_browser_first;
static char g_browser_cwd[WW_SAVE_PATH_MAX] = "sdmc:/";
static ww_browser_filter g_browser_filter = WW_BROWSER_FILTER_SAV;

typedef enum {
	WW_TASK_NONE = 0,
	WW_TASK_STATUS,
	WW_TASK_SNAPSHOT,
	WW_TASK_EXPORT_EEPROM,
	WW_TASK_IMPORT_EEPROM,
	WW_TASK_COMMAND_SET_STEPS,
	WW_TASK_COMMAND_SET_WATTS,
	WW_TASK_COMMAND_SET_SYNC,
	WW_TASK_COMMAND_SET_TRAINER,
	WW_TASK_HGSS_PATCH_MANUAL,
	WW_TASK_HGSS_PATCH_SYNC,
	WW_TASK_HGSS_STROLL_SEND,
	WW_TASK_HGSS_STROLL_RETURN,
} ww_async_task;

typedef struct {
	Thread thread;
	LightLock lock;
	bool running;
	bool finished;
	ww_async_task task;
	bool success;
	char json[WW_ASYNC_LOG_MAX];
	wearwalker_snapshot snapshot;
} ww_async_context;

static ww_async_context g_ww_async;
static s32 g_ui_thread_prio = 0x30;

#define WW_ASYNC_STACK_SIZE (24 * 1024)

#define WW_NDS_OVERLAY_ENTRY_SIZE 32u
#define WW_NDS_OV112_ID 112u
#define WW_OV112_ROUTE_IMAGE_TABLE_ADDR 0x021FF528u
#define WW_OV112_ROUTE_IMAGE_COUNT 8u
#define WW_OV112_ROUTE_IMAGE_SIZE 0x0C0u

static const char *ww_async_task_name(ww_async_task task)
{
	switch (task) {
		case WW_TASK_STATUS:
			return "bridge status";
		case WW_TASK_SNAPSHOT:
			return "snapshot";
		case WW_TASK_EXPORT_EEPROM:
			return "EEPROM export";
		case WW_TASK_IMPORT_EEPROM:
			return "EEPROM import";
		case WW_TASK_COMMAND_SET_STEPS:
			return "command set-steps";
		case WW_TASK_COMMAND_SET_WATTS:
			return "command set-watts";
		case WW_TASK_COMMAND_SET_SYNC:
			return "command set-sync";
		case WW_TASK_COMMAND_SET_TRAINER:
			return "command set-trainer";
		case WW_TASK_HGSS_PATCH_MANUAL:
			return "HGSS manual patch";
		case WW_TASK_HGSS_PATCH_SYNC:
			return "HGSS sync patch";
		case WW_TASK_HGSS_STROLL_SEND:
			return "HGSS stroll send";
		case WW_TASK_HGSS_STROLL_RETURN:
			return "HGSS stroll return";
		default:
			return "request";
	}
}

static bool ww_json_get_u32_from(const char *json, const char *key, u32 *out_value)
{
	char token[64];
	const char *cursor;
	char *endptr;
	unsigned long value;

	if (!json || !key || !out_value)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr(json, token);
	if (!cursor)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor)
		return false;
	cursor++;

	while (*cursor && isspace((unsigned char)*cursor))
		cursor++;

	value = strtoul(cursor, &endptr, 10);
	if (endptr == cursor || value > 0xFFFFFFFFUL)
		return false;

	*out_value = (u32)value;
	return true;
}

static bool ww_json_get_u32_from_range(const char *json, const char *json_end, const char *key, u32 *out_value)
{
	char token[64];
	const char *cursor;
	char *endptr;
	unsigned long value;

	if (!json || !json_end || !key || !out_value || json >= json_end)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr(json, token);
	if (!cursor || cursor >= json_end)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor || cursor >= json_end)
		return false;
	cursor++;

	while (cursor < json_end && isspace((unsigned char)*cursor))
		cursor++;
	if (cursor >= json_end)
		return false;

	value = strtoul(cursor, &endptr, 10);
	if (endptr == cursor || endptr > json_end || value > 0xFFFFFFFFUL)
		return false;

	*out_value = (u32)value;
	return true;
}

static bool ww_json_get_string_from_range(
		const char *json,
		const char *json_end,
		const char *key,
		char *out_str,
		u32 out_size)
{
	char token[64];
	const char *cursor;
	const char *start;
	const char *end;
	size_t len;

	if (!json || !json_end || !key || !out_str || out_size == 0 || json >= json_end)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr(json, token);
	if (!cursor || cursor >= json_end)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor || cursor >= json_end)
		return false;

	start = strchr(cursor, '"');
	if (!start || start >= json_end)
		return false;
	start++;

	end = strchr(start, '"');
	if (!end || end > json_end)
		return false;

	len = (size_t)(end - start);
	if (len >= out_size)
		len = out_size - 1;

	memcpy(out_str, start, len);
	out_str[len] = '\0';
	return true;
}

static bool ww_json_get_string_from(const char *json, const char *key, char *out_str, u32 out_size)
{
	if (!json)
		return false;

	return ww_json_get_string_from_range(json, json + strlen(json), key, out_str, out_size);
}

static const char *ww_json_find_object_end(const char *object_start, const char *limit)
{
	const char *cursor;
	int depth = 0;
	bool in_string = false;
	bool escaped = false;

	if (!object_start || !limit || object_start >= limit || *object_start != '{')
		return NULL;

	for (cursor = object_start; cursor < limit; cursor++) {
		char ch = *cursor;

		if (in_string) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (ch == '\\') {
				escaped = true;
				continue;
			}
			if (ch == '"')
				in_string = false;
			continue;
		}

		if (ch == '"') {
			in_string = true;
			continue;
		}

		if (ch == '{') {
			depth++;
		} else if (ch == '}') {
			depth--;
			if (depth == 0)
				return cursor;
		}
	}

	return NULL;
}

static const char *ww_json_find_array_end(const char *array_start, const char *limit)
{
	const char *cursor;
	int depth = 0;
	bool in_string = false;
	bool escaped = false;

	if (!array_start || !limit || array_start >= limit || *array_start != '[')
		return NULL;

	for (cursor = array_start; cursor < limit; cursor++) {
		char ch = *cursor;

		if (in_string) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (ch == '\\') {
				escaped = true;
				continue;
			}
			if (ch == '"')
				in_string = false;
			continue;
		}

		if (ch == '"') {
			in_string = true;
			continue;
		}

		if (ch == '[') {
			depth++;
		} else if (ch == ']') {
			depth--;
			if (depth == 0)
				return cursor;
		}
	}

	return NULL;
}

static bool ww_json_get_moves_from_range(const char *json, const char *json_end, u16 out_moves[4])
{
	const char *moves;
	const char *list_start;
	const char *list_end;
	const char *cursor;
	char *endptr;
	u32 parsed = 0;

	if (!json || !json_end || !out_moves || json >= json_end)
		return false;

	out_moves[0] = 0;
	out_moves[1] = 0;
	out_moves[2] = 0;
	out_moves[3] = 0;

	moves = strstr(json, "\"moves\"");
	if (!moves || moves >= json_end)
		return false;

	list_start = strchr(moves, '[');
	if (!list_start || list_start >= json_end)
		return false;
	list_end = ww_json_find_array_end(list_start, json_end);
	if (!list_end || list_end > json_end)
		return false;

	cursor = list_start + 1;
	while (cursor < list_end && parsed < 4) {
		unsigned long value;

		while (cursor < list_end && (isspace((unsigned char)*cursor) || *cursor == ','))
			cursor++;
		if (cursor >= list_end || *cursor == ']')
			break;

		value = strtoul(cursor, &endptr, 10);
		if (endptr == cursor || endptr > list_end)
			return false;

		if (value <= 0xFFFFUL)
			out_moves[parsed] = (u16)value;
		parsed++;
		cursor = endptr;
	}

	return parsed > 0;
}

static bool ww_json_get_inventory_capture_moves_by_slot(const char *json, u32 target_slot, u16 out_moves[4])
{
	const char *inventory;
	const char *caught;
	const char *list_start;
	const char *list_end;
	const char *cursor;

	if (!json || !out_moves)
		return false;

	inventory = strstr(json, "\"inventory\"");
	if (!inventory)
		return false;

	caught = strstr(inventory, "\"caught\"");
	if (!caught)
		return false;

	list_start = strchr(caught, '[');
	if (!list_start)
		return false;
	list_end = ww_json_find_array_end(list_start, json + strlen(json));
	if (!list_end)
		return false;

	cursor = list_start + 1;
	while (cursor < list_end) {
		const char *entry;
		const char *entry_end;
		u32 slot;

		entry = strchr(cursor, '{');
		if (!entry || entry >= list_end)
			break;

		entry_end = ww_json_find_object_end(entry, list_end + 1);
		if (!entry_end)
			break;

		if (ww_json_get_u32_from_range(entry, entry_end + 1, "slot", &slot) && slot == target_slot)
			return ww_json_get_moves_from_range(entry, entry_end + 1, out_moves);

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_u32_after_token(const char *json, const char *anchor, const char *key, u32 *out_value)
{
	const char *cursor;

	if (!json || !anchor)
		return false;

	cursor = strstr(json, anchor);
	if (!cursor)
		return false;

	return ww_json_get_u32_from(cursor, key, out_value);
}

static u8 ww_estimate_level_from_exp(u32 exp)
{
	u8 level = 1;

	while (level < 100) {
		u64 next = (u64)(level + 1) * (u64)(level + 1) * (u64)(level + 1);
		if ((u64)exp < next)
			break;
		level++;
	}

	return level;
}

static bool ww_json_get_first_capture(
		const char *json,
		u16 *out_species,
		u8 *out_level,
		u16 out_moves[4],
		char *out_species_name,
		u32 out_species_name_size)
{
	const char *captures;
	const char *applied;
	const char *list_start;
	const char *list_end;
	const char *cursor;
	const char *entry;
	const char *entry_end;
	u32 species;
	u32 level = 10;
	u32 capture_slot = 0;

	if (!json || !out_species || !out_level)
		return false;

	if (out_species_name && out_species_name_size > 0)
		out_species_name[0] = '\0';
	if (out_moves) {
		out_moves[0] = 0;
		out_moves[1] = 0;
		out_moves[2] = 0;
		out_moves[3] = 0;
	}

	captures = strstr(json, "\"captures\"");
	if (!captures)
		return false;

	applied = strstr(captures, "\"applied\"");
	if (!applied)
		return false;

	list_start = strchr(applied, '[');
	if (!list_start)
		return false;
	list_end = ww_json_find_array_end(list_start, json + strlen(json));
	if (!list_end)
		return false;

	cursor = list_start + 1;
	while (cursor < list_end && isspace((unsigned char)*cursor))
		cursor++;
	if (cursor >= list_end || *cursor == ']')
		return false;

	entry = strchr(cursor, '{');
	if (!entry || entry >= list_end)
		return false;
	entry_end = ww_json_find_object_end(entry, list_end + 1);
	if (!entry_end)
		return false;

	if (!ww_json_get_u32_from_range(entry, entry_end + 1, "speciesId", &species))
		return false;
	ww_json_get_u32_from_range(entry, entry_end + 1, "level", &level);
	ww_json_get_u32_from_range(entry, entry_end + 1, "slot", &capture_slot);

	if (out_species_name && out_species_name_size > 0)
		ww_json_get_string_from_range(entry, entry_end + 1, "speciesName", out_species_name, out_species_name_size);

	if (out_moves) {
		if (!ww_json_get_inventory_capture_moves_by_slot(json, capture_slot, out_moves))
			out_moves[0] = 33;
	}

	if (species > 0xFFFF)
		return false;
	if (level == 0 || level > 100)
		level = 10;

	*out_species = (u16)species;
	*out_level = (u8)level;
	return true;
}

static bool ww_json_get_configured_route_species_name(
		const char *json,
		u32 route_slot,
		char *out_name,
		u32 out_name_size)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !out_name || out_name_size == 0)
		return false;

	anchor = strstr(json, "\"configuredRouteSlots\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 slot_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "slot", &slot_value) && slot_value == route_slot)
			return ww_json_get_string_from_range(cursor, entry_end + 1, "speciesName", out_name, out_name_size);

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_species_id(const char *json, u32 route_slot, u16 *out_species)
{
	const char *anchor;
	const char *cursor;
	const char *limit;
	u32 species_id;

	if (!json || !out_species)
		return false;

	anchor = strstr(json, "\"configuredRouteSlots\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 slot_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "slot", &slot_value) && slot_value == route_slot) {
			if (!ww_json_get_u32_from_range(cursor, entry_end + 1, "speciesId", &species_id))
				return false;
			if (species_id == 0 || species_id > 0xFFFFu)
				return false;

			*out_species = (u16)species_id;
			return true;
		}

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_slot_u32(
		const char *json,
		u32 route_slot,
		const char *field_name,
		u32 *out_value)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !field_name || !out_value)
		return false;

	anchor = strstr(json, "\"configuredRouteSlots\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 slot_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "slot", &slot_value) && slot_value == route_slot)
			return ww_json_get_u32_from_range(cursor, entry_end + 1, field_name, out_value);

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_item_name(
		const char *json,
		u32 route_item_index,
		char *out_name,
		u32 out_name_size)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !out_name || out_name_size == 0)
		return false;

	anchor = strstr(json, "\"configuredRouteItems\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 item_index_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "routeItemIndex", &item_index_value)
				&& item_index_value == route_item_index) {
			return ww_json_get_string_from_range(cursor, entry_end + 1, "itemName", out_name, out_name_size);
		}

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_item_id(
		const char *json,
		u32 route_item_index,
		u32 *out_item_id)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !out_item_id)
		return false;

	anchor = strstr(json, "\"configuredRouteItems\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 item_index_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "routeItemIndex", &item_index_value)
				&& item_index_value == route_item_index) {
			return ww_json_get_u32_from_range(cursor, entry_end + 1, "itemId", out_item_id);
		}

		cursor = entry_end + 1;
	}

	return false;
}

static u16 ww_read_u16_le(const u8 *ptr)
{
	return (u16)ptr[0] | ((u16)ptr[1] << 8);
}

static u32 ww_read_u32_le(const u8 *ptr)
{
	return (u32)ptr[0] |
			((u32)ptr[1] << 8) |
			((u32)ptr[2] << 16) |
			((u32)ptr[3] << 24);
}

static bool ww_nds_get_file_bounds_from_fat(const u8 *fat, u32 fat_size, u32 file_id, u32 *out_start, u32 *out_end);
static bool ww_read_file_range(FILE *f, u32 offset, u8 *out_buffer, u32 size);

static bool ww_nds_find_arm9_overlay_entry(
		const u8 *overlay_table,
		u32 overlay_table_size,
		u32 overlay_id,
		u32 *out_ram_address,
		u32 *out_file_id,
		u32 *out_compression)
{
	u32 entry_count;
	u32 idx;

	if (!overlay_table || !out_ram_address || !out_file_id || !out_compression)
		return false;
	if (overlay_table_size < WW_NDS_OVERLAY_ENTRY_SIZE)
		return false;

	entry_count = overlay_table_size / WW_NDS_OVERLAY_ENTRY_SIZE;
	for (idx = 0; idx < entry_count; idx++) {
		u32 entry_offset = idx * WW_NDS_OVERLAY_ENTRY_SIZE;
		u32 entry_overlay_id = ww_read_u32_le(overlay_table + entry_offset);

		if (entry_overlay_id != overlay_id)
			continue;

		*out_ram_address = ww_read_u32_le(overlay_table + entry_offset + 4);
		*out_file_id = ww_read_u32_le(overlay_table + entry_offset + 24);
		*out_compression = ww_read_u32_le(overlay_table + entry_offset + 28);
		return true;
	}

	return false;
}

static bool ww_overlay_uncompress_backwards(u8 **in_out_data, u32 *in_out_size)
{
	u8 *buffer;
	u32 compressed_size;
	u32 footer_size;
	u32 extra_size;
	u32 output_size;
	u8 *expanded;
	u8 *end_ptr;
	u8 *end;
	u8 *start;
	u8 *dest;

	if (!in_out_data || !in_out_size || !*in_out_data)
		return false;

	buffer = *in_out_data;
	compressed_size = *in_out_size;
	if (compressed_size < 8)
		return false;

	footer_size = ww_read_u32_le(buffer + compressed_size - 8);
	extra_size = ww_read_u32_le(buffer + compressed_size - 4);
	output_size = compressed_size + extra_size;
	if (output_size <= compressed_size)
		return false;

	expanded = (u8 *)realloc(buffer, output_size);
	if (!expanded)
		return false;

	buffer = expanded;
	*in_out_data = buffer;
	end_ptr = buffer + compressed_size;
	dest = end_ptr + extra_size;
	end = end_ptr - (u8)(footer_size >> 24);
	start = end_ptr - (footer_size & 0x00FFFFFFu);

	if (start < buffer || end < buffer || end > end_ptr || start > end)
		return false;

	while (end > start) {
		u8 flags = *--end;
		int bit;

		for (bit = 0; bit < 8; bit++) {
			if ((flags & 0x80u) == 0) {
				if (end <= start)
					return false;
				*--dest = *--end;
			} else {
				int ip;
				int disp;

				if ((end - start) < 2)
					return false;

				ip = *--end;
				disp = *--end;
				disp = ((disp | (ip << 8)) & ~0xF000) + 2;
				ip += 0x20;

				while (ip >= 0) {
					dest[-1] = dest[disp];
					dest--;
					ip -= 0x10;
				}
			}

			if (end <= start)
				break;

			flags <<= 1;
		}
	}

	*in_out_data = buffer;
	*in_out_size = output_size;
	return true;
}

static bool ww_extract_route_area_sprite_from_nds(
		const char *nds_path,
		u32 route_image_index,
		u8 out_sprite[WW_OV112_ROUTE_IMAGE_SIZE])
{
	FILE *f = NULL;
	u8 header[0x58];
	u8 *fat_data = NULL;
	u8 *overlay_table = NULL;
	u8 *overlay_file = NULL;
	u32 overlay_file_size = 0;
	u32 fat_offset;
	u32 fat_size;
	u32 ovt_offset;
	u32 ovt_size;
	u32 ov112_ram_address;
	u32 ov112_file_id;
	u32 ov112_compression;
	u32 file_start;
	u32 file_end;
	u32 pointer_table_offset;
	u32 pointer_offset;
	u32 sprite_address;
	u32 sprite_offset;
	long nds_size;
	bool ok = false;

	if (!nds_path || !out_sprite)
		return false;
	if (route_image_index >= WW_OV112_ROUTE_IMAGE_COUNT)
		return false;

	f = fopen(nds_path, "rb");
	if (!f)
		goto cleanup;

	if (fseek(f, 0, SEEK_END) != 0)
		goto cleanup;
	nds_size = ftell(f);
	if (nds_size <= 0)
		goto cleanup;
	if (fseek(f, 0, SEEK_SET) != 0)
		goto cleanup;

	if (fread(header, 1, sizeof(header), f) != sizeof(header))
		goto cleanup;

	fat_offset = ww_read_u32_le(header + 0x48);
	fat_size = ww_read_u32_le(header + 0x4C);
	ovt_offset = ww_read_u32_le(header + 0x50);
	ovt_size = ww_read_u32_le(header + 0x54);

	if (fat_size == 0 || ovt_size == 0)
		goto cleanup;
	if ((u64)fat_offset + (u64)fat_size > (u64)nds_size)
		goto cleanup;
	if ((u64)ovt_offset + (u64)ovt_size > (u64)nds_size)
		goto cleanup;

	fat_data = (u8 *)malloc(fat_size);
	overlay_table = (u8 *)malloc(ovt_size);
	if (!fat_data || !overlay_table)
		goto cleanup;

	if (!ww_read_file_range(f, fat_offset, fat_data, fat_size))
		goto cleanup;
	if (!ww_read_file_range(f, ovt_offset, overlay_table, ovt_size))
		goto cleanup;

	if (!ww_nds_find_arm9_overlay_entry(
				overlay_table,
				ovt_size,
				WW_NDS_OV112_ID,
				&ov112_ram_address,
				&ov112_file_id,
				&ov112_compression)) {
		goto cleanup;
	}

	if (!ww_nds_get_file_bounds_from_fat(fat_data, fat_size, ov112_file_id, &file_start, &file_end))
		goto cleanup;
	if (file_end > (u32)nds_size)
		goto cleanup;

	overlay_file_size = file_end - file_start;
	overlay_file = (u8 *)malloc(overlay_file_size);
	if (!overlay_file)
		goto cleanup;

	if (!ww_read_file_range(f, file_start, overlay_file, overlay_file_size))
		goto cleanup;

	if (((ov112_compression >> 24) & 0x01u) != 0) {
		if (!ww_overlay_uncompress_backwards(&overlay_file, &overlay_file_size))
			goto cleanup;
	}

	if (ov112_ram_address >= WW_OV112_ROUTE_IMAGE_TABLE_ADDR)
		goto cleanup;

	pointer_table_offset = WW_OV112_ROUTE_IMAGE_TABLE_ADDR - ov112_ram_address;
	pointer_offset = pointer_table_offset + (route_image_index + 1u) * 4u;
	if (pointer_offset + 4u > overlay_file_size)
		goto cleanup;

	sprite_address = ww_read_u32_le(overlay_file + pointer_offset);
	if (sprite_address < ov112_ram_address)
		goto cleanup;

	sprite_offset = sprite_address - ov112_ram_address;
	if (sprite_offset + WW_OV112_ROUTE_IMAGE_SIZE > overlay_file_size)
		goto cleanup;

	memcpy(out_sprite, overlay_file + sprite_offset, WW_OV112_ROUTE_IMAGE_SIZE);
	ok = true;

cleanup:
	if (f)
		fclose(f);
	if (fat_data)
		free(fat_data);
	if (overlay_table)
		free(overlay_table);
	if (overlay_file)
		free(overlay_file);
	return ok;
}

static bool ww_nds_find_file_id_from_fnt(const u8 *fnt, u32 fnt_size, const char *path, u32 *out_file_id)
{
	char path_copy[128];
	char components[8][32];
	char *saveptr = NULL;
	char *token;
	u32 component_count = 0;
	u16 total_dirs;
	u16 current_dir_id = 0xF000;
	u32 part;

	if (!fnt || !path || !out_file_id || fnt_size < 8)
		return false;

	total_dirs = ww_read_u16_le(fnt + 6);
	if (total_dirs == 0)
		return false;

	snprintf(path_copy, sizeof(path_copy), "%s", path);
	token = strtok_r(path_copy, "/", &saveptr);
	while (token && component_count < 8) {
		if (token[0]) {
			snprintf(components[component_count], sizeof(components[component_count]), "%s", token);
			component_count++;
		}
		token = strtok_r(NULL, "/", &saveptr);
	}

	if (component_count == 0)
		return false;

	for (part = 0; part < component_count; part++) {
		u32 dir_index;
		u32 dir_entry_offset;
		u32 subtable_offset;
		u32 cursor;
		u16 file_id;
		bool found = false;
		bool last_component = (part + 1 == component_count);
		u32 target_len = (u32)strlen(components[part]);

		if (current_dir_id < 0xF000)
			return false;

		dir_index = (u32)(current_dir_id - 0xF000);
		if (dir_index >= total_dirs)
			return false;

		dir_entry_offset = dir_index * 8;
		if (dir_entry_offset + 8 > fnt_size)
			return false;

		subtable_offset = ww_read_u32_le(fnt + dir_entry_offset);
		file_id = ww_read_u16_le(fnt + dir_entry_offset + 4);
		cursor = subtable_offset;
		if (cursor >= fnt_size)
			return false;

		while (cursor < fnt_size) {
			u8 type_len;
			u32 name_len;
			bool is_dir;
			bool name_match;

			type_len = fnt[cursor++];
			if (type_len == 0)
				break;

			name_len = (u32)(type_len & 0x7F);
			is_dir = (type_len & 0x80u) != 0;
			if (cursor + name_len > fnt_size)
				return false;

			name_match = (target_len == name_len) && (memcmp(fnt + cursor, components[part], name_len) == 0);
			cursor += name_len;

			if (is_dir) {
				u16 child_dir_id;

				if (cursor + 2 > fnt_size)
					return false;
				child_dir_id = ww_read_u16_le(fnt + cursor);
				cursor += 2;

				if (name_match) {
					if (last_component)
						return false;
					current_dir_id = child_dir_id;
					found = true;
					break;
				}
			} else {
				if (name_match) {
					if (!last_component)
						return false;
					*out_file_id = file_id;
					return true;
				}
				file_id++;
			}
		}

		if (!found)
			return false;
	}

	return false;
}

static bool ww_nds_get_file_bounds_from_fat(const u8 *fat, u32 fat_size, u32 file_id, u32 *out_start, u32 *out_end)
{
	u32 entry_offset;
	u32 entry_count;
	u32 start;
	u32 end;

	if (!fat || !out_start || !out_end || fat_size < 8)
		return false;

	entry_count = fat_size / 8;
	if (file_id >= entry_count)
		return false;

	entry_offset = file_id * 8;
	if (entry_offset + 8 > fat_size)
		return false;

	start = ww_read_u32_le(fat + entry_offset);
	end = ww_read_u32_le(fat + entry_offset + 4);
	if (start >= end)
		return false;

	*out_start = start;
	*out_end = end;
	return true;
}

static bool ww_read_file_range(FILE *f, u32 offset, u8 *out_buffer, u32 size)
{
	if (!f || !out_buffer || size == 0)
		return false;

	if (fseek(f, (long)offset, SEEK_SET) != 0)
		return false;

	return fread(out_buffer, 1, size, f) == size;
}

static bool ww_load_narc_from_nds(const char *nds_path, const char *narc_path, u8 **out_narc_data, u32 *out_narc_size)
{
	FILE *f = NULL;
	u8 header[0x50];
	u8 *fnt_data = NULL;
	u8 *fat_data = NULL;
	u8 *narc_data = NULL;
	u32 fnt_offset;
	u32 fnt_size;
	u32 fat_offset;
	u32 fat_size;
	u32 file_id;
	u32 file_start;
	u32 file_end;
	long nds_size;
	bool ok = false;

	if (!nds_path || !narc_path || !out_narc_data || !out_narc_size)
		return false;

	*out_narc_data = NULL;
	*out_narc_size = 0;

	f = fopen(nds_path, "rb");
	if (!f)
		goto cleanup;

	if (fseek(f, 0, SEEK_END) != 0)
		goto cleanup;
	nds_size = ftell(f);
	if (nds_size <= 0)
		goto cleanup;
	if (fseek(f, 0, SEEK_SET) != 0)
		goto cleanup;

	if (fread(header, 1, sizeof(header), f) != sizeof(header))
		goto cleanup;

	fnt_offset = ww_read_u32_le(header + 0x40);
	fnt_size = ww_read_u32_le(header + 0x44);
	fat_offset = ww_read_u32_le(header + 0x48);
	fat_size = ww_read_u32_le(header + 0x4C);

	if (fnt_size == 0 || fat_size == 0)
		goto cleanup;
	if ((u64)fnt_offset + (u64)fnt_size > (u64)nds_size)
		goto cleanup;
	if ((u64)fat_offset + (u64)fat_size > (u64)nds_size)
		goto cleanup;

	fnt_data = (u8 *)malloc(fnt_size);
	fat_data = (u8 *)malloc(fat_size);
	if (!fnt_data || !fat_data)
		goto cleanup;

	if (!ww_read_file_range(f, fnt_offset, fnt_data, fnt_size))
		goto cleanup;
	if (!ww_read_file_range(f, fat_offset, fat_data, fat_size))
		goto cleanup;

	if (!ww_nds_find_file_id_from_fnt(fnt_data, fnt_size, narc_path, &file_id)) {
		/* fallback for standard HG/SS ROM order */
		file_id = 258;
	}

	if (!ww_nds_get_file_bounds_from_fat(fat_data, fat_size, file_id, &file_start, &file_end))
		goto cleanup;
	if (file_end > (u32)nds_size)
		goto cleanup;

	narc_data = (u8 *)malloc(file_end - file_start);
	if (!narc_data)
		goto cleanup;
	if (!ww_read_file_range(f, file_start, narc_data, file_end - file_start))
		goto cleanup;

	*out_narc_data = narc_data;
	*out_narc_size = file_end - file_start;
	narc_data = NULL;
	ok = true;

cleanup:
	if (f)
		fclose(f);
	if (fnt_data)
		free(fnt_data);
	if (fat_data)
		free(fat_data);
	if (narc_data)
		free(narc_data);
	return ok;
}

static bool ww_narc_get_file_by_index(const u8 *narc, u32 narc_size, u32 file_index, const u8 **out_file, u32 *out_size)
{
	u32 offset = 16;
	u16 chunk_count;
	u32 i;
	u32 btaf_offset = 0;
	u32 gmif_offset = 0;
	u32 btaf_size;
	u16 num_files;
	u32 entry_offset;
	u32 data_start;
	u32 data_end;
	u32 gmif_data_start;
	u32 gmif_size;
	u32 gmif_data_size;

	if (!narc || !out_file || !out_size || narc_size < 16)
		return false;
	if (memcmp(narc, "NARC", 4) != 0)
		return false;

	chunk_count = ww_read_u16_le(narc + 0x0E);
	for (i = 0; i < chunk_count; i++) {
		u32 chunk_size;

		if (offset + 8 > narc_size)
			return false;

		chunk_size = ww_read_u32_le(narc + offset + 4);
		if (chunk_size < 8 || offset + chunk_size > narc_size)
			return false;

		if (memcmp(narc + offset, "BTAF", 4) == 0)
			btaf_offset = offset;
		else if (memcmp(narc + offset, "GMIF", 4) == 0)
			gmif_offset = offset;

		offset += chunk_size;
	}

	if (!btaf_offset || !gmif_offset)
		return false;

	btaf_size = ww_read_u32_le(narc + btaf_offset + 4);
	if (btaf_offset + btaf_size > narc_size || btaf_size < 12)
		return false;

	num_files = ww_read_u16_le(narc + btaf_offset + 8);
	if (file_index >= num_files)
		return false;

	entry_offset = btaf_offset + 12 + file_index * 8;
	if (entry_offset + 8 > btaf_offset + btaf_size)
		return false;

	data_start = ww_read_u32_le(narc + entry_offset);
	data_end = ww_read_u32_le(narc + entry_offset + 4);
	if (data_start > data_end)
		return false;

	gmif_size = ww_read_u32_le(narc + gmif_offset + 4);
	if (gmif_size < 8 || gmif_offset + gmif_size > narc_size)
		return false;

	gmif_data_start = gmif_offset + 8;
	gmif_data_size = gmif_size - 8;
	if (data_end > gmif_data_size)
		return false;

	*out_file = narc + gmif_data_start + data_start;
	*out_size = data_end - data_start;
	return true;
}

static bool ww_decompress_lz10(const u8 *compressed, u32 compressed_size, u8 **out_data, u32 *out_size)
{
	u32 decompressed_size;
	u8 *output;

	if (!compressed || !out_data || !out_size || compressed_size < 4)
		return false;
	if (compressed[0] != 0x10)
		return false;

	decompressed_size = (u32)compressed[1] |
			((u32)compressed[2] << 8) |
			((u32)compressed[3] << 16);
	if (decompressed_size == 0)
		return false;

	output = (u8 *)malloc(decompressed_size);
	if (!output)
		return false;

	if (!decompress(output, decompressed_size, NULL, (void *)compressed, compressed_size)) {
		free(output);
		return false;
	}

	*out_data = output;
	*out_size = decompressed_size;
	return true;
}

static void ww_remap_sprite_2bpp_contrast(u8 *sprite_data, u32 size)
{
	/* Preserve native HGSS 2bpp shades to avoid over-thick black lines on Pokewalker LCD. */
	static const u8 shade_map[4] = {0, 1, 2, 3};
	u32 i;

	if (!sprite_data || size == 0)
		return;

	for (i = 0; i < size; i++) {
		u8 value = sprite_data[i];
		u8 p0 = shade_map[value & 0x03u];
		u8 p1 = shade_map[(value >> 2) & 0x03u];
		u8 p2 = shade_map[(value >> 4) & 0x03u];
		u8 p3 = shade_map[(value >> 6) & 0x03u];

		sprite_data[i] = (u8)(p0 | (p1 << 2) | (p2 << 4) | (p3 << 6));
	}
}

static bool ww_extract_species_large_frames(
		const u8 *narc_data,
		u32 narc_size,
		u16 species_id,
		u8 out_large0[0x300],
		u8 out_large1[0x300])
{
	u8 *decompressed = NULL;
	u32 sprite_index;
	const u8 *compressed;
	u32 compressed_size;
	u32 decompressed_size;
	bool ok = false;

	if (!narc_data || narc_size == 0 || !out_large0 || !out_large1 || species_id == 0)
		return false;

	sprite_index = (u32)species_id - 1;
	if (!ww_narc_get_file_by_index(narc_data, narc_size, sprite_index, &compressed, &compressed_size))
		goto cleanup;

	if (!ww_decompress_lz10(compressed, compressed_size, &decompressed, &decompressed_size))
		goto cleanup;

	if (decompressed_size < 0x600)
		goto cleanup;

	memcpy(out_large0, decompressed, 0x300);
	memcpy(out_large1, decompressed + 0x300, 0x300);
	ok = true;

cleanup:
	if (decompressed)
		free(decompressed);
	return ok;
}

static bool ww_extract_species_small_frames(
		const u8 *narc_data,
		u32 narc_size,
		u16 species_id,
		u8 out_small0[0x0C0],
		u8 out_small1[0x0C0])
{
	u8 *decompressed = NULL;
	u32 sprite_index;
	const u8 *compressed;
	u32 compressed_size;
	u32 decompressed_size;
	bool ok = false;

	if (!narc_data || narc_size == 0 || !out_small0 || !out_small1 || species_id == 0)
		return false;

	sprite_index = (u32)species_id;
	if (!ww_narc_get_file_by_index(narc_data, narc_size, sprite_index, &compressed, &compressed_size))
		goto cleanup;

	if (!ww_decompress_lz10(compressed, compressed_size, &decompressed, &decompressed_size))
		goto cleanup;

	if (decompressed_size < 0x180)
		goto cleanup;

	memcpy(out_small0, decompressed, 0x0C0);
	memcpy(out_small1, decompressed + 0x0C0, 0x0C0);
	ok = true;

cleanup:
	if (decompressed)
		free(decompressed);
	return ok;
}

static void ww_patch_sprite_block_counted(const char *key, const u8 *data, u32 size, u32 *applied, u32 *failed)
{
	if (ww_api_stroll_patch_sprite_block(key, data, size, NULL, 0)) {
		if (applied)
			(*applied)++;
	} else {
		if (failed)
			(*failed)++;
	}
}

static void ww_patch_species_image_sprites(
		const u8 *small_narc_data,
		u32 small_narc_size,
		const u8 *large_narc_data,
		u32 large_narc_size,
		u16 species_id,
		const char *small0_key,
		const char *small1_key,
		const char *large0_key,
		const char *large1_key,
		u32 *applied,
		u32 *failed)
{
	u8 large0[0x300];
	u8 large1[0x300];
	u8 small0[0x0C0];
	u8 small1[0x0C0];

	if (small0_key && small1_key) {
		if (!ww_extract_species_small_frames(small_narc_data, small_narc_size, species_id, small0, small1)) {
			if (failed)
				*failed += 2;
		} else {
			ww_remap_sprite_2bpp_contrast(small0, sizeof(small0));
			ww_remap_sprite_2bpp_contrast(small1, sizeof(small1));
			ww_patch_sprite_block_counted(small0_key, small0, sizeof(small0), applied, failed);
			ww_patch_sprite_block_counted(small1_key, small1, sizeof(small1), applied, failed);
		}
	}

	if (large0_key && large1_key) {
		if (!ww_extract_species_large_frames(large_narc_data, large_narc_size, species_id, large0, large1)) {
			if (failed)
				*failed += 2;
		} else {
			ww_remap_sprite_2bpp_contrast(large0, sizeof(large0));
			ww_remap_sprite_2bpp_contrast(large1, sizeof(large1));
			ww_patch_sprite_block_counted(large0_key, large0, sizeof(large0), applied, failed);
			ww_patch_sprite_block_counted(large1_key, large1, sizeof(large1), applied, failed);
		}
	}
}

static void ww_apply_dynamic_pokemon_sprite_patches(
		const char *nds_path,
		const hgss_stroll_send_context *send_context,
		const char *send_json,
		u32 *out_applied,
		u32 *out_failed)
{
	const char *small_narc_path = "a/2/4/8";
	const char *large_narc_path = "a/2/5/6";
	u8 *small_narc_data = NULL;
	u8 *large_narc_data = NULL;
	u8 area_sprite[WW_OV112_ROUTE_IMAGE_SIZE];
	u32 small_narc_size = 0;
	u32 large_narc_size = 0;
	u32 applied = 0;
	u32 failed = 0;
	u32 slot;
	u32 route_image_index;
	u16 join_species = 0;
	u32 join_best_chance = 0;
	bool join_species_found = false;
	bool join_has_chance = false;
	bool route_image_index_found = false;

	if (!nds_path || !nds_path[0] || !send_context || !send_json) {
		if (out_applied)
			*out_applied = 0;
		if (out_failed)
			*out_failed = 0;
		return;
	}

	route_image_index_found = ww_json_get_u32_from(send_json, "selectedRouteImageIndex", &route_image_index)
			|| ww_json_get_u32_from(send_json, "routeImageIndex", &route_image_index);

	if (route_image_index_found
			&& ww_extract_route_area_sprite_from_nds(nds_path, route_image_index, area_sprite)) {
		ww_patch_sprite_block_counted("areaSprite", area_sprite, sizeof(area_sprite), &applied, &failed);
	} else {
		failed++;
	}

	ww_load_narc_from_nds(nds_path, small_narc_path, &small_narc_data, &small_narc_size);
	ww_load_narc_from_nds(nds_path, large_narc_path, &large_narc_data, &large_narc_size);

	if (!small_narc_data && !large_narc_data) {
		if (out_applied)
			*out_applied = applied;
		if (out_failed)
			*out_failed = failed + 12;
		return;
	}

	ww_patch_species_image_sprites(
			small_narc_data,
			small_narc_size,
			large_narc_data,
			large_narc_size,
			send_context->source_slot.species_id,
			"walkPokeSmall0",
			"walkPokeSmall1",
			"walkPokeLarge0",
			"walkPokeLarge1",
			&applied,
			&failed);

	for (slot = 0; slot < 3; slot++) {
		u16 route_species;
		u32 slot_chance;
		char small0_key[24];
		char small1_key[24];

		if (!ww_json_get_configured_route_species_id(send_json, slot, &route_species)) {
			failed += 2;
			continue;
		}

		snprintf(small0_key, sizeof(small0_key), "routePoke%luSmall0", (unsigned long)slot);
		snprintf(small1_key, sizeof(small1_key), "routePoke%luSmall1", (unsigned long)slot);

		if (!join_species_found) {
			join_species = route_species;
			join_species_found = true;
		}

		if (ww_json_get_configured_route_slot_u32(send_json, slot, "chance", &slot_chance)) {
			if (!join_has_chance || slot_chance > join_best_chance) {
				join_species = route_species;
				join_best_chance = slot_chance;
				join_has_chance = true;
			}
		}

		ww_patch_species_image_sprites(
				small_narc_data,
				small_narc_size,
				NULL,
				0,
				route_species,
				small0_key,
				small1_key,
				NULL,
				NULL,
				&applied,
				&failed);
	}

	if (join_species_found) {
		ww_patch_species_image_sprites(
				NULL,
				0,
				large_narc_data,
				large_narc_size,
				join_species,
				NULL,
				NULL,
				"joinPokeLarge0",
				"joinPokeLarge1",
				&applied,
				&failed);
	}

	if (out_applied)
		*out_applied = applied;
	if (out_failed)
		*out_failed = failed;

	if (small_narc_data)
		free(small_narc_data);
	if (large_narc_data)
		free(large_narc_data);
}

static bool ww_patch_text_sprite(const char *key, u8 width, u32 size, const char *text, bool centered)
{
	u8 sprite[0x180];
	const char *value = (text && text[0]) ? text : "?";

	if (size > sizeof(sprite))
		return false;

	memset(sprite, 0, sizeof(sprite));
	string_to_img(sprite, width, value, centered);
	return ww_api_stroll_patch_sprite_block(key, sprite, size, NULL, 0);
}

static bool ww_patch_text_sprite_80x16(const char *key, const char *text)
{
	return ww_patch_text_sprite(key, 80, 0x140, text, true);
}

static bool ww_patch_text_sprite_96x16(const char *key, const char *text)
{
	return ww_patch_text_sprite(key, 96, 0x180, text, false);
}

static void ww_apply_dynamic_name_sprite_patches(
		const hgss_stroll_send_context *send_context,
		const char *send_json,
		u32 course_id,
		u32 *out_applied,
		u32 *out_failed)
{
	char area_name[32];
	char route_name[32];
	char item_name[48];
	u32 slot;
	u32 applied = 0;
	u32 failed = 0;

	if (!send_context || !send_json) {
		if (out_applied)
			*out_applied = 0;
		if (out_failed)
			*out_failed = 0;
		return;
	}

	if (ww_patch_text_sprite_80x16("walkPokeName", send_context->nickname))
		applied++;
	else
		failed++;

	if (ww_patch_text_sprite_80x16("trainerCardName", send_context->trainer_name))
		applied++;
	else
		failed++;

	if (!ww_json_get_string_from(send_json, "selectedCourseName", area_name, sizeof(area_name)))
		snprintf(area_name, sizeof(area_name), "COURSE %lu", (unsigned long)course_id);

	if (ww_patch_text_sprite_80x16("areaNameSprite", area_name))
		applied++;
	else
		failed++;

	for (slot = 0; slot < 3; slot++) {
		const char *patch_key = slot == 0 ? "routePoke0Name" : (slot == 1 ? "routePoke1Name" : "routePoke2Name");

		if (!ww_json_get_configured_route_species_name(send_json, slot, route_name, sizeof(route_name)))
			snprintf(route_name, sizeof(route_name), "POKE %lu", (unsigned long)(slot + 1));

		if (ww_patch_text_sprite_80x16(patch_key, route_name))
			applied++;
		else
			failed++;
	}

	for (slot = 0; slot < 10; slot++) {
		char patch_key[24];
		u32 item_id;

		snprintf(patch_key, sizeof(patch_key), "routeItem%luName", (unsigned long)slot);

		if (!ww_json_get_configured_route_item_name(send_json, slot, item_name, sizeof(item_name))) {
			if (ww_json_get_configured_route_item_id(send_json, slot, &item_id))
				snprintf(item_name, sizeof(item_name), "ITEM %lu", (unsigned long)item_id);
			else
				snprintf(item_name, sizeof(item_name), "ITEM %lu", (unsigned long)(slot + 1));
		}

		if (ww_patch_text_sprite_96x16(patch_key, item_name))
			applied++;
		else
			failed++;
	}

	if (out_applied)
		*out_applied = applied;
	if (out_failed)
		*out_failed = failed;
}

static bool ww_path_is_sd_root(const char *path)
{
	return path && (strcmp(path, "sdmc:/") == 0 || strcmp(path, "sdmc:") == 0);
}

static bool ww_name_has_extension(const char *name, const char *extension)
{
	const char *dot;

	if (!name || !extension)
		return false;

	dot = strrchr(name, '.');
	if (!dot)
		return false;

	return strcasecmp(dot, extension) == 0;
}

static bool ww_name_has_sav_extension(const char *name)
{
	return ww_name_has_extension(name, ".sav");
}

static bool ww_name_has_nds_extension(const char *name)
{
	return ww_name_has_extension(name, ".nds");
}

static bool ww_browser_accept_file(const char *name)
{
	if (g_browser_filter == WW_BROWSER_FILTER_NDS)
		return ww_name_has_nds_extension(name);

	return ww_name_has_sav_extension(name);
}

static void ww_path_join(const char *directory, const char *name, char *out, size_t out_size)
{
	const char *entry_name = name ? name : "";
	size_t len;

	if (!out || out_size == 0)
		return;

	if (!directory || !directory[0] || ww_path_is_sd_root(directory))
		snprintf(out, out_size, "sdmc:/%s", entry_name);
	else {
		snprintf(out, out_size, "%s", directory);
		len = strlen(out);
		if (len + 1 < out_size) {
			out[len] = '/';
			out[len + 1] = '\0';
		}
		strncat(out, entry_name, out_size - strlen(out) - 1);
	}
}

static void ww_path_parent(char *path, size_t path_size)
{
	char *last_slash;
	size_t len;

	if (!path || path_size == 0)
		return;

	if (ww_path_is_sd_root(path)) {
		snprintf(path, path_size, "sdmc:/");
		return;
	}

	len = strlen(path);
	while (len > 6 && path[len - 1] == '/') {
		path[len - 1] = '\0';
		len--;
	}

	last_slash = strrchr(path, '/');
	if (!last_slash || (size_t)(last_slash - path) <= 5) {
		snprintf(path, path_size, "sdmc:/");
		return;
	}

	*last_slash = '\0';
}

static void ww_selected_directory_from_path(const char *selected_path, char *out, size_t out_size)
{
	char *last_slash;

	if (!out || out_size == 0)
		return;

	if (!selected_path || !selected_path[0]) {
		snprintf(out, out_size, "sdmc:/");
		return;
	}

	snprintf(out, out_size, "%s", selected_path);
	last_slash = strrchr(out, '/');
	if (!last_slash || (size_t)(last_slash - out) <= 5)
		snprintf(out, out_size, "sdmc:/");
	else
		*last_slash = '\0';
}

static void ww_selected_save_directory(char *out, size_t out_size)
{
	ww_selected_directory_from_path(g_selected_hgss_save_path, out, out_size);
}

static void ww_selected_rom_directory(char *out, size_t out_size)
{
	ww_selected_directory_from_path(g_selected_hgss_nds_path, out, out_size);
}

static int ww_browser_entry_cmp(const void *left, const void *right)
{
	const ww_browser_entry *a = (const ww_browser_entry *)left;
	const ww_browser_entry *b = (const ww_browser_entry *)right;

	if (a->is_dir != b->is_dir)
		return a->is_dir ? -1 : 1;

	return strcasecmp(a->name, b->name);
}

static bool ww_browser_reload(void)
{
	DIR *directory;
	struct dirent *entry;

	g_browser_entry_count = 0;
	directory = opendir(g_browser_cwd);
	if (!directory)
		return false;

	while ((entry = readdir(directory)) != NULL) {
		struct stat info;
		char full_path[WW_SAVE_PATH_MAX];
		bool is_dir;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		ww_path_join(g_browser_cwd, entry->d_name, full_path, sizeof(full_path));
		if (stat(full_path, &info) != 0)
			continue;

		is_dir = S_ISDIR(info.st_mode);
		if (!is_dir && !ww_browser_accept_file(entry->d_name))
			continue;

		if (g_browser_entry_count >= WW_BROWSER_MAX_ENTRIES)
			break;

		snprintf(g_browser_entries[g_browser_entry_count].name,
				sizeof(g_browser_entries[g_browser_entry_count].name),
				"%s", entry->d_name);
		g_browser_entries[g_browser_entry_count].is_dir = is_dir;
		g_browser_entry_count++;
	}

	closedir(directory);

	if (g_browser_entry_count > 1)
		qsort(g_browser_entries, g_browser_entry_count, sizeof(g_browser_entries[0]), ww_browser_entry_cmp);

	if (g_browser_entry_count == 0)
		g_browser_selected = 0;
	else if (g_browser_selected >= (s32)g_browser_entry_count)
		g_browser_selected = (s32)g_browser_entry_count - 1;

	if (g_browser_selected < 0)
		g_browser_selected = 0;

	g_browser_first = 0;
	return true;
}

static void ww_browser_open_for_filter(ww_browser_filter filter)
{
	char start_dir[WW_SAVE_PATH_MAX];

	g_browser_filter = filter;
	if (filter == WW_BROWSER_FILTER_NDS) {
		ww_selected_rom_directory(start_dir, sizeof(start_dir));
		if (ww_path_is_sd_root(start_dir))
			ww_selected_save_directory(start_dir, sizeof(start_dir));
	} else {
		ww_selected_save_directory(start_dir, sizeof(start_dir));
	}

	snprintf(g_browser_cwd, sizeof(g_browser_cwd), "%s", start_dir);
	g_browser_selected = 0;
	g_browser_first = 0;

	if (!ww_browser_reload()) {
		snprintf(g_browser_cwd, sizeof(g_browser_cwd), "sdmc:/");
		if (!ww_browser_reload())
			printf("Failed to open SD browser\n");
	}
}

static void ww_browser_move_selection(s32 offset)
{
	s32 selected;

	if (g_browser_entry_count == 0) {
		g_browser_selected = 0;
		return;
	}

	selected = g_browser_selected + offset;
	if (selected < 0)
		selected = 0;
	if (selected >= (s32)g_browser_entry_count)
		selected = (s32)g_browser_entry_count - 1;

	g_browser_selected = selected;
}

static void ww_browser_enter_parent(void)
{
	if (ww_path_is_sd_root(g_browser_cwd))
		return;

	ww_path_parent(g_browser_cwd, sizeof(g_browser_cwd));
	g_browser_selected = 0;
	g_browser_first = 0;
	ww_browser_reload();
}

static bool ww_browser_activate_selected(void)
{
	char path[WW_SAVE_PATH_MAX];
	ww_browser_entry *entry;

	if (g_browser_entry_count == 0 || g_browser_selected < 0 || g_browser_selected >= (s32)g_browser_entry_count)
		return false;

	entry = &g_browser_entries[g_browser_selected];
	ww_path_join(g_browser_cwd, entry->name, path, sizeof(path));

	if (entry->is_dir) {
		snprintf(g_browser_cwd, sizeof(g_browser_cwd), "%s", path);
		g_browser_selected = 0;
		g_browser_first = 0;
		return ww_browser_reload();
	}

	if (g_browser_filter == WW_BROWSER_FILTER_NDS)
		snprintf(g_selected_hgss_nds_path, sizeof(g_selected_hgss_nds_path), "%s", path);
	else
		snprintf(g_selected_hgss_save_path, sizeof(g_selected_hgss_save_path), "%s", path);
	return true;
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
	bool pending_send_allow_locked = g_pending_send_allow_locked;
	bool pending_send_clear_buffers = g_pending_send_clear_buffers;
	u32 pending_return_box = g_pending_return_box;
	u32 pending_return_source_slot = g_pending_return_source_slot;
	u32 pending_return_target_slot = g_pending_return_target_slot;
	u32 pending_return_walked_steps = g_pending_return_walked_steps;
	u32 pending_return_bonus_watts = g_pending_return_bonus_watts;
	u32 pending_return_auto_captures = g_pending_return_auto_captures;
	bool pending_return_increment_trip_counter = g_pending_return_increment_trip_counter;
	char pending_trainer[sizeof(g_pending_trainer)];
	char pending_save_path[sizeof(g_pending_save_path)];
	char pending_nds_path[sizeof(g_pending_nds_path)];

	snprintf(pending_trainer, sizeof(pending_trainer), "%s", g_pending_trainer);
	snprintf(pending_save_path, sizeof(pending_save_path), "%s", g_pending_save_path);
	snprintf(pending_nds_path, sizeof(pending_nds_path), "%s", g_pending_nds_path);

	if (!json) {
		LightLock_Lock(&g_ww_async.lock);
		g_ww_async.success = false;
		g_ww_async.finished = true;
		g_ww_async.running = false;
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
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "status request failed");
			break;
		case WW_TASK_SNAPSHOT:
			success = ww_api_get_snapshot(&snapshot, json, WW_API_RESPONSE_MAX);
			if (!success)
				snprintf(json, WW_API_RESPONSE_MAX, "snapshot request failed");
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
				char slot_error[128];
				char patch_error[128];
				u32 eeprom_watts;
				u32 sprite_name_patches_applied = 0;
				u32 sprite_name_patches_failed = 0;
				u32 sprite_image_patches_applied = 0;
				u32 sprite_image_patches_failed = 0;
				u8 level;

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

				level = ww_estimate_level_from_exp(send_context.source_slot.exp);

				if (!ww_api_patch_identity(
						send_context.trainer_name,
						send_context.trainer_tid,
						send_context.trainer_sid,
						NULL,
						0)) {
					success = ww_api_command_set_trainer(send_context.trainer_name, NULL, NULL, 0);
					if (!success) {
						snprintf(json, WW_API_RESPONSE_MAX, "failed to seed EEPROM trainer from HGSS save");
						break;
					}
				}

				success = ww_api_command_set_steps(send_context.pokewalker_steps, NULL, NULL, 0);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to seed EEPROM steps from HGSS save");
					break;
				}

				eeprom_watts = send_context.pokewalker_watts;
				if (eeprom_watts > 0xFFFFu)
					eeprom_watts = 0xFFFFu;

				success = ww_api_command_set_watts(eeprom_watts, NULL, NULL, 0);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to seed EEPROM watts from HGSS save");
					break;
				}

				success = ww_api_stroll_send(
						send_context.source_slot.species_id,
						level,
						(u8)pending_send_course,
						pending_send_clear_buffers,
						pending_send_allow_locked,
						send_context.nickname,
						send_context.source_slot.friendship,
						send_context.held_item,
						send_context.moves,
						send_context.variant_flags,
						send_context.special_flags,
						json,
						WW_API_RESPONSE_MAX);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "stroll send request failed");
					break;
				}

				ww_apply_dynamic_name_sprite_patches(
						&send_context,
						json,
						pending_send_course,
						&sprite_name_patches_applied,
						&sprite_name_patches_failed);

				ww_apply_dynamic_pokemon_sprite_patches(
						pending_nds_path,
						&send_context,
						json,
						&sprite_image_patches_applied,
						&sprite_image_patches_failed);

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
						"Sent species %u (Lv~%u) from box %lu slot %lu to route %lu | EEPROM seeded (trainer=%s tid=%u sid=%u steps=%lu watts=%lu) | dynamic-name patches applied=%lu failed=%lu | dynamic-image patches applied=%lu failed=%lu (ROM=%s) | save updated (%s, pair=%s)",
						(unsigned)send_context.source_slot.species_id,
						(unsigned)level,
						(unsigned long)pending_send_box,
						(unsigned long)pending_send_slot,
						(unsigned long)pending_send_course,
						send_context.trainer_name,
						(unsigned)send_context.trainer_tid,
						(unsigned)send_context.trainer_sid,
						(unsigned long)send_context.pokewalker_steps,
						(unsigned long)eeprom_watts,
						(unsigned long)sprite_name_patches_applied,
						(unsigned long)sprite_name_patches_failed,
						(unsigned long)sprite_image_patches_applied,
						(unsigned long)sprite_image_patches_failed,
						pending_nds_path,
						send_report.source_slot_cleared ? "source cleared" : "source kept",
						send_report.walker_pair_written ? "written" : "missing");
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

				capture_species_name[0] = '\0';

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

				if (report.capture_species != 0 && report.target_slot > 0) {
					success = hgss_read_box_slot_summary(
							pending_save_path,
							(u8)pending_return_box,
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

				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Return applied to %s | source box %lu slot %lu (species %u, exp %lu->%lu, friendship %u->%u, origin %s) | capture %s | steps %lu watts %lu flags 0x%08lX",
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
						(unsigned long)report.pokewalker_course_flags_after);
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
	g_ww_async.task = task;
	g_ww_async.json[0] = '\0';
	memset(&g_ww_async.snapshot, 0, sizeof(g_ww_async.snapshot));
	LightLock_Unlock(&g_ww_async.lock);

	worker_prio = g_ui_thread_prio > 0 ? g_ui_thread_prio - 1 : g_ui_thread_prio;
	thread = threadCreate(ww_async_worker, (void *)(uintptr_t)task, WW_ASYNC_STACK_SIZE, worker_prio, -2, false);
	if (!thread) {
		LightLock_Lock(&g_ww_async.lock);
		g_ww_async.running = false;
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
	LightLock_Unlock(&g_ww_async.lock);

	if (thread) {
		threadJoin(thread, U64_MAX);
		threadFree(thread);
	}
}

enum {
	WW_MENU_SET_HOST = 0,
	WW_MENU_PORT,
	WW_MENU_APPLY_ENDPOINT,
	WW_MENU_GET_STATUS,
	WW_MENU_GET_SNAPSHOT,
	WW_MENU_EXPORT_EEPROM,
	WW_MENU_IMPORT_EEPROM,
	WW_MENU_SET_TRAINER_TEXT,
	WW_MENU_CMD_STEPS_VALUE,
	WW_MENU_CMD_STEPS_SEND,
	WW_MENU_CMD_WATTS_VALUE,
	WW_MENU_CMD_WATTS_SEND,
	WW_MENU_CMD_SYNC_VALUE,
	WW_MENU_CMD_SYNC_SEND,
	WW_MENU_CMD_TRAINER_SEND,
};

enum {
	HGSS_MENU_SELECT_SAVE = 0,
	HGSS_MENU_SHOW_SAVE,
	HGSS_MENU_MANUAL_STEPS,
	HGSS_MENU_MANUAL_WATTS,
	HGSS_MENU_MANUAL_FLAGS,
	HGSS_MENU_TRIP_COUNTER,
	HGSS_MENU_PATCH_MANUAL,
	HGSS_MENU_PATCH_SYNC,
};

enum {
	SEND_MENU_SET_HOST = 0,
	SEND_MENU_PORT,
	SEND_MENU_APPLY_ENDPOINT,
	SEND_MENU_SELECT_SAVE,
	SEND_MENU_SHOW_SAVE,
	SEND_MENU_SELECT_ROM,
	SEND_MENU_SHOW_ROM,
	SEND_MENU_BOX,
	SEND_MENU_SLOT,
	SEND_MENU_SHOW_SLOT,
	SEND_MENU_ROUTE,
	SEND_MENU_ALLOW_LOCKED,
	SEND_MENU_CLEAR_BUFFERS,
	SEND_MENU_SEND,
};

enum {
	RETURN_MENU_SET_HOST = 0,
	RETURN_MENU_PORT,
	RETURN_MENU_APPLY_ENDPOINT,
	RETURN_MENU_SELECT_SAVE,
	RETURN_MENU_SHOW_SAVE,
	RETURN_MENU_BOX,
	RETURN_MENU_SOURCE_SLOT,
	RETURN_MENU_SHOW_SLOT,
	RETURN_MENU_TARGET_SLOT,
	RETURN_MENU_WALKED_STEPS,
	RETURN_MENU_BONUS_WATTS,
	RETURN_MENU_AUTO_CAPTURES,
	RETURN_MENU_INCREMENT_TRIP,
	RETURN_MENU_APPLY,
};

// WearWalker WiFi menu
menu_entry wearwalker_wifi_menu_entries[] = {
	{"Set backend host", ENTRY_ACTION, .callback = call_set_wearwalker_host},
	{"Backend port", ENTRY_NUMATTR, .num_attr = {.value = WW_API_DEFAULT_PORT, .min = 1, .max = 65535}},
	{"Apply endpoint", ENTRY_ACTION, .callback = call_apply_wearwalker_endpoint},
	{"Get bridge status", ENTRY_ACTION, .callback = call_wearwalker_status},
	{"Get snapshot", ENTRY_ACTION, .callback = call_wearwalker_snapshot},
	{"Export EEPROM -> WWEEPROM.bin", ENTRY_ACTION, .callback = call_wearwalker_export_eeprom},
	{"Import EEPROM <- PWEEPROM_IMPORT.bin", ENTRY_ACTION, .callback = call_wearwalker_import_eeprom},
	{"Trainer command text", ENTRY_ACTION, .callback = call_set_wearwalker_trainer},
	{"Set-steps value", ENTRY_NUMATTR, .num_attr = {.value = 1000, .min = 0, .max = 9999999}},
	{"Send command: set-steps", ENTRY_ACTION, .callback = call_wearwalker_command_set_steps},
	{"Set-watts value", ENTRY_NUMATTR, .num_attr = {.value = 100, .min = 0, .max = 65535}},
	{"Send command: set-watts", ENTRY_ACTION, .callback = call_wearwalker_command_set_watts},
	{"Set-sync epoch", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 4294967295U}},
	{"Send command: set-sync", ENTRY_ACTION, .callback = call_wearwalker_command_set_sync},
	{"Send command: set-trainer", ENTRY_ACTION, .callback = call_wearwalker_command_set_trainer},
};

menu wearwalker_wifi_menu = {
	.title = "WearWalker API Test",
	.entries = wearwalker_wifi_menu_entries,
	.props = {.len = sizeof(wearwalker_wifi_menu_entries) / sizeof(wearwalker_wifi_menu_entries[0]), .selected = 0},
};

menu_entry hgss_patch_menu_entries[] = {
	{"Select HGSS save (.sav)", ENTRY_ACTION, .callback = call_select_hgss_save},
	{"Show selected save path", ENTRY_ACTION, .callback = call_show_hgss_save_path},
	{"Manual steps", ENTRY_NUMATTR, .num_attr = {.value = 2400, .min = 0, .max = 4294967295U}},
	{"Manual watts", ENTRY_NUMATTR, .num_attr = {.value = 120, .min = 0, .max = 65535}},
	{"Manual course flags", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 4294967295U}},
	{"Increment trip counter (0/1)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 0, .max = 1}},
	{"Patch selected save (manual)", ENTRY_ACTION, .callback = call_patch_hgss_manual},
	{"Patch selected save from sync package", ENTRY_ACTION, .callback = call_patch_hgss_from_sync},
};

menu hgss_patch_menu = {
	.title = "HGSS Save Sync/Patch",
	.entries = hgss_patch_menu_entries,
	.props = {.len = sizeof(hgss_patch_menu_entries) / sizeof(hgss_patch_menu_entries[0]), .selected = 0},
};

menu_entry hgss_stroll_send_menu_entries[] = {
	{"Set backend host", ENTRY_ACTION, .callback = call_set_wearwalker_host},
	{"Backend port", ENTRY_NUMATTR, .num_attr = {.value = WW_API_DEFAULT_PORT, .min = 1, .max = 65535}},
	{"Apply endpoint", ENTRY_ACTION, .callback = call_apply_stroll_send_endpoint},
	{"Select HGSS save (.sav)", ENTRY_ACTION, .callback = call_select_hgss_save},
	{"Show selected save path", ENTRY_ACTION, .callback = call_show_hgss_save_path},
	{"Select HGSS ROM (.nds)", ENTRY_ACTION, .callback = call_select_hgss_rom},
	{"Show selected ROM path", ENTRY_ACTION, .callback = call_show_hgss_rom_path},
	{"Source box (1-18)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 1, .max = HGSS_BOX_COUNT}},
	{"Source slot (1-30)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 1, .max = HGSS_BOX_SLOTS}},
	{"Inspect source slot", ENTRY_ACTION, .callback = call_show_stroll_send_slot},
	{"Route/course id (0-26)", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 26}},
	{"Allow locked route (0/1)", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 1}},
	{"Clear stroll buffers (0/1)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 0, .max = 1}},
	{"Send selected box Pokemon to stroll", ENTRY_ACTION, .callback = call_stroll_send_from_save},
};

menu hgss_stroll_send_menu = {
	.title = "HGSS -> Stroll Send",
	.entries = hgss_stroll_send_menu_entries,
	.props = {.len = sizeof(hgss_stroll_send_menu_entries) / sizeof(hgss_stroll_send_menu_entries[0]), .selected = 0},
};

menu_entry hgss_stroll_return_menu_entries[] = {
	{"Set backend host", ENTRY_ACTION, .callback = call_set_wearwalker_host},
	{"Backend port", ENTRY_NUMATTR, .num_attr = {.value = WW_API_DEFAULT_PORT, .min = 1, .max = 65535}},
	{"Apply endpoint", ENTRY_ACTION, .callback = call_apply_stroll_return_endpoint},
	{"Select HGSS save (.sav)", ENTRY_ACTION, .callback = call_select_hgss_save},
	{"Show selected save path", ENTRY_ACTION, .callback = call_show_hgss_save_path},
	{"Source box (1-18)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 1, .max = HGSS_BOX_COUNT}},
	{"Source slot (1-30)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 1, .max = HGSS_BOX_SLOTS}},
	{"Inspect source slot", ENTRY_ACTION, .callback = call_show_stroll_return_slot},
	{"Target slot (0=auto,1-30)", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = HGSS_BOX_SLOTS}},
	{"Walked steps", ENTRY_NUMATTR, .num_attr = {.value = 1200, .min = 0, .max = 4294967295U}},
	{"Bonus watts", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 65535}},
	{"Auto captures", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 0, .max = 3}},
	{"Increment trip counter (0/1)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 0, .max = 1}},
	{"Apply stroll return and patch save", ENTRY_ACTION, .callback = call_stroll_return_to_save},
};

menu hgss_stroll_return_menu = {
	.title = "Stroll -> HGSS Return",
	.entries = hgss_stroll_return_menu_entries,
	.props = {.len = sizeof(hgss_stroll_return_menu_entries) / sizeof(hgss_stroll_return_menu_entries[0]), .selected = 0},
};

// Main menu
menu_entry main_menu_entries[] = {
	{"WearWalker API test menu", ENTRY_CHANGEMENU, .new_menu = &wearwalker_wifi_menu},
	{"HGSS save sync/patch", ENTRY_CHANGEMENU, .new_menu = &hgss_patch_menu},
	{"HGSS stroll send from box", ENTRY_CHANGEMENU, .new_menu = &hgss_stroll_send_menu},
	{"HGSS stroll return to save", ENTRY_CHANGEMENU, .new_menu = &hgss_stroll_return_menu},
};

menu main_menu = {
	.title = "Main menu",
	.entries = main_menu_entries,
	.props = {.len = sizeof(main_menu_entries) / sizeof(main_menu_entries[0]), .selected = 0},
};


// Currently active menu
static menu *g_active_menu = &main_menu;
static enum state g_state = IN_MENU;
static C3D_RenderTarget *target;
static C2D_TextBuf textbuf;
static PrintConsole logs;

void ui_init()
{
	PrintConsole header;

	svcGetThreadPriority(&g_ui_thread_prio, CUR_THREAD_HANDLE);
	LightLock_Init(&g_ww_async.lock);
	g_ww_async.thread = NULL;
	g_ww_async.running = false;
	g_ww_async.finished = false;
	g_ww_async.task = WW_TASK_NONE;
	g_ww_async.success = false;
	g_ww_async.json[0] = '\0';
	memset(&g_ww_async.snapshot, 0, sizeof(g_ww_async.snapshot));

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	consoleInit(GFX_TOP, &header);
	consoleInit(GFX_TOP, &logs);

	consoleSetWindow(&header, 0, 1, header.consoleWidth, 2);
	consoleSetWindow(&logs, 0, 3, logs.consoleWidth, logs.consoleHeight - 3);

	consoleSelect(&header);
	printf("WearWalker Bridge Test v%s\n---", VER);
	consoleSelect(&logs);

	strncpy(g_wearwalker_host, ww_api_get_host(), sizeof(g_wearwalker_host) - 1);
	g_wearwalker_host[sizeof(g_wearwalker_host) - 1] = '\0';
	wearwalker_wifi_menu_entries[WW_MENU_PORT].num_attr.value = ww_api_get_port();
	hgss_stroll_send_menu_entries[SEND_MENU_PORT].num_attr.value = ww_api_get_port();
	hgss_stroll_return_menu_entries[RETURN_MENU_PORT].num_attr.value = ww_api_get_port();
	g_pending_steps = wearwalker_wifi_menu_entries[WW_MENU_CMD_STEPS_VALUE].num_attr.value;
	g_pending_watts = wearwalker_wifi_menu_entries[WW_MENU_CMD_WATTS_VALUE].num_attr.value;
	g_pending_sync = wearwalker_wifi_menu_entries[WW_MENU_CMD_SYNC_VALUE].num_attr.value;
	snprintf(g_pending_trainer, sizeof(g_pending_trainer), "%s", g_wearwalker_trainer);
	g_selected_hgss_save_path[0] = '\0';
	g_selected_hgss_nds_path[0] = '\0';
	g_pending_save_path[0] = '\0';
	g_pending_nds_path[0] = '\0';
	g_pending_hgss_steps = hgss_patch_menu_entries[HGSS_MENU_MANUAL_STEPS].num_attr.value;
	g_pending_hgss_watts = hgss_patch_menu_entries[HGSS_MENU_MANUAL_WATTS].num_attr.value;
	g_pending_hgss_course_flags = hgss_patch_menu_entries[HGSS_MENU_MANUAL_FLAGS].num_attr.value;
	g_pending_increment_trip_counter = hgss_patch_menu_entries[HGSS_MENU_TRIP_COUNTER].num_attr.value != 0;
	g_pending_send_box = hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value;
	g_pending_send_slot = hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value;
	g_pending_send_course = hgss_stroll_send_menu_entries[SEND_MENU_ROUTE].num_attr.value;
	g_pending_send_allow_locked = hgss_stroll_send_menu_entries[SEND_MENU_ALLOW_LOCKED].num_attr.value != 0;
	g_pending_send_clear_buffers = hgss_stroll_send_menu_entries[SEND_MENU_CLEAR_BUFFERS].num_attr.value != 0;
	g_pending_return_box = hgss_stroll_return_menu_entries[RETURN_MENU_BOX].num_attr.value;
	g_pending_return_source_slot = hgss_stroll_return_menu_entries[RETURN_MENU_SOURCE_SLOT].num_attr.value;
	g_pending_return_target_slot = hgss_stroll_return_menu_entries[RETURN_MENU_TARGET_SLOT].num_attr.value;
	g_pending_return_walked_steps = hgss_stroll_return_menu_entries[RETURN_MENU_WALKED_STEPS].num_attr.value;
	g_pending_return_bonus_watts = hgss_stroll_return_menu_entries[RETURN_MENU_BONUS_WATTS].num_attr.value;
	g_pending_return_auto_captures = hgss_stroll_return_menu_entries[RETURN_MENU_AUTO_CAPTURES].num_attr.value;
	g_pending_return_increment_trip_counter = hgss_stroll_return_menu_entries[RETURN_MENU_INCREMENT_TRIP].num_attr.value != 0;
	g_browser_entry_count = 0;
	g_browser_selected = 0;
	g_browser_first = 0;
	snprintf(g_browser_cwd, sizeof(g_browser_cwd), "sdmc:/");
	g_browser_filter = WW_BROWSER_FILTER_SAV;

	target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	textbuf = C2D_TextBufNew(256);
}

void ui_exit()
{
	ww_async_shutdown();

	C2D_TextBufDelete(textbuf);
	C2D_Fini();
	C3D_Fini();
}

void draw_string(float x, float y, float size, const char *str, bool centered, int flags)
{
	C2D_Text text;
	float scale;

	C2D_TextBufClear(textbuf);
	C2D_TextParse(&text, textbuf, str);
	scale = size / 30;
	x = centered ? (SCREEN_WIDTH - text.width * scale) / 2 : x;
	x = x < 0 ? SCREEN_WIDTH - text.width * scale + x : x;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | flags, x, y, 0.0f, scale, scale, COLOR_TEXT);
}

void draw_top(const char *str)
{
	C2D_DrawRectSolid(0, 0, 0, SCREEN_WIDTH, 30, COLOR_BG2);
	C2D_DrawRectSolid(0, 28, 0, SCREEN_WIDTH, 2, C2D_Color32(0, 0, 0, 255));
	draw_string(0, 5, 20, str, true, 0);

}
void draw_scrollbar(u16 first, u16 last, u16 total)
{
	float height = SCREEN_HEIGHT - 36;
	float width = 10;
	float scroll_start = ceil(((height - 4) / total) * first);
	float scroll_height = ceil(((height - 4) / total) * (last - first + 1));

	// The scrollbar has a width of 10 and is placed 3 pixels from the right edge
	C2D_DrawRectSolid(SCREEN_WIDTH - width - 3, 33, 0, width, height, COLOR_SB2);
	C2D_DrawRectSolid(SCREEN_WIDTH - 8 - 4, 35 + scroll_start, 0, 8, scroll_height, COLOR_SB1);
}

void draw_menu(u16 font_size, u16 padding, menu_properties props)
{
	u16 avail_lines, cur, line, first, draw_start, height;
	char strbuf[20];
	selection_menu *sel_menu;

	if (g_state == IN_SELECTION)
		draw_top(g_active_menu->entries[g_active_menu->props.selected].text);
	else
		draw_top(g_active_menu->title);

	height = font_size + padding * 2;
	avail_lines = (SCREEN_HEIGHT - 30) / height;
	cur = props.selected - (avail_lines / 2) > 0 ? props.selected - (avail_lines / 2) : 0;
	draw_start = 30 + (SCREEN_HEIGHT - 30 - avail_lines * height) / 2;

	if ((props.len - cur) < avail_lines)
		cur = props.len - avail_lines > 0 ? props.len - avail_lines : 0;
	first = cur;

	line = 0;
	while (cur < props.len && line < avail_lines) {
		if (cur == props.selected) {
			u16 w = props.len > avail_lines ? 19 : 6;
			C2D_DrawRectSolid(3,
					draw_start + padding - (int) (padding / 2), 0,
					SCREEN_WIDTH - w,
					font_size + 2 * (int) (padding / 2), COLOR_SEL);
		}

		if (g_state == IN_SELECTION) {
			sel_menu = &g_active_menu->entries[g_active_menu->props.selected].sel_menu;
			sprintf(strbuf, "%03d", cur);
			draw_string(6, draw_start + padding, font_size, strbuf, false, 0);
			draw_string(0, draw_start + padding, font_size, sel_menu->options[cur], true, 0);
		} else {
			draw_string(15, draw_start + padding, font_size, g_active_menu->entries[cur].text, false, 0);

			switch (g_active_menu->entries[cur].type) {
					case ENTRY_ACTION:
					case ENTRY_CHANGEMENU:
						break;
				case ENTRY_SELATTR:
					draw_string(-21, draw_start + padding, font_size, g_active_menu->entries[cur].sel_menu.options[g_active_menu->entries[cur].sel_menu.props.selected], false, 0);
					break;
				case ENTRY_NUMATTR:
						sprintf(strbuf, "%lu", (unsigned long)g_active_menu->entries[cur].num_attr.value);
					draw_string(-21, draw_start + padding, font_size, strbuf, false, 0);
					break;
			}
		}

		cur++;
		line++;
		draw_start += height;
	}

	if (props.len > avail_lines)
		draw_scrollbar(first, cur - 1, props.len);
}

void draw_file_browser(void)
{
	const u16 font_size = 12;
	const u16 padding = 3;
	const char *browser_title = g_browser_filter == WW_BROWSER_FILTER_NDS ? "Select HGSS ROM (.nds)" : "Select HGSS save (.sav)";
	const char *empty_hint = g_browser_filter == WW_BROWSER_FILTER_NDS ? "No folders or .nds files here" : "No folders or .sav files here";
	u16 height = font_size + padding * 2;
	u16 avail_lines = (SCREEN_HEIGHT - 52) / height;
	u16 draw_start = 46;
	u32 index;
	u16 line = 0;
	char linebuf[WW_BROWSER_ENTRY_NAME_MAX + 12];

	if (avail_lines == 0)
		avail_lines = 1;

	if (g_browser_entry_count > 0) {
		if ((u32)g_browser_selected < g_browser_first)
			g_browser_first = (u32)g_browser_selected;
		if ((u32)g_browser_selected >= g_browser_first + avail_lines)
			g_browser_first = (u32)g_browser_selected - (avail_lines - 1);
	} else {
		g_browser_first = 0;
	}

	draw_top(browser_title);
	draw_string(6, 32, 9, g_browser_cwd, false, 0);

	if (g_browser_entry_count == 0) {
		draw_string(0, 118, 12, empty_hint, true, 0);
		draw_string(0, 210, 9, "A: open  B: up/back  START: exit", true, 0);
		return;
	}

	index = g_browser_first;
	while (index < g_browser_entry_count && line < avail_lines) {
		if ((s32)index == g_browser_selected) {
			C2D_DrawRectSolid(
					3,
					draw_start + padding - (int)(padding / 2),
					0,
					SCREEN_WIDTH - 6,
					font_size + 2 * (int)(padding / 2),
					COLOR_SEL);
		}

		snprintf(
				linebuf,
				sizeof(linebuf),
				"%s%s",
				g_browser_entries[index].is_dir ? "[DIR] " : "      ",
				g_browser_entries[index].name);
		draw_string(8, draw_start + padding, font_size, linebuf, false, 0);

		draw_start += height;
		line++;
		index++;
	}

	if (g_browser_entry_count > avail_lines)
		draw_scrollbar((u16)g_browser_first, (u16)(index - 1), (u16)g_browser_entry_count);

	draw_string(0, 210, 9, "A: open/select  B: up/back  START: exit", true, 0);
}

s32 numpad_input(const char *hint_text, u8 digits)
{
	char buf[32];
	SwkbdState swkbd;
	SwkbdButton button = SWKBD_BUTTON_NONE;

	swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, digits);
	swkbdSetHintText(&swkbd, hint_text);
	swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
	swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
	button = swkbdInputText(&swkbd, buf, sizeof(buf));

	return button == SWKBD_BUTTON_RIGHT ? atoi(buf) : -1;
}

bool text_input(const char *hint_text, char *out, u32 max_len)
{
	char buf[64];
	SwkbdState swkbd;
	SwkbdButton button = SWKBD_BUTTON_NONE;

	if (!out || max_len < 2 || max_len > sizeof(buf))
		return false;

	snprintf(buf, sizeof(buf), "%s", out);

	swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 1, max_len - 1);
	swkbdSetHintText(&swkbd, hint_text);
	swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
	swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
	button = swkbdInputText(&swkbd, buf, sizeof(buf));

	if (button != SWKBD_BUTTON_RIGHT)
		return false;

	snprintf(out, max_len, "%s", buf);
	return true;
}

// menu_entry must be of type ENTRY_SELATTR
void goto_item(menu_entry *entry)
{
	char str[] = "Go to item";
	s32 value = numpad_input(str, 3);

	if (value >= 0 && value < entry->sel_menu.props.len)
		entry->sel_menu.props.selected = value;
}

// menu_entry must be of type ENTRY_NUMATTR
static u8 ww_num_digits(u32 value)
{
	u8 digits = 1;

	while (value >= 10 && digits < 10) {
		value /= 10;
		digits++;
	}

	return digits;
}

void set_numattr(menu_entry *entry)
{
	char strbuf[64];
	s32 value;
	u8 digits;

	sprintf(strbuf, "%s (min %lu, max %lu)", entry->text,
			(unsigned long)entry->num_attr.min,
			(unsigned long)entry->num_attr.max);
	digits = ww_num_digits(entry->num_attr.max);
	value = numpad_input(strbuf, digits);

	if (value != -1) {
		value = value > entry->num_attr.max ? entry->num_attr.max : value;
		value = value < (s16) entry->num_attr.min ? entry->num_attr.min : value;
		entry->num_attr.value = value;
	}
}

void call_set_wearwalker_host()
{
	if (text_input("WearWalker IP/host", g_wearwalker_host, sizeof(g_wearwalker_host)))
		printf("WearWalker host set to %s\n", g_wearwalker_host);
}

void call_apply_wearwalker_endpoint()
{
	u16 port = wearwalker_wifi_menu_entries[WW_MENU_PORT].num_attr.value;

	if (ww_api_set_endpoint(g_wearwalker_host, port))
		printf("Using WearWalker endpoint %s:%lu\n", g_wearwalker_host, (unsigned long)port);
	else
		printf("Invalid WearWalker endpoint\n");
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

	if (ww_api_set_endpoint(g_wearwalker_host, port))
		printf("Using WearWalker endpoint %s:%lu\n", g_wearwalker_host, (unsigned long)port);
	else
		printf("Invalid WearWalker endpoint\n");
}

void call_show_stroll_send_slot()
{
	hgss_box_slot_summary slot;
	char error[128];
	u32 box = hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value;
	u32 source_slot = hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value;

	if (!ww_prepare_selected_save_path())
		return;

	if (!hgss_read_box_slot_summary(g_pending_save_path, (u8)box, (u8)source_slot, &slot, error, sizeof(error))) {
		printf("%s\n", error[0] ? error : "failed to read source slot");
		return;
	}

	if (!slot.occupied || slot.species_id == 0) {
		printf(
				"Source box %lu slot %lu is empty (return will try walker-pair restore)\n",
				(unsigned long)box,
				(unsigned long)source_slot);
		return;
	}

	printf(
			"Source box %lu slot %lu | species %u | exp %lu | lv~%u\n",
			(unsigned long)box,
			(unsigned long)source_slot,
			(unsigned)slot.species_id,
			(unsigned long)slot.exp,
			(unsigned)ww_estimate_level_from_exp(slot.exp));
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
	g_pending_send_course = hgss_stroll_send_menu_entries[SEND_MENU_ROUTE].num_attr.value;
	g_pending_send_allow_locked = hgss_stroll_send_menu_entries[SEND_MENU_ALLOW_LOCKED].num_attr.value != 0;
	g_pending_send_clear_buffers = hgss_stroll_send_menu_entries[SEND_MENU_CLEAR_BUFFERS].num_attr.value != 0;

	if (!ww_async_start(WW_TASK_HGSS_STROLL_SEND)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Sending selected box Pokemon to stroll...\n");
}

void call_apply_stroll_return_endpoint()
{
	u16 port = hgss_stroll_return_menu_entries[RETURN_MENU_PORT].num_attr.value;

	if (ww_api_set_endpoint(g_wearwalker_host, port))
		printf("Using WearWalker endpoint %s:%lu\n", g_wearwalker_host, (unsigned long)port);
	else
		printf("Invalid WearWalker endpoint\n");
}

void call_show_stroll_return_slot()
{
	hgss_box_slot_summary slot;
	char error[128];
	u32 box = hgss_stroll_return_menu_entries[RETURN_MENU_BOX].num_attr.value;
	u32 source_slot = hgss_stroll_return_menu_entries[RETURN_MENU_SOURCE_SLOT].num_attr.value;

	if (!ww_prepare_selected_save_path())
		return;

	if (!hgss_read_box_slot_summary(g_pending_save_path, (u8)box, (u8)source_slot, &slot, error, sizeof(error))) {
		printf("%s\n", error[0] ? error : "failed to read source slot");
		return;
	}

	if (!slot.occupied || slot.species_id == 0) {
		printf("Source box %lu slot %lu is empty\n", (unsigned long)box, (unsigned long)source_slot);
		return;
	}

	printf(
			"Return source box %lu slot %lu | species %u | exp %lu | friendship %u\n",
			(unsigned long)box,
			(unsigned long)source_slot,
			(unsigned)slot.species_id,
			(unsigned long)slot.exp,
			(unsigned)slot.friendship);
}

void call_stroll_return_to_save()
{
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

	if (!ww_async_start(WW_TASK_HGSS_STROLL_RETURN)) {
		printf("WearWalker request already running\n");
		return;
	}

	printf("Applying stroll return and patching selected HGSS save...\n");
}

void move_selection(const s16 offset)
{
	menu_properties *props;
	s16 new_selected;

	props = g_state == IN_SELECTION ? &g_active_menu->entries[g_active_menu->props.selected].sel_menu.props : &g_active_menu->props;

	new_selected = props->selected + offset;
	if (new_selected >= props->len)
		new_selected = props->len - 1;
	else if (new_selected < 0)
		new_selected = 0;

	props->selected = new_selected;
}

void ui_draw()
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

	C2D_TargetClear(target, COLOR_BG);
	C2D_SceneBegin(target);

	if (g_state == IN_FILE_BROWSER)
		draw_file_browser();
	else if (g_state == IN_SELECTION)
		draw_menu(12, 3, g_active_menu->entries[g_active_menu->props.selected].sel_menu.props);
	else
		draw_menu(15, 5, g_active_menu->props);

	C3D_FrameEnd(0);
}

enum operation ui_update()
{
	menu_entry *selected_entry = &g_active_menu->entries[g_active_menu->props.selected];
	static u16 old_selected = 0;
	bool async_completed;

	gspWaitForVBlank();
	hidScanInput();
	u32 kDown = hidKeysDown() | (hidKeysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT));
	async_completed = ww_async_poll_completion();

	if (kDown & KEY_START)
		return OP_EXIT;

	if (kDown) {
		if (g_state == IN_FILE_BROWSER) {
			if (kDown & KEY_UP) {
				ww_browser_move_selection(-1);
			} else if (kDown & KEY_DOWN) {
				ww_browser_move_selection(1);
			} else if (kDown & KEY_LEFT) {
				ww_browser_move_selection(-10);
			} else if (kDown & KEY_RIGHT) {
				ww_browser_move_selection(10);
			} else if (kDown & KEY_A) {
				bool was_dir = false;

				if (g_browser_entry_count > 0 && g_browser_selected >= 0 && g_browser_selected < (s32)g_browser_entry_count)
					was_dir = g_browser_entries[g_browser_selected].is_dir;

				if (!ww_browser_activate_selected()) {
					printf("Failed to open selected entry\n");
				} else if (!was_dir) {
					g_state = IN_MENU;
					if (g_browser_filter == WW_BROWSER_FILTER_NDS)
						printf("Selected HGSS ROM: %s\n", g_selected_hgss_nds_path);
					else
						printf("Selected HGSS save: %s\n", g_selected_hgss_save_path);
				}
			} else if (kDown & KEY_B) {
				if (ww_path_is_sd_root(g_browser_cwd)) {
					g_state = IN_MENU;
					printf("Closed save browser\n");
				} else {
					ww_browser_enter_parent();
				}
			}

			return OP_UPDATE;
		}

		if (kDown & KEY_UP) {
			move_selection(-1);
		} else if (kDown & KEY_DOWN) {
			move_selection(1);
		} else if (kDown & KEY_LEFT) {
			move_selection(-10);
		} else if (kDown & KEY_RIGHT) {
			move_selection(10);
		} else if (kDown & KEY_Y && g_state == IN_SELECTION) {
			goto_item(selected_entry);
		} else if (kDown & KEY_A) {
			if (g_state == IN_SELECTION) {
				// We are in a selection menu
				g_state = IN_MENU;
				old_selected = 0;
			} else {
				switch (selected_entry->type) {
					case ENTRY_ACTION:
						selected_entry->callback();
						break;
					case ENTRY_CHANGEMENU:
						g_active_menu = selected_entry->new_menu;
						g_active_menu->props.selected = 0;
						consoleClear();
						break;
					case ENTRY_SELATTR:
						old_selected = selected_entry->sel_menu.props.selected;
						g_state = IN_SELECTION;
						break;
					case ENTRY_NUMATTR:
						set_numattr(selected_entry);
						break;
				}
			}
		} else if (kDown & KEY_B) {
			if (g_state == IN_SELECTION) {
				selected_entry->sel_menu.props.selected = old_selected;
				g_state = IN_MENU;
				old_selected = 0;
			} else {
				g_active_menu = &main_menu;
				consoleClear();
			}
		} 
		return OP_UPDATE;
	}

	return async_completed ? OP_UPDATE : OP_NONE;
}

