#include "ui.h"
#include "hgss_item_icon_map.h"
#include "hgss_mon_icon_palette_map.h"
#include "hgss_patcher.h"
#include "hgss_storage.h"
#include "pokewalker_lookup.h"
#include "utils.h"
#include "wearwalker_api.h"

#include <3ds.h>
#include <3ds/util/decompress.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

static bool g_console_enabled = false;
static bool g_debug_console_ready;
static int ww_ui_log(const char *fmt, ...);
#define printf ww_ui_log

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
void call_pick_stroll_send_slot();
void call_stroll_send_from_save();
void call_apply_stroll_return_endpoint();
void call_show_stroll_return_slot();
void call_pick_stroll_return_slot();
void call_stroll_return_to_save();
void call_apply_settings_endpoint();
void call_toggle_simple_mode();
void call_save_ui_config_now();
void call_start_guided_send(void);
void call_start_guided_return(void);

static bool ww_save_ui_config(void);
static void ww_sync_port_entries(u16 port);
static bool ww_apply_endpoint_from_ui(u16 port);
static menu *ww_get_main_menu(void);
static bool ww_prepare_selected_save_path(void);
static bool ww_prepare_selected_nds_path(void);
static void ww_draw_top_context(void);
static void ww_path_basename(const char *path, char *out, size_t out_size);
static void ww_init_debug_console(void);
static void ww_clear_debug_console(void);

typedef enum {
	WW_BOX_PICKER_NONE = 0,
	WW_BOX_PICKER_SEND_SOURCE,
	WW_BOX_PICKER_RETURN_SOURCE,
	WW_BOX_PICKER_RETURN_CAPTURE_1,
	WW_BOX_PICKER_RETURN_CAPTURE_2,
	WW_BOX_PICKER_RETURN_CAPTURE_3,
} ww_box_picker_mode;

static bool ww_box_selector_reload(void);
static bool ww_box_selector_open(ww_box_picker_mode mode);
static void ww_box_selector_apply_choice(void);
static void ww_draw_box_selector(void);
static bool ww_compute_simple_return_walked_steps(u32 *out_walked_steps);
static bool ww_route_selector_reload(void);
static bool ww_route_selector_open(void);
static void ww_draw_route_selector(void);
static void ww_draw_return_selector(void);
static void ww_return_flow_reset(void);
static bool ww_return_fetch_trip_preview(void);
static bool ww_return_open_capture_picker(u8 capture_index);
static bool ww_return_validate_capture_target(u8 capture_index, u8 box, u8 slot);
static void ww_return_set_capture_auto_all(void);
static bool ww_start_guided_return_apply(void);
static void ww_draw_2bpp_sprite(
		const u8 *sprite,
		u32 width,
		u32 height,
		float x,
		float y,
		float scale,
		bool transparent_zero,
		u32 color_seed,
		bool colorize);
static void ww_draw_item_token_sprite(u16 item_id, float x, float y, float scale);
static void ww_draw_indexed_icon(
		const u8 *indices,
		u32 width,
		u32 height,
		const u32 *palette,
		float x,
		float y,
		float scale,
		bool transparent_zero);
static void ww_draw_simple_top_panel(void);
static void ww_async_progress_set(u32 percent, const char *label);
static void ww_async_progress_get(u32 *out_percent, char *out_label, size_t out_label_size);
static void ww_build_sprite_palette(u32 color_seed, bool colorize, u32 out_colors[4]);

typedef struct {
	u16 species_id;
	u8 level;
	u16 min_steps;
	u8 chance;
	u16 moves[4];
	bool sprite_ready;
	u8 sprite_frame0[0x0C0];
	bool color_icon_ready;
	u8 color_icon_width;
	u8 color_icon_height;
	u8 color_icon_indices[32 * 32];
	u32 color_icon_palette[16];
	char species_name[32];
} ww_route_slot_preview;

typedef struct {
	u16 item_id;
	u16 min_steps;
	u8 chance;
	bool icon_ready;
	u8 icon_width;
	u8 icon_height;
	u8 icon_indices[32 * 32];
	u32 icon_palette[16];
	char item_name[32];
} ww_route_item_preview;

typedef struct {
	bool present;
	u16 species_id;
	u8 level;
	u16 moves[4];
	u32 api_slot;
	u8 target_box;
	u8 target_slot;
	char species_name[32];
} ww_return_capture_choice;

typedef enum {
	WW_RETURN_STEP_CONFIRM = 0,
	WW_RETURN_STEP_CAPTURE_POLICY,
	WW_RETURN_STEP_REVIEW,
	WW_RETURN_STEP_APPLYING,
} ww_return_step;

#define WW_ROUTE_PREVIEW_SLOT_COUNT 6u
#define WW_ROUTE_SELECTED_SLOT_COUNT 3u
#define WW_RETURN_CAPTURE_MAX 3u

#define WW_SAVE_PATH_MAX 512
#define WW_BROWSER_ENTRY_NAME_MAX 256
#define WW_BROWSER_MAX_ENTRIES 384
#define WW_ASYNC_LOG_MAX 1024
#define WW_STROLL_SEND_BODY_MAX 12288
#define WW_UI_CONFIG_DIR "sdmc:/3ds/wearwalker_bridge"
#define WW_UI_CONFIG_PATH "sdmc:/3ds/wearwalker_bridge/config.ini"
#define TOP_SCREEN_WIDTH 400

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
static bool g_simple_mode = true;
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
static u32 g_pending_return_auto_captures = 0;
static bool g_pending_return_increment_trip_counter = true;
static ww_browser_entry g_browser_entries[WW_BROWSER_MAX_ENTRIES];
static u32 g_browser_entry_count;
static s32 g_browser_selected;
static u32 g_browser_first;
static char g_browser_cwd[WW_SAVE_PATH_MAX] = "sdmc:/";
static ww_browser_filter g_browser_filter = WW_BROWSER_FILTER_SAV;
static ww_box_picker_mode g_box_picker_mode = WW_BOX_PICKER_NONE;
static u32 g_box_picker_box = 1;
static u32 g_box_picker_slot = 1;
static hgss_box_slot_summary g_box_picker_slots[HGSS_BOX_SLOTS];
static hgss_stroll_send_context g_box_picker_context[HGSS_BOX_SLOTS];
static bool g_box_picker_context_valid[HGSS_BOX_SLOTS];
static u16 g_box_picker_sprite_species = 0xFFFF;
static bool g_box_picker_sprite_ready = false;
static u8 g_box_picker_sprite[0x300];
static bool g_box_picker_color_icon_ready = false;
static u8 g_box_picker_color_icon_width = 0;
static u8 g_box_picker_color_icon_height = 0;
static u8 g_box_picker_color_icon_indices[32 * 32];
static u32 g_box_picker_color_icon_palette[16];
static bool g_box_picker_reload_ok = false;
static char g_box_picker_error[128];
static bool g_route_selector_ready = false;
static char g_route_selector_error[128];
static u32 g_route_selector_course = 0;
static hgss_stroll_send_context g_guided_send_context;
static bool g_guided_send_context_ready = false;
static u8 g_route_preview_area_sprite[0x0C0];
static bool g_route_preview_area_ready = false;
static u8 g_route_preview_adv_types[3];
static ww_route_slot_preview g_route_preview_slots[WW_ROUTE_PREVIEW_SLOT_COUNT];
static ww_route_item_preview g_route_preview_items[10];
static u8 g_route_preview_selected_slots[WW_ROUTE_SELECTED_SLOT_COUNT];
static s8 g_route_preview_selected_group[WW_ROUTE_PREVIEW_SLOT_COUNT];
static bool g_route_selector_locked = false;
static bool g_route_selector_special_lock = false;
static s32 g_route_selector_required_watts = 0;
static u32 g_route_selector_current_watts = 0;
static bool g_route_send_busy = false;
static bool g_return_apply_busy = false;
static u32 g_ui_anim_tick = 0;
static u32 g_async_progress_percent = 0;
static char g_async_progress_label[48] = "Idle";
static u32 g_route_selector_session_seed = 0;
static u32 g_route_selector_preview_seed = 0;
static u32 g_pending_send_route_seed = 0;
static bool g_pending_send_route_seed_valid = false;
static bool g_return_flow_active = false;
static ww_return_step g_return_step = WW_RETURN_STEP_CONFIRM;
static bool g_return_manual_capture_targets = false;
static u8 g_return_capture_pick_index = 0;
static u16 g_return_preview_source_species = 0;
static char g_return_preview_source_name[32];
static u32 g_return_preview_walked_steps = 0;
static u32 g_return_preview_exp_gain = 0;
static u32 g_return_preview_sync_steps = 0;
static u32 g_return_preview_sync_watts = 0;
static u32 g_return_preview_sync_flags = 0;
static u8 g_return_capture_count = 0;
static char g_return_error[160];
static ww_return_capture_choice g_return_captures[WW_RETURN_CAPTURE_MAX];

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
	WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY,
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
#define WW_OV112_COURSE_TABLE_ADDR 0x021F4138u
#define WW_OV112_COURSE_RECORD_SIZE 0x0C0u
#define WW_OV112_COURSE_COUNT 27u
#define WW_OV112_COURSE_TABLE_SIZE (WW_OV112_COURSE_RECORD_SIZE * WW_OV112_COURSE_COUNT)
#define WW_OV112_SLOT_OFFSET 0x008u
#define WW_OV112_SLOT_SIZE 0x014u
#define WW_OV112_SLOT_COUNT 6u
#define WW_OV112_ITEMS_OFFSET 0x080u
#define WW_OV112_ITEM_SIZE 0x006u
#define WW_OV112_ITEM_COUNT 10u
#define WW_OV112_ADV_TYPES_OFFSET 0x0BCu
#define WW_OV112_ADV_TYPES_COUNT 3u
#define WW_OV112_ROUTE_IMAGE_COUNT 8u
#define WW_OV112_ROUTE_IMAGE_SIZE 0x0C0u
#define WW_OV112_MAX_SPECIES_ID 2048u
#define WW_OV112_MAX_TYPE_ID 17u
#define WW_OV112_MAX_SLOT_CHANCE 100u
#define WW_OV112_MAX_ITEM_CHANCE 100u

static const char *g_route_course_names[WW_OV112_COURSE_COUNT] = {
	"Refreshing Field",
	"Noisy Forest",
	"Rugged Road",
	"Beautiful Beach",
	"Suburban Area",
	"Dim Cave",
	"Blue Lake",
	"Town Outskirts",
	"Hoenn Field",
	"Warm Beach",
	"Volcano Path",
	"Treehouse",
	"Scary Cave",
	"Sinnoh Field",
	"Icy Mountain Road",
	"Big Forest",
	"White Lake",
	"Stormy Beach",
	"Resort",
	"Quiet Cave",
	"Beyond The Sea",
	"Night Sky's Edge",
	"Yellow Forest",
	"Rally",
	"Sightseeing",
	"Winners Path",
	"Amity Meadow",
};

static const s32 g_route_course_watts_required[WW_OV112_COURSE_COUNT] = {
	0,
	0,
	50,
	200,
	500,
	1000,
	2000,
	3000,
	5000,
	7500,
	10000,
	15000,
	20000,
	25000,
	30000,
	40000,
	50000,
	65000,
	80000,
	100000,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
};

static const char *g_type_names[18] = {
	"Normal",
	"Fighting",
	"Flying",
	"Poison",
	"Ground",
	"Rock",
	"Bug",
	"Ghost",
	"Steel",
	"???",
	"Fire",
	"Water",
	"Grass",
	"Electric",
	"Psychic",
	"Ice",
	"Dragon",
	"Dark",
};

#define WW_ROM_CACHE_MAGIC 0x48524357u /* WRCH */
#define WW_ROM_CACHE_VERSION 2u
#define WW_ROM_CACHE_PATH "sdmc:/3ds/wearwalker_bridge/rom_course_cache.bin"

typedef struct {
	u32 magic;
	u16 version;
	u16 reserved;
	u64 rom_size;
	u64 rom_mtime;
	u32 table_size;
	u32 course_count;
} ww_rom_cache_header;

static int ww_ui_log(const char *fmt, ...)
{
	va_list args;
	int written;

	if (!g_console_enabled || !g_debug_console_ready)
		return 0;

	va_start(args, fmt);
	written = vprintf(fmt, args);
	va_end(args);

	return written;
}

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
		case WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY:
			return "HGSS guided stroll return";
		default:
			return "request";
	}
}

static void ww_async_progress_set(u32 percent, const char *label)
{
	if (percent > 100)
		percent = 100;

	LightLock_Lock(&g_ww_async.lock);
	g_async_progress_percent = percent;
	snprintf(g_async_progress_label, sizeof(g_async_progress_label), "%s", (label && label[0]) ? label : "Working");
	LightLock_Unlock(&g_ww_async.lock);
}

static void ww_async_progress_get(u32 *out_percent, char *out_label, size_t out_label_size)
{
	if (!out_percent)
		return;

	LightLock_Lock(&g_ww_async.lock);
	*out_percent = g_async_progress_percent;
	if (out_label && out_label_size > 0)
		snprintf(out_label, out_label_size, "%s", g_async_progress_label);
	LightLock_Unlock(&g_ww_async.lock);
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

static bool ww_json_get_string_after_token(
		const char *json,
		const char *anchor,
		const char *key,
		char *out_str,
		u32 out_size)
{
	const char *cursor;

	if (!json || !anchor || !key || !out_str || out_size == 0)
		return false;

	cursor = strstr(json, anchor);
	if (!cursor)
		return false;

	return ww_json_get_string_from(cursor, key, out_str, out_size);
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

static u8 ww_json_get_caught_captures_from_sync(
		const char *sync_json,
		ww_return_capture_choice out_captures[WW_RETURN_CAPTURE_MAX])
{
	const char *domains;
	const char *inventory;
	const char *caught;
	const char *list_start;
	const char *list_end;
	const char *cursor;
	u8 parsed = 0;
	u8 i;

	if (!sync_json || !out_captures)
		return 0;

	for (i = 0; i < WW_RETURN_CAPTURE_MAX; i++)
		memset(&out_captures[i], 0, sizeof(out_captures[i]));

	domains = strstr(sync_json, "\"domains\"");
	if (!domains)
		return 0;

	inventory = strstr(domains, "\"inventory\"");
	if (!inventory)
		return 0;

	caught = strstr(inventory, "\"caught\"");
	if (!caught)
		return 0;

	list_start = strchr(caught, '[');
	if (!list_start)
		return 0;
	list_end = ww_json_find_array_end(list_start, sync_json + strlen(sync_json));
	if (!list_end)
		return 0;

	cursor = list_start + 1;
	while (cursor < list_end && parsed < WW_RETURN_CAPTURE_MAX) {
		const char *entry;
		const char *entry_end;
		u32 species = 0;
		u32 level = 10;
		u32 api_slot = 0;
		ww_return_capture_choice *capture;

		entry = strchr(cursor, '{');
		if (!entry || entry >= list_end)
			break;

		entry_end = ww_json_find_object_end(entry, list_end + 1);
		if (!entry_end)
			break;

		if (!ww_json_get_u32_from_range(entry, entry_end + 1, "speciesId", &species)
				|| species == 0
				|| species > 0xFFFFu) {
			cursor = entry_end + 1;
			continue;
		}

		ww_json_get_u32_from_range(entry, entry_end + 1, "level", &level);
		ww_json_get_u32_from_range(entry, entry_end + 1, "slot", &api_slot);
		if (level == 0 || level > 100)
			level = 10;

		capture = &out_captures[parsed];
		capture->present = true;
		capture->species_id = (u16)species;
		capture->level = (u8)level;
		capture->api_slot = api_slot;
		capture->target_box = 0;
		capture->target_slot = 0;
		capture->moves[0] = 0;
		capture->moves[1] = 0;
		capture->moves[2] = 0;
		capture->moves[3] = 0;

		if (!ww_json_get_string_from_range(
					entry,
					entry_end + 1,
					"speciesName",
					capture->species_name,
					sizeof(capture->species_name))) {
			snprintf(
					capture->species_name,
					sizeof(capture->species_name),
					"%s",
					ww_lookup_species_name(capture->species_id));
		}

		if (!ww_json_get_moves_from_range(entry, entry_end + 1, capture->moves))
			capture->moves[0] = 33;

		parsed++;
		cursor = entry_end + 1;
	}

	return parsed;
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

static bool ww_course_record_is_plausible(const u8 *record)
{
	u32 route_plus_one;
	u32 slot;
	u32 valid_species = 0;
	u32 item;
	u32 adv_idx;

	if (!record)
		return false;

	route_plus_one = ww_read_u32_le(record + 0x04);
	if (route_plus_one == 0 || route_plus_one > WW_OV112_ROUTE_IMAGE_COUNT)
		return false;

	for (slot = 0; slot < WW_OV112_SLOT_COUNT; slot++) {
		u32 base = WW_OV112_SLOT_OFFSET + slot * WW_OV112_SLOT_SIZE;
		u16 species_id = ww_read_u16_le(record + base + 0x00);
		u8 level = record[base + 0x02];
		u16 chance = ww_read_u16_le(record + base + 0x12);

		if (species_id > WW_OV112_MAX_SPECIES_ID)
			return false;
		if (species_id != 0)
			valid_species++;
		if (level > 100)
			return false;
		if (species_id != 0 && level == 0)
			return false;
		if (chance > WW_OV112_MAX_SLOT_CHANCE)
			return false;
	}

	if (valid_species < 4)
		return false;

	for (item = 0; item < WW_OV112_ITEM_COUNT; item++) {
		u32 base = WW_OV112_ITEMS_OFFSET + item * WW_OV112_ITEM_SIZE;
		u16 chance = ww_read_u16_le(record + base + 0x04);

		if (chance > WW_OV112_MAX_ITEM_CHANCE)
			return false;
	}

	for (adv_idx = 0; adv_idx < WW_OV112_ADV_TYPES_COUNT; adv_idx++) {
		u8 type_id = record[WW_OV112_ADV_TYPES_OFFSET + adv_idx];

		if (type_id > WW_OV112_MAX_TYPE_ID)
			return false;
	}

	return true;
}

static bool ww_course_table_is_plausible(const u8 *table)
{
	u32 course_id;

	if (!table)
		return false;

	for (course_id = 0; course_id < WW_OV112_COURSE_COUNT; course_id++) {
		const u8 *record = table + course_id * WW_OV112_COURSE_RECORD_SIZE;

		if (!ww_course_record_is_plausible(record))
			return false;
	}

	return true;
}

static bool ww_find_course_table_offset_in_overlay(const u8 *overlay_data, u32 overlay_size, u32 *out_offset)
{
	u32 offset;

	if (!overlay_data || !out_offset)
		return false;
	if (overlay_size < WW_OV112_COURSE_TABLE_SIZE)
		return false;

	for (offset = 0; (u64)offset + (u64)WW_OV112_COURSE_TABLE_SIZE <= (u64)overlay_size; offset += 4u) {
		if (ww_course_table_is_plausible(overlay_data + offset)) {
			*out_offset = offset;
			return true;
		}
	}

	return false;
}

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

static bool ww_get_file_identity(const char *path, u64 *out_size, u64 *out_mtime)
{
	struct stat info;

	if (!path || !out_size || !out_mtime)
		return false;

	if (stat(path, &info) != 0)
		return false;
	if (!S_ISREG(info.st_mode))
		return false;

	*out_size = (u64)info.st_size;
	*out_mtime = (u64)info.st_mtime;
	return true;
}

static bool ww_load_overlay112_from_nds(
		const char *nds_path,
		u8 **out_overlay_data,
		u32 *out_overlay_size,
		u32 *out_ram_address)
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
	long nds_size;
	bool ok = false;

	if (!nds_path || !out_overlay_data || !out_overlay_size || !out_ram_address)
		return false;

	*out_overlay_data = NULL;
	*out_overlay_size = 0;
	*out_ram_address = 0;

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

	*out_overlay_data = overlay_file;
	*out_overlay_size = overlay_file_size;
	*out_ram_address = ov112_ram_address;
	overlay_file = NULL;
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

static bool ww_extract_course_table_from_nds(
		const char *nds_path,
		u8 out_table[WW_OV112_COURSE_TABLE_SIZE])
{
	u8 *overlay_data = NULL;
	u32 overlay_size = 0;
	u32 ov112_ram_address = 0;
	u32 table_offset;
	u8 candidate_table[WW_OV112_COURSE_TABLE_SIZE];
	bool ok = false;

	if (!nds_path || !out_table)
		return false;

	if (!ww_load_overlay112_from_nds(nds_path, &overlay_data, &overlay_size, &ov112_ram_address))
		goto cleanup;

	if (ov112_ram_address < WW_OV112_COURSE_TABLE_ADDR) {
		table_offset = WW_OV112_COURSE_TABLE_ADDR - ov112_ram_address;
		if ((u64)table_offset + (u64)WW_OV112_COURSE_TABLE_SIZE <= (u64)overlay_size) {
			memcpy(candidate_table, overlay_data + table_offset, WW_OV112_COURSE_TABLE_SIZE);
			if (ww_course_table_is_plausible(candidate_table)) {
				memcpy(out_table, candidate_table, WW_OV112_COURSE_TABLE_SIZE);
				ok = true;
				goto cleanup;
			}
		}
	}

	if (!ww_find_course_table_offset_in_overlay(overlay_data, overlay_size, &table_offset))
		goto cleanup;

	memcpy(out_table, overlay_data + table_offset, WW_OV112_COURSE_TABLE_SIZE);
	ok = true;

cleanup:
	if (overlay_data)
		free(overlay_data);
	return ok;
}

static void ww_ensure_rom_cache_dirs(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/wearwalker_bridge", 0777);
}

static bool ww_load_course_table_cache(
		u64 rom_size,
		u64 rom_mtime,
		u8 out_table[WW_OV112_COURSE_TABLE_SIZE])
{
	FILE *f = NULL;
	ww_rom_cache_header header;
	bool ok = false;

	if (!out_table)
		return false;

	f = fopen(WW_ROM_CACHE_PATH, "rb");
	if (!f)
		goto cleanup;

	if (fread(&header, sizeof(header), 1, f) != 1)
		goto cleanup;
	if (header.magic != WW_ROM_CACHE_MAGIC)
		goto cleanup;
	if (header.version != WW_ROM_CACHE_VERSION)
		goto cleanup;
	if (header.course_count != WW_OV112_COURSE_COUNT)
		goto cleanup;
	if (header.table_size != WW_OV112_COURSE_TABLE_SIZE)
		goto cleanup;
	if (header.rom_size != rom_size || header.rom_mtime != rom_mtime)
		goto cleanup;

	if (fread(out_table, 1, WW_OV112_COURSE_TABLE_SIZE, f) != WW_OV112_COURSE_TABLE_SIZE)
		goto cleanup;
	if (!ww_course_table_is_plausible(out_table))
		goto cleanup;

	ok = true;

cleanup:
	if (f)
		fclose(f);
	return ok;
}

static void ww_save_course_table_cache(
		u64 rom_size,
		u64 rom_mtime,
		const u8 table[WW_OV112_COURSE_TABLE_SIZE])
{
	FILE *f;
	ww_rom_cache_header header;

	if (!table)
		return;

	ww_ensure_rom_cache_dirs();
	f = fopen(WW_ROM_CACHE_PATH, "wb");
	if (!f)
		return;

	memset(&header, 0, sizeof(header));
	header.magic = WW_ROM_CACHE_MAGIC;
	header.version = WW_ROM_CACHE_VERSION;
	header.rom_size = rom_size;
	header.rom_mtime = rom_mtime;
	header.table_size = WW_OV112_COURSE_TABLE_SIZE;
	header.course_count = WW_OV112_COURSE_COUNT;

	if (fwrite(&header, sizeof(header), 1, f) == 1)
		fwrite(table, 1, WW_OV112_COURSE_TABLE_SIZE, f);

	fclose(f);
}

static bool ww_get_course_table_cached(
		const char *nds_path,
		u8 out_table[WW_OV112_COURSE_TABLE_SIZE],
		bool *out_cache_hit)
{
	u64 rom_size;
	u64 rom_mtime;

	if (!nds_path || !out_table)
		return false;

	if (out_cache_hit)
		*out_cache_hit = false;

	if (!ww_get_file_identity(nds_path, &rom_size, &rom_mtime))
		return false;

	if (ww_load_course_table_cache(rom_size, rom_mtime, out_table)) {
		if (out_cache_hit)
			*out_cache_hit = true;
		return true;
	}

	if (!ww_extract_course_table_from_nds(nds_path, out_table))
		return false;
	if (!ww_course_table_is_plausible(out_table))
		return false;

	ww_save_course_table_cache(rom_size, rom_mtime, out_table);
	return true;
}

static bool ww_extract_route_area_sprite_from_nds(
		const char *nds_path,
		u32 route_image_index,
		u8 out_sprite[WW_OV112_ROUTE_IMAGE_SIZE])
{
	u8 *overlay_file = NULL;
	u32 overlay_file_size = 0;
	u32 ov112_ram_address = 0;
	u32 pointer_table_offset;
	u32 pointer_offset;
	u32 sprite_address;
	u32 sprite_offset;
	bool ok = false;

	if (!nds_path || !out_sprite)
		return false;
	if (route_image_index >= WW_OV112_ROUTE_IMAGE_COUNT)
		return false;

	if (!ww_load_overlay112_from_nds(nds_path, &overlay_file, &overlay_file_size, &ov112_ram_address))
		goto cleanup;

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

	if (!ww_nds_find_file_id_from_fnt(fnt_data, fnt_size, narc_path, &file_id))
		goto cleanup;

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

static bool ww_nitro_find_chunk(const u8 *data, u32 data_size, const char chunk_id[4], u32 *out_offset, u32 *out_size)
{
	u32 offset = 16;
	u16 chunk_count;
	u32 chunk_index;

	if (!data || !chunk_id || !out_offset || !out_size || data_size < 16)
		return false;

	chunk_count = ww_read_u16_le(data + 0x0E);
	for (chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
		u32 chunk_size;

		if (offset + 8 > data_size)
			return false;

		chunk_size = ww_read_u32_le(data + offset + 4);
		if (chunk_size < 8 || offset + chunk_size > data_size)
			return false;

		if (memcmp(data + offset, chunk_id, 4) == 0) {
			*out_offset = offset;
			*out_size = chunk_size;
			return true;
		}

		offset += chunk_size;
	}

	return false;
}

static bool ww_unpack_narc_member_lz10(
		const u8 *member_data,
		u32 member_size,
		const u8 **out_data,
		u32 *out_size,
		u8 **out_allocated)
{
	u8 *decompressed = NULL;
	u32 decompressed_size = 0;

	if (!member_data || !out_data || !out_size || !out_allocated || member_size == 0)
		return false;

	*out_data = NULL;
	*out_size = 0;
	*out_allocated = NULL;

	if (member_data[0] == 0x10) {
		if (!ww_decompress_lz10(member_data, member_size, &decompressed, &decompressed_size))
			return false;

		*out_data = decompressed;
		*out_size = decompressed_size;
		*out_allocated = decompressed;
		return true;
	}

	*out_data = member_data;
	*out_size = member_size;
	return true;
}

static u32 ww_bgr555_to_c2d_color(u16 bgr555, bool transparent)
{
	u8 r5 = (u8)(bgr555 & 0x1Fu);
	u8 g5 = (u8)((bgr555 >> 5) & 0x1Fu);
	u8 b5 = (u8)((bgr555 >> 10) & 0x1Fu);
	u8 r8 = (u8)((r5 << 3) | (r5 >> 2));
	u8 g8 = (u8)((g5 << 3) | (g5 >> 2));
	u8 b8 = (u8)((b5 << 3) | (b5 >> 2));

	return C2D_Color32(r8, g8, b8, transparent ? 0x00 : 0xFF);
}

static bool ww_decode_ncgr_32x32_4bpp(const u8 *ncgr_data, u32 ncgr_size, u8 out_indices[32 * 32], u8 *out_width, u8 *out_height)
{
	u32 rahc_offset;
	u32 rahc_size;
	u32 pixel_data_size;
	const u8 *pixel_data;
	u32 tile_index;
	u32 nonzero = 0;

	if (!ncgr_data || !out_indices || !out_width || !out_height)
		return false;
	if (ncgr_size < 16 || memcmp(ncgr_data, "RGCN", 4) != 0)
		return false;
	if (!ww_nitro_find_chunk(ncgr_data, ncgr_size, "RAHC", &rahc_offset, &rahc_size))
		return false;
	if (rahc_size < 0x20 || rahc_offset + 0x20 > ncgr_size)
		return false;

	pixel_data_size = ww_read_u32_le(ncgr_data + rahc_offset + 0x18);
	if (pixel_data_size < 0x200)
		return false;
	if (rahc_offset + 0x20 + pixel_data_size > ncgr_size)
		return false;

	pixel_data = ncgr_data + rahc_offset + 0x20;
	memset(out_indices, 0, 32 * 32);

	for (tile_index = 0; tile_index < 16; tile_index++) {
		const u8 *tile = pixel_data + tile_index * 32u;
		u32 tile_x = tile_index % 4u;
		u32 tile_y = tile_index / 4u;
		u32 py;

		for (py = 0; py < 8u; py++) {
			u32 px;

			for (px = 0; px < 8u; px++) {
				u8 packed = tile[py * 4u + (px >> 1u)];
				u8 color_index = (px & 1u) ? (packed >> 4) : (packed & 0x0Fu);
				u32 out_x = tile_x * 8u + px;
				u32 out_y = tile_y * 8u + py;

				if (color_index != 0)
					nonzero++;
				out_indices[out_y * 32u + out_x] = color_index;
			}
		}
	}

	if (nonzero < 8u)
		return false;

	*out_width = 32;
	*out_height = 32;
	return true;
}

static bool ww_decode_nclr_colors(const u8 *nclr_data, u32 nclr_size, u32 *out_colors, u32 out_cap, u32 *out_color_count)
{
	u32 ttlp_offset;
	u32 ttlp_size;
	u32 palette_size;
	u32 color_count;
	const u8 *palette_data;
	u32 i;

	if (!nclr_data || !out_colors || out_cap == 0 || !out_color_count)
		return false;
	if (nclr_size < 16 || memcmp(nclr_data, "RLCN", 4) != 0)
		return false;
	if (!ww_nitro_find_chunk(nclr_data, nclr_size, "TTLP", &ttlp_offset, &ttlp_size))
		return false;
	if (ttlp_size < 0x18 || ttlp_offset + 0x18 > nclr_size)
		return false;

	palette_size = ww_read_u32_le(nclr_data + ttlp_offset + 0x10);
	if (palette_size < 32)
		return false;
	if (ttlp_offset + 0x18 + palette_size > nclr_size)
		return false;

	palette_data = nclr_data + ttlp_offset + 0x18;
	color_count = palette_size / 2u;
	if (color_count > out_cap)
		color_count = out_cap;

	for (i = 0; i < color_count; i++) {
		u16 color555 = ww_read_u16_le(palette_data + i * 2u);
		out_colors[i] = ww_bgr555_to_c2d_color(color555, false);
	}

	*out_color_count = color_count;

	return color_count >= 16u;
}

static bool ww_copy_palette16(const u32 *src_colors, u32 src_count, u32 palette_index, u32 out_palette[16])
{
	u32 base;
	u32 i;

	if (!src_colors || !out_palette)
		return false;

	base = palette_index * 16u;
	if (base + 16u > src_count)
		return false;

	for (i = 0; i < 16u; i++) {
		u32 color = src_colors[base + i];
		if (i == 0u)
			color &= 0x00FFFFFFu;
		else
			color = (color & 0x00FFFFFFu) | 0xFF000000u;
		out_palette[i] = color;
	}

	return true;
}

static u32 ww_species_to_icon_member_index(u16 species_id)
{
	if (species_id >= 1u && species_id <= 493u)
		return (u32)species_id + 7u;
	return 7u;
}

static u32 ww_species_to_icon_palette_index(u16 species_id)
{
	if (species_id < WW_HGSS_MON_ICON_PALETTE_COUNT)
		return (u32)g_hgss_mon_icon_palette_idx[species_id];
	return 0u;
}

static bool ww_extract_species_color_icon_from_narc(
		const u8 *poke_icon_narc,
		u32 poke_icon_narc_size,
		u16 species_id,
		u8 out_indices[32 * 32],
		u32 out_palette[16],
		u8 *out_width,
		u8 *out_height)
{
	u32 icon_member_index;
	u32 palette_index;
	const u8 *icon_member;
	u32 icon_member_size;
	const u8 *palette_member;
	u32 palette_member_size;
	const u8 *icon_data;
	u32 icon_size;
	const u8 *palette_data;
	u32 palette_size;
	u8 *owned_icon = NULL;
	u8 *owned_palette = NULL;
	u32 colors[256];
	u32 color_count = 0;
	bool ok = false;

	if (!poke_icon_narc || poke_icon_narc_size == 0 || !out_indices || !out_palette || !out_width || !out_height)
		return false;

	icon_member_index = ww_species_to_icon_member_index(species_id);
	palette_index = ww_species_to_icon_palette_index(species_id);

	if (!ww_narc_get_file_by_index(poke_icon_narc, poke_icon_narc_size, icon_member_index, &icon_member, &icon_member_size))
		return false;
	if (!ww_narc_get_file_by_index(poke_icon_narc, poke_icon_narc_size, 0u, &palette_member, &palette_member_size))
		return false;
	if (!ww_unpack_narc_member_lz10(icon_member, icon_member_size, &icon_data, &icon_size, &owned_icon))
		goto cleanup;
	if (!ww_unpack_narc_member_lz10(palette_member, palette_member_size, &palette_data, &palette_size, &owned_palette))
		goto cleanup;
	if (!ww_decode_ncgr_32x32_4bpp(icon_data, icon_size, out_indices, out_width, out_height))
		goto cleanup;
	if (!ww_decode_nclr_colors(palette_data, palette_size, colors, sizeof(colors) / sizeof(colors[0]), &color_count))
		goto cleanup;
	if (!ww_copy_palette16(colors, color_count, palette_index, out_palette))
		goto cleanup;

	ok = true;

cleanup:
	if (owned_icon)
		free(owned_icon);
	if (owned_palette)
		free(owned_palette);
	return ok;
}

static bool ww_decode_item_icon_nclr(const u8 *nclr_data, u32 nclr_size, u32 out_palette[16])
{
	u32 colors[256];
	u32 color_count = 0;

	if (!out_palette)
		return false;
	if (!ww_decode_nclr_colors(nclr_data, nclr_size, colors, sizeof(colors) / sizeof(colors[0]), &color_count))
		return false;
	return ww_copy_palette16(colors, color_count, 0u, out_palette);
}

static bool ww_lookup_item_icon_indices(u16 item_id, u16 *out_ncgr_index, u16 *out_nclr_index)
{
	if (!out_ncgr_index || !out_nclr_index)
		return false;
	if (item_id >= WW_HGSS_ITEM_ICON_COUNT)
		return false;

	*out_ncgr_index = g_hgss_item_icon_ncgr[item_id];
	*out_nclr_index = g_hgss_item_icon_nclr[item_id];
	return true;
}

static bool ww_extract_item_icon_from_narc(
		const u8 *item_icon_narc,
		u32 item_icon_narc_size,
		u16 item_id,
		u8 out_indices[32 * 32],
		u32 out_palette[16],
		u8 *out_width,
		u8 *out_height)
{
	u16 ncgr_index;
	u16 nclr_index;
	const u8 *ncgr_member;
	u32 ncgr_member_size;
	const u8 *nclr_member;
	u32 nclr_member_size;
	const u8 *ncgr_data;
	u32 ncgr_size;
	const u8 *nclr_data;
	u32 nclr_size;
	u8 *owned_ncgr = NULL;
	u8 *owned_nclr = NULL;
	bool ok = false;

	if (!item_icon_narc || item_icon_narc_size == 0 || !out_indices || !out_palette || !out_width || !out_height)
		return false;
	if (!ww_lookup_item_icon_indices(item_id, &ncgr_index, &nclr_index))
		return false;
	if (!ww_narc_get_file_by_index(item_icon_narc, item_icon_narc_size, ncgr_index, &ncgr_member, &ncgr_member_size))
		return false;
	if (!ww_narc_get_file_by_index(item_icon_narc, item_icon_narc_size, nclr_index, &nclr_member, &nclr_member_size))
		return false;
	if (!ww_unpack_narc_member_lz10(ncgr_member, ncgr_member_size, &ncgr_data, &ncgr_size, &owned_ncgr))
		goto cleanup;
	if (!ww_unpack_narc_member_lz10(nclr_member, nclr_member_size, &nclr_data, &nclr_size, &owned_nclr))
		goto cleanup;
	if (!ww_decode_ncgr_32x32_4bpp(ncgr_data, ncgr_size, out_indices, out_width, out_height))
		goto cleanup;
	if (!ww_decode_item_icon_nclr(nclr_data, nclr_size, out_palette))
		goto cleanup;

	ok = true;

cleanup:
	if (owned_ncgr)
		free(owned_ncgr);
	if (owned_nclr)
		free(owned_nclr);
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

static u32 ww_lcg_next(u32 *state)
{
	if (!state)
		return 0;

	*state = (*state * 1664525u) + 1013904223u;
	return *state;
}

static u32 ww_lcg_pair_pick(u32 *state)
{
	/* Do not use LCG LSB: it alternates predictably and creates obvious pair patterns. */
	return (ww_lcg_next(state) >> 16) & 0x1u;
}

static void ww_sanitize_json_ascii(const char *src, char *dst, size_t dst_size)
{
	size_t pos = 0;

	if (!dst || dst_size == 0)
		return;

	if (!src) {
		dst[0] = '\0';
		return;
	}

	while (*src && pos + 1 < dst_size) {
		unsigned char c = (unsigned char)*src++;
		if (c < 0x20 || c == '"' || c == '\\')
			dst[pos++] = '?';
		else
			dst[pos++] = (char)c;
	}

	dst[pos] = '\0';
}

static bool ww_json_append(char **cursor, size_t *remaining, const char *fmt, ...)
{
	va_list args;
	int written;

	if (!cursor || !remaining || !*cursor || !fmt || *remaining == 0)
		return false;

	va_start(args, fmt);
	written = vsnprintf(*cursor, *remaining, fmt, args);
	va_end(args);

	if (written < 0 || (size_t)written >= *remaining)
		return false;

	*cursor += written;
	*remaining -= (size_t)written;
	return true;
}

static bool ww_build_resolved_stroll_send_json(
		const hgss_stroll_send_context *send_context,
		u8 level,
		u32 course_id,
		bool force_route_seed,
		u32 forced_route_seed,
		bool clear_buffers,
		bool allow_locked_course,
		const u8 course_table[WW_OV112_COURSE_TABLE_SIZE],
		u64 rom_size,
		u64 rom_mtime,
		u32 *out_route_image_index,
		u32 *out_route_seed,
		char *out_body,
		u32 out_body_size)
{
	const u8 *record;
	char safe_nickname[48];
	char *cursor;
	size_t remaining;
	u32 route_plus_one;
	u32 route_image_index;
	u32 route_seed;
	u8 adv_type0;
	u8 adv_type1;
	u8 adv_type2;
	u32 group;
	u32 item_index;

	if (!send_context || !course_table || !out_body || out_body_size < 256)
		return false;
	if (course_id >= WW_OV112_COURSE_COUNT)
		return false;

	record = course_table + course_id * WW_OV112_COURSE_RECORD_SIZE;
	route_plus_one = ww_read_u32_le(record + 0x04);
	if (route_plus_one > 0 && route_plus_one <= WW_OV112_ROUTE_IMAGE_COUNT) {
		route_image_index = route_plus_one - 1u;
	} else {
		u32 route_plus_one_low = route_plus_one & 0xFFu;

		if (route_plus_one_low > 0 && route_plus_one_low <= WW_OV112_ROUTE_IMAGE_COUNT)
			route_image_index = route_plus_one_low - 1u;
		else if (course_id < WW_OV112_ROUTE_IMAGE_COUNT)
			route_image_index = course_id;
		else
			route_image_index = course_id % WW_OV112_ROUTE_IMAGE_COUNT;
	}
	if (force_route_seed) {
		route_seed = forced_route_seed;
	} else {
		route_seed = (course_id + 1u) * 2654435761u;
		route_seed ^= ((u32)send_context->source_slot.species_id << 16);
		route_seed ^= send_context->source_slot.exp;
	}
	adv_type0 = record[WW_OV112_ADV_TYPES_OFFSET + 0u];
	adv_type1 = record[WW_OV112_ADV_TYPES_OFFSET + 1u];
	adv_type2 = record[WW_OV112_ADV_TYPES_OFFSET + 2u];
	if (route_seed == 0)
		route_seed = 0xA5A55A5Au;

	if (out_route_image_index)
		*out_route_image_index = route_image_index;
	if (out_route_seed)
		*out_route_seed = route_seed;

	ww_sanitize_json_ascii(send_context->nickname, safe_nickname, sizeof(safe_nickname));
	cursor = out_body;
	remaining = out_body_size;

	if (!ww_json_append(
				&cursor,
				&remaining,
				"{\"speciesId\":%u,\"level\":%u,\"courseId\":%u,\"nickname\":\"%s\",\"friendship\":%u,\"heldItem\":%u,\"moves\":[%u,%u,%u,%u],\"variantFlags\":%u,\"specialFlags\":%u,\"clearBuffers\":%s,\"allowLockedCourse\":%s,\"resolvedRouteConfig\":{\"schemaVersion\":1,\"romSize\":%llu,\"romMtime\":%llu,\"routeImageIndex\":%u,\"routeSeed\":%u,\"advantagedTypes\":[%u,%u,%u],\"slots\":[",
				(unsigned)send_context->source_slot.species_id,
				(unsigned)level,
				(unsigned)course_id,
				safe_nickname,
				(unsigned)send_context->source_slot.friendship,
				(unsigned)send_context->held_item,
				(unsigned)send_context->moves[0],
				(unsigned)send_context->moves[1],
				(unsigned)send_context->moves[2],
				(unsigned)send_context->moves[3],
				(unsigned)send_context->variant_flags,
				(unsigned)send_context->special_flags,
				clear_buffers ? "true" : "false",
				allow_locked_course ? "true" : "false",
				(unsigned long long)rom_size,
				(unsigned long long)rom_mtime,
				(unsigned)route_image_index,
				(unsigned)route_seed,
				(unsigned)adv_type0,
				(unsigned)adv_type1,
				(unsigned)adv_type2)) {
		return false;
	}

	for (group = 0; group < 3; group++) {
		u32 pair_base = group * 2u;
		u32 pair_pick = ww_lcg_pair_pick(&route_seed);
		u32 source_pair_index = pair_base + pair_pick;
		u32 base = WW_OV112_SLOT_OFFSET + source_pair_index * WW_OV112_SLOT_SIZE;
		u16 species_id = ww_read_u16_le(record + base + 0x00);
		u8 slot_level = record[base + 0x02];
		u8 gender = record[base + 0x07];
		u16 move0 = ww_read_u16_le(record + base + 0x08);
		u16 move1 = ww_read_u16_le(record + base + 0x0A);
		u16 move2 = ww_read_u16_le(record + base + 0x0C);
		u16 move3 = ww_read_u16_le(record + base + 0x0E);
		u16 min_steps = ww_read_u16_le(record + base + 0x10);
		u16 chance_raw = ww_read_u16_le(record + base + 0x12);
		u8 chance = chance_raw > 0xFFu ? 0xFFu : (u8)chance_raw;

		if (species_id == 0) {
			u32 alt_pair_index = pair_base + (pair_pick ^ 0x1u);
			u32 alt_base = WW_OV112_SLOT_OFFSET + alt_pair_index * WW_OV112_SLOT_SIZE;
			u16 alt_species_id = ww_read_u16_le(record + alt_base + 0x00);

			if (alt_species_id != 0) {
				source_pair_index = alt_pair_index;
				base = alt_base;
				species_id = alt_species_id;
				slot_level = record[base + 0x02];
				gender = record[base + 0x07];
				move0 = ww_read_u16_le(record + base + 0x08);
				move1 = ww_read_u16_le(record + base + 0x0A);
				move2 = ww_read_u16_le(record + base + 0x0C);
				move3 = ww_read_u16_le(record + base + 0x0E);
				min_steps = ww_read_u16_le(record + base + 0x10);
				chance_raw = ww_read_u16_le(record + base + 0x12);
				chance = chance_raw > 0xFFu ? 0xFFu : (u8)chance_raw;
			} else {
				species_id = send_context->source_slot.species_id;
				if (species_id == 0)
					species_id = 1;
			}
		}

		if (slot_level == 0)
			slot_level = 1;
		if (slot_level > 100)
			slot_level = 100;

		if (!ww_json_append(
					&cursor,
					&remaining,
					"%s{\"slot\":%u,\"sourcePairIndex\":%u,\"speciesId\":%u,\"level\":%u,\"gender\":%u,\"moves\":[%u,%u,%u,%u],\"minSteps\":%u,\"chance\":%u}",
					group == 0 ? "" : ",",
					(unsigned)group,
					(unsigned)source_pair_index,
					(unsigned)species_id,
					(unsigned)slot_level,
					(unsigned)gender,
					(unsigned)move0,
					(unsigned)move1,
					(unsigned)move2,
					(unsigned)move3,
					(unsigned)min_steps,
					(unsigned)chance)) {
			return false;
		}
	}

	if (!ww_json_append(&cursor, &remaining, "],\"items\":["))
		return false;

	for (item_index = 0; item_index < WW_OV112_ITEM_COUNT; item_index++) {
		u32 base = WW_OV112_ITEMS_OFFSET + item_index * WW_OV112_ITEM_SIZE;
		u16 item_id = ww_read_u16_le(record + base + 0x00);
		u16 min_steps = ww_read_u16_le(record + base + 0x02);
		u16 chance_raw = ww_read_u16_le(record + base + 0x04);
		u8 chance = chance_raw > 0xFFu ? 0xFFu : (u8)chance_raw;

		if (!ww_json_append(
					&cursor,
					&remaining,
					"%s{\"routeItemIndex\":%u,\"itemId\":%u,\"minSteps\":%u,\"chance\":%u}",
					item_index == 0 ? "" : ",",
					(unsigned)item_index,
					(unsigned)item_id,
					(unsigned)min_steps,
					(unsigned)chance)) {
			return false;
		}
	}

	if (!ww_json_append(&cursor, &remaining, "]}}"))
		return false;

	return true;
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

	ww_save_ui_config();
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

				ww_async_progress_set(5, "Reading save context");

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
				ww_async_progress_set(12, "Syncing trainer with API");

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
				ww_async_progress_set(22, "Trainer synced");
				ww_async_progress_set(28, "Syncing steps with API");

				success = ww_api_command_set_steps(send_context.pokewalker_steps, NULL, NULL, 0);
				if (!success) {
					snprintf(json, WW_API_RESPONSE_MAX, "failed to seed EEPROM steps from HGSS save");
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
			case WW_TASK_HGSS_STROLL_RETURN_GUIDED_APPLY: {
				hgss_stroll_return_report base_report;
				hgss_box_slot_summary verify_source_slot;
				char patch_error[128];
				char verify_error[128];
				char api_error[96];
				char api_detail[128];
				char capture_summary[384];
				char *return_json = NULL;
				char *sync_json = NULL;
				u32 api_return_species = pending_return_expected_species;
				u32 api_exp_gain = pending_return_exp_gain;
				u8 captures_written = 0;
				u8 captures_skipped = 0;
				u8 i;

				capture_summary[0] = '\0';
				api_error[0] = '\0';
				api_detail[0] = '\0';

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

				ww_async_progress_set(4, "Requesting return from API");
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

				if (!ww_json_get_u32_after_token(return_json, "\"returnedPokemon\"", "speciesId", &api_return_species)
						|| api_return_species == 0
						|| api_return_species > 0xFFFFu) {
					api_return_species = pending_return_expected_species;
				}

				if (!ww_json_get_u32_after_token(return_json, "\"returnedPokemon\"", "expGainApplied", &api_exp_gain)
						&& !ww_json_get_u32_after_token(return_json, "\"returnedPokemon\"", "expGain", &api_exp_gain)) {
					api_exp_gain = pending_return_exp_gain;
				}

				ww_async_progress_set(12, "Fetching sync package");
				if (!ww_api_get_sync_package(sync_json, WW_API_RESPONSE_MAX)) {
					snprintf(json, WW_API_RESPONSE_MAX, "guided return sync fetch failed");
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

				ww_async_progress_set(20, "Applying returned Pokemon");
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
							pending_return_sync_steps,
							pending_return_sync_watts,
							pending_return_sync_flags,
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

				ww_async_progress_set(100, "Return completed");
				snprintf(
						json,
						WW_API_RESPONSE_MAX,
						"Guided return applied to %s | returned %u to box %lu slot %lu | EXP +%lu | captures written=%u skipped=%u%s | steps %lu watts %lu flags 0x%08lX",
						pending_save_path,
						(unsigned)api_return_species,
						(unsigned long)pending_return_box,
						(unsigned long)pending_return_source_slot,
						(unsigned long)api_exp_gain,
						(unsigned)captures_written,
						(unsigned)captures_skipped,
						capture_summary,
						(unsigned long)pending_return_sync_steps,
						(unsigned long)pending_return_sync_watts,
						(unsigned long)pending_return_sync_flags);
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
	ww_async_progress_set(0, "Starting");

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
	LightLock_Unlock(&g_ww_async.lock);

	if (thread) {
		threadJoin(thread, U64_MAX);
		threadFree(thread);
	}
}

static void ww_trim_ascii(char *text)
{
	char *start;
	char *end;

	if (!text)
		return;

	start = text;
	while (*start && isspace((unsigned char)*start))
		start++;

	if (start != text)
		memmove(text, start, strlen(start) + 1);

	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
}

static bool ww_save_ui_config(void)
{
	FILE *f;
	u16 port = ww_api_get_port();

	mkdir("sdmc:/3ds", 0777);
	mkdir(WW_UI_CONFIG_DIR, 0777);

	f = fopen(WW_UI_CONFIG_PATH, "wb");
	if (!f)
		return false;

	fprintf(f, "host=%s\n", g_wearwalker_host);
	fprintf(f, "port=%u\n", (unsigned)port);
	fprintf(f, "save_path=%s\n", g_selected_hgss_save_path);
	fprintf(f, "rom_path=%s\n", g_selected_hgss_nds_path);
	fprintf(f, "simple_mode=%u\n", g_simple_mode ? 1u : 0u);

	fclose(f);
	return true;
}

static void ww_load_ui_config(void)
{
	FILE *f;
	char line[WW_SAVE_PATH_MAX + 96];
	u16 loaded_port = ww_api_get_port();

	f = fopen(WW_UI_CONFIG_PATH, "rb");
	if (!f)
		return;

	while (fgets(line, sizeof(line), f) != NULL) {
		char *eq = strchr(line, '=');
		char *key;
		char *value;

		if (!eq)
			continue;

		*eq = '\0';
		key = line;
		value = eq + 1;

		ww_trim_ascii(key);
		ww_trim_ascii(value);

		if (strcmp(key, "host") == 0) {
			if (value[0]) {
				snprintf(g_wearwalker_host, sizeof(g_wearwalker_host), "%s", value);
			}
		} else if (strcmp(key, "port") == 0) {
			char *endptr = NULL;
			unsigned long parsed = strtoul(value, &endptr, 10);
			if (endptr != value && parsed >= 1 && parsed <= 65535)
				loaded_port = (u16)parsed;
		} else if (strcmp(key, "save_path") == 0) {
			snprintf(g_selected_hgss_save_path, sizeof(g_selected_hgss_save_path), "%s", value);
		} else if (strcmp(key, "rom_path") == 0) {
			snprintf(g_selected_hgss_nds_path, sizeof(g_selected_hgss_nds_path), "%s", value);
		} else if (strcmp(key, "simple_mode") == 0) {
			if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0)
				g_simple_mode = false;
			else
				g_simple_mode = true;
		}
	}

	fclose(f);

	if (!g_wearwalker_host[0])
		snprintf(g_wearwalker_host, sizeof(g_wearwalker_host), "%s", WW_API_DEFAULT_HOST);

	if (!ww_api_set_endpoint(g_wearwalker_host, loaded_port)) {
		snprintf(g_wearwalker_host, sizeof(g_wearwalker_host), "%s", WW_API_DEFAULT_HOST);
		loaded_port = WW_API_DEFAULT_PORT;
		ww_api_set_endpoint(g_wearwalker_host, loaded_port);
	}

	ww_sync_port_entries(loaded_port);
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

enum {
	SEND_SIMPLE_MENU_PICK_SLOT = 0,
	SEND_SIMPLE_MENU_SHOW_SLOT,
	SEND_SIMPLE_MENU_ROUTE,
	SEND_SIMPLE_MENU_SEND,
};

enum {
	RETURN_SIMPLE_MENU_PICK_SLOT = 0,
	RETURN_SIMPLE_MENU_SHOW_SLOT,
	RETURN_SIMPLE_MENU_APPLY,
};

enum {
	SETTINGS_MENU_TOGGLE_MODE = 0,
	SETTINGS_MENU_SET_HOST,
	SETTINGS_MENU_PORT,
	SETTINGS_MENU_APPLY_ENDPOINT,
	SETTINGS_MENU_SELECT_SAVE,
	SETTINGS_MENU_SHOW_SAVE,
	SETTINGS_MENU_SELECT_ROM,
	SETTINGS_MENU_SHOW_ROM,
	SETTINGS_MENU_SAVE,
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
	{"Auto captures", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 3}},
	{"Increment trip counter (0/1)", ENTRY_NUMATTR, .num_attr = {.value = 1, .min = 0, .max = 1}},
	{"Apply stroll return and patch save", ENTRY_ACTION, .callback = call_stroll_return_to_save},
};

menu hgss_stroll_return_menu = {
	.title = "Stroll -> HGSS Return",
	.entries = hgss_stroll_return_menu_entries,
	.props = {.len = sizeof(hgss_stroll_return_menu_entries) / sizeof(hgss_stroll_return_menu_entries[0]), .selected = 0},
};

menu_entry hgss_stroll_send_simple_menu_entries[] = {
	{"Choose source slot (visual)", ENTRY_ACTION, .callback = call_pick_stroll_send_slot},
	{"Inspect selected source slot", ENTRY_ACTION, .callback = call_show_stroll_send_slot},
	{"Route/course id (0-26)", ENTRY_NUMATTR, .num_attr = {.value = 0, .min = 0, .max = 26}},
	{"Send to stroll", ENTRY_ACTION, .callback = call_stroll_send_from_save},
};

menu hgss_stroll_send_simple_menu = {
	.title = "Simple Send",
	.entries = hgss_stroll_send_simple_menu_entries,
	.props = {.len = sizeof(hgss_stroll_send_simple_menu_entries) / sizeof(hgss_stroll_send_simple_menu_entries[0]), .selected = 0},
};

menu_entry hgss_stroll_return_simple_menu_entries[] = {
	{"Choose source slot (visual)", ENTRY_ACTION, .callback = call_pick_stroll_return_slot},
	{"Inspect selected source slot", ENTRY_ACTION, .callback = call_show_stroll_return_slot},
	{"Apply stroll return", ENTRY_ACTION, .callback = call_stroll_return_to_save},
};

menu hgss_stroll_return_simple_menu = {
	.title = "Simple Return",
	.entries = hgss_stroll_return_simple_menu_entries,
	.props = {.len = sizeof(hgss_stroll_return_simple_menu_entries) / sizeof(hgss_stroll_return_simple_menu_entries[0]), .selected = 0},
};

menu_entry settings_menu_entries[] = {
	{"Toggle UI mode (Simple/Debug)", ENTRY_ACTION, .callback = call_toggle_simple_mode},
	{"Set backend host", ENTRY_ACTION, .callback = call_set_wearwalker_host},
	{"Backend port", ENTRY_NUMATTR, .num_attr = {.value = WW_API_DEFAULT_PORT, .min = 1, .max = 65535}},
	{"Apply endpoint", ENTRY_ACTION, .callback = call_apply_settings_endpoint},
	{"Select HGSS save (.sav)", ENTRY_ACTION, .callback = call_select_hgss_save},
	{"Show selected save path", ENTRY_ACTION, .callback = call_show_hgss_save_path},
	{"Select HGSS ROM (.nds)", ENTRY_ACTION, .callback = call_select_hgss_rom},
	{"Show selected ROM path", ENTRY_ACTION, .callback = call_show_hgss_rom_path},
	{"Save settings now", ENTRY_ACTION, .callback = call_save_ui_config_now},
};

menu settings_menu = {
	.title = "Settings",
	.entries = settings_menu_entries,
	.props = {.len = sizeof(settings_menu_entries) / sizeof(settings_menu_entries[0]), .selected = 0},
};

menu_entry main_menu_simple_entries[] = {
	{"HGSS stroll send", ENTRY_ACTION, .callback = call_start_guided_send},
	{"HGSS stroll return", ENTRY_ACTION, .callback = call_start_guided_return},
	{"Settings", ENTRY_CHANGEMENU, .new_menu = &settings_menu},
};

menu main_menu_simple = {
	.title = "Main menu (Simple)",
	.entries = main_menu_simple_entries,
	.props = {.len = sizeof(main_menu_simple_entries) / sizeof(main_menu_simple_entries[0]), .selected = 0},
};

menu_entry main_menu_debug_entries[] = {
	{"WearWalker API test menu", ENTRY_CHANGEMENU, .new_menu = &wearwalker_wifi_menu},
	{"HGSS save sync/patch", ENTRY_CHANGEMENU, .new_menu = &hgss_patch_menu},
	{"HGSS stroll send from box", ENTRY_CHANGEMENU, .new_menu = &hgss_stroll_send_menu},
	{"HGSS stroll return to save", ENTRY_CHANGEMENU, .new_menu = &hgss_stroll_return_menu},
	{"Settings", ENTRY_CHANGEMENU, .new_menu = &settings_menu},
};

menu main_menu_debug = {
	.title = "Main menu (Debug)",
	.entries = main_menu_debug_entries,
	.props = {.len = sizeof(main_menu_debug_entries) / sizeof(main_menu_debug_entries[0]), .selected = 0},
};

static menu *ww_get_main_menu(void)
{
	return g_simple_mode ? &main_menu_simple : &main_menu_debug;
}

static void ww_sync_port_entries(u16 port)
{
	wearwalker_wifi_menu_entries[WW_MENU_PORT].num_attr.value = port;
	hgss_stroll_send_menu_entries[SEND_MENU_PORT].num_attr.value = port;
	hgss_stroll_return_menu_entries[RETURN_MENU_PORT].num_attr.value = port;
	settings_menu_entries[SETTINGS_MENU_PORT].num_attr.value = port;
}

static bool ww_apply_endpoint_from_ui(u16 port)
{
	if (!ww_api_set_endpoint(g_wearwalker_host, port)) {
		printf("Invalid WearWalker endpoint\n");
		return false;
	}

	ww_sync_port_entries(port);
	printf("Using WearWalker endpoint %s:%lu\n", g_wearwalker_host, (unsigned long)port);
	ww_save_ui_config();
	return true;
}


// Currently active menu
static menu *g_active_menu = &main_menu_simple;
static enum state g_state = IN_MENU;
static C3D_RenderTarget *target;
static C3D_RenderTarget *target_top;
static C2D_TextBuf textbuf;
static PrintConsole g_header_console;
static PrintConsole logs;
static bool g_debug_console_ready = false;

static void ww_init_debug_console(void)
{
	if (g_debug_console_ready)
		return;

	consoleInit(GFX_TOP, &g_header_console);
	consoleInit(GFX_TOP, &logs);
	consoleSetWindow(&g_header_console, 0, 0, g_header_console.consoleWidth, 6);
	consoleSetWindow(&logs, 0, 6, logs.consoleWidth, logs.consoleHeight - 6);
	g_debug_console_ready = true;
}

static void ww_clear_debug_console(void)
{
	if (!g_debug_console_ready)
		return;

	consoleSelect(&g_header_console);
	consoleClear();
	consoleSelect(&logs);
	consoleClear();
}

static void ww_path_basename(const char *path, char *out, size_t out_size)
{
	const char *base;

	if (!out || out_size == 0)
		return;

	if (!path || !path[0]) {
		snprintf(out, out_size, "(none)");
		return;
	}

	base = strrchr(path, '/');
	if (!base || !base[1])
		snprintf(out, out_size, "%s", path);
	else
		snprintf(out, out_size, "%s", base + 1);
}

static void ww_draw_top_context(void)
{
	char save_name[48];
	char rom_name[48];
	const char *panel_state;

	if (g_simple_mode || !g_console_enabled || !g_debug_console_ready)
		return;

	ww_path_basename(g_selected_hgss_save_path, save_name, sizeof(save_name));
	ww_path_basename(g_selected_hgss_nds_path, rom_name, sizeof(rom_name));

	if (g_state == IN_BOX_SELECTOR)
		panel_state = "Visual box selector";
	else if (g_state == IN_RETURN_SELECTOR)
		panel_state = "Guided return flow";
	else if (g_state == IN_FILE_BROWSER)
		panel_state = "File browser";
	else
		panel_state = g_active_menu ? g_active_menu->title : "Menu";

	consoleSelect(&g_header_console);
	printf("\x1b[2J\x1b[0;0H");
	printf("WearWalker v%s | %s mode\n", VER, g_simple_mode ? "Simple" : "Debug");
	printf("%s:%u\n", ww_api_get_host(), (unsigned)ww_api_get_port());
	printf("SAVE %s\n", save_name);
	printf("ROM  %s\n", rom_name);
	printf("%s\n", panel_state);
	if (g_state == IN_BOX_SELECTOR)
		printf("Box %lu Slot %lu (L/R change box)\n", (unsigned long)g_box_picker_box, (unsigned long)g_box_picker_slot);
	else
		printf("\n");

	consoleSelect(&logs);
}

void ui_init()
{
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

	g_console_enabled = false;
	g_debug_console_ready = false;

	strncpy(g_wearwalker_host, ww_api_get_host(), sizeof(g_wearwalker_host) - 1);
	g_wearwalker_host[sizeof(g_wearwalker_host) - 1] = '\0';
	g_simple_mode = true;
	g_selected_hgss_save_path[0] = '\0';
	g_selected_hgss_nds_path[0] = '\0';
	ww_sync_port_entries(ww_api_get_port());
	ww_load_ui_config();
	g_console_enabled = !g_simple_mode;
	g_active_menu = ww_get_main_menu();
	g_active_menu->props.selected = 0;
	if (g_console_enabled) {
		ww_init_debug_console();
		ww_clear_debug_console();
		consoleSelect(&g_header_console);
		printf("WearWalker Bridge Test v%s\n", VER);
		consoleSelect(&logs);
	}
	printf("UI mode: %s\n", g_simple_mode ? "Simple" : "Debug");
	g_pending_steps = wearwalker_wifi_menu_entries[WW_MENU_CMD_STEPS_VALUE].num_attr.value;
	g_pending_watts = wearwalker_wifi_menu_entries[WW_MENU_CMD_WATTS_VALUE].num_attr.value;
	g_pending_sync = wearwalker_wifi_menu_entries[WW_MENU_CMD_SYNC_VALUE].num_attr.value;
	snprintf(g_pending_trainer, sizeof(g_pending_trainer), "%s", g_wearwalker_trainer);
	g_pending_save_path[0] = '\0';
	g_pending_nds_path[0] = '\0';
	g_pending_hgss_steps = hgss_patch_menu_entries[HGSS_MENU_MANUAL_STEPS].num_attr.value;
	g_pending_hgss_watts = hgss_patch_menu_entries[HGSS_MENU_MANUAL_WATTS].num_attr.value;
	g_pending_hgss_course_flags = hgss_patch_menu_entries[HGSS_MENU_MANUAL_FLAGS].num_attr.value;
	g_pending_increment_trip_counter = hgss_patch_menu_entries[HGSS_MENU_TRIP_COUNTER].num_attr.value != 0;
	g_pending_send_box = hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value;
	g_pending_send_slot = hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value;
	g_pending_send_course = hgss_stroll_send_menu_entries[SEND_MENU_ROUTE].num_attr.value;
	g_route_selector_course = g_pending_send_course;
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

	target_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	textbuf = C2D_TextBufNew(256);
}

void ui_exit()
{
	ww_save_ui_config();
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

static void ww_draw_string_width(
		float x,
		float y,
		float size,
		const char *str,
		bool centered,
		int flags,
		float viewport_width,
		u32 color)
{
	C2D_Text text;
	float scale;

	C2D_TextBufClear(textbuf);
	C2D_TextParse(&text, textbuf, str);
	scale = size / 30;
	x = centered ? (viewport_width - text.width * scale) / 2 : x;
	x = x < 0 ? viewport_width - text.width * scale + x : x;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | flags, x, y, 0.0f, scale, scale, color);
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

static bool ww_is_simple_main_menu_active(void)
{
	return g_simple_mode && g_state == IN_MENU && g_active_menu == &main_menu_simple;
}

static bool ww_handle_simple_main_menu_input(u32 kDown)
{
	u16 selected;
	u16 next;

	if (!ww_is_simple_main_menu_active())
		return false;

	selected = g_active_menu->props.selected;
	next = selected;

	if (kDown & KEY_LEFT)
		next = 0;
	else if (kDown & KEY_RIGHT)
		next = 1;
	else if (kDown & KEY_DOWN)
		next = 2;
	else if ((kDown & KEY_UP) && selected == 2)
		next = 0;
	else
		return false;

	g_active_menu->props.selected = next;
	return true;
}

static void ww_draw_simple_main_menu(void)
{
	const float side_pad = 10.0f;
	const float center_gap = 10.0f;
	const float card_y = 34.0f;
	const float card_h = 164.0f;
	const float card_w = (SCREEN_WIDTH - side_pad * 2.0f - center_gap) * 0.5f;
	const float left_x = side_pad;
	const float right_x = side_pad + card_w + center_gap;
	const float settings_w = 92.0f;
	const float settings_h = 22.0f;
	const float settings_x = SCREEN_WIDTH - settings_w - 10.0f;
	const float settings_y = 6.0f;
	const bool send_selected = g_active_menu->props.selected == 0;
	const bool receive_selected = g_active_menu->props.selected == 1;
	const bool settings_selected = g_active_menu->props.selected == 2;
	const u32 send_border = send_selected ? C2D_Color32(0xB6, 0xF0, 0xD2, 0xFF) : C2D_Color32(0x2E, 0x78, 0x63, 0xFF);
	const u32 send_fill = send_selected ? C2D_Color32(0x2E, 0x87, 0x6B, 0xFF) : C2D_Color32(0x1B, 0x4E, 0x42, 0xFF);
	const u32 receive_border = receive_selected ? C2D_Color32(0xF5, 0xDE, 0x9E, 0xFF) : C2D_Color32(0x7A, 0x5B, 0x22, 0xFF);
	const u32 receive_fill = receive_selected ? C2D_Color32(0x9A, 0x6E, 0x28, 0xFF) : C2D_Color32(0x5A, 0x42, 0x18, 0xFF);
	const u32 settings_border = settings_selected ? C2D_Color32(0xB8, 0xE0, 0xF0, 0xFF) : C2D_Color32(0x3A, 0x63, 0x77, 0xFF);
	const u32 settings_fill = settings_selected ? C2D_Color32(0x3B, 0x79, 0x95, 0xFF) : C2D_Color32(0x24, 0x49, 0x5B, 0xFF);

	C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, C2D_Color32(0x0D, 0x1F, 0x27, 0xFF));
	C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, SCREEN_WIDTH, 28.0f, C2D_Color32(0x14, 0x37, 0x45, 0xFF));
	C2D_DrawRectSolid(0.0f, 210.0f, 0.0f, SCREEN_WIDTH, 30.0f, C2D_Color32(0x12, 0x2C, 0x38, 0xFF));

	C2D_DrawRectSolid(settings_x - 1.0f, settings_y - 1.0f, 0.0f, settings_w + 2.0f, settings_h + 2.0f, settings_border);
	C2D_DrawRectSolid(settings_x, settings_y, 0.0f, settings_w, settings_h, settings_fill);
	draw_string(settings_x + 15.0f, settings_y + 4.0f, 9.0f, "SETTINGS", false, 0);

	draw_string(10.0f, 7.0f, 11.5f, "Quick Actions", false, 0);
	draw_string(10.0f, 20.0f, 8.1f, "Left/Right choose card, Down highlights settings", false, 0);

	C2D_DrawRectSolid(left_x - 2.0f, card_y - 2.0f, 0.0f, card_w + 4.0f, card_h + 4.0f, send_border);
	C2D_DrawRectSolid(left_x, card_y, 0.0f, card_w, card_h, send_fill);
	C2D_DrawRectSolid(left_x + 8.0f, card_y + 10.0f, 0.0f, card_w - 16.0f, 24.0f, C2D_Color32(0x44, 0xA1, 0x84, 0xFF));
	draw_string(left_x + 42.0f, card_y + 14.0f, 14.2f, "SEND", false, 0);
	draw_string(left_x + 12.0f, card_y + 56.0f, 9.2f, "Pick one Pokemon from", false, 0);
	draw_string(left_x + 12.0f, card_y + 72.0f, 9.2f, "your HGSS box and", false, 0);
	draw_string(left_x + 12.0f, card_y + 88.0f, 9.2f, "send it to Pokewalker.", false, 0);
	draw_string(left_x + 12.0f, card_y + 122.0f, 8.5f, "Source slot and route", false, 0);
	draw_string(left_x + 12.0f, card_y + 136.0f, 8.5f, "selection are guided.", false, 0);

	C2D_DrawRectSolid(right_x - 2.0f, card_y - 2.0f, 0.0f, card_w + 4.0f, card_h + 4.0f, receive_border);
	C2D_DrawRectSolid(right_x, card_y, 0.0f, card_w, card_h, receive_fill);
	C2D_DrawRectSolid(right_x + 8.0f, card_y + 10.0f, 0.0f, card_w - 16.0f, 24.0f, C2D_Color32(0xC2, 0x8D, 0x35, 0xFF));
	draw_string(right_x + 25.0f, card_y + 14.0f, 14.2f, "RECEIVE", false, 0);
	draw_string(right_x + 12.0f, card_y + 56.0f, 9.2f, "Bring back the walking", false, 0);
	draw_string(right_x + 12.0f, card_y + 72.0f, 9.2f, "Pokemon and apply", false, 0);
	draw_string(right_x + 12.0f, card_y + 88.0f, 9.2f, "captured encounters.", false, 0);
	draw_string(right_x + 12.0f, card_y + 122.0f, 8.5f, "Guided flow with slot", false, 0);
	draw_string(right_x + 12.0f, card_y + 136.0f, 8.5f, "and capture placement.", false, 0);

	draw_string(10.0f, 218.0f, 8.7f, "A: open selected action", false, 0);
	draw_string(186.0f, 218.0f, 8.7f, "START: exit", false, 0);
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

static bool ww_box_selector_reload(void)
{
	u32 slot_index;
	char slot_error[96];
	hgss_stroll_send_context context;

	if (!g_pending_save_path[0]) {
		g_box_picker_reload_ok = false;
		snprintf(g_box_picker_error, sizeof(g_box_picker_error), "Select a HGSS save first");
		for (slot_index = 0; slot_index < HGSS_BOX_SLOTS; slot_index++) {
			memset(&g_box_picker_slots[slot_index], 0, sizeof(g_box_picker_slots[slot_index]));
			g_box_picker_context_valid[slot_index] = false;
		}
		return false;
	}

	g_box_picker_reload_ok = true;
	g_box_picker_error[0] = '\0';
	g_box_picker_sprite_species = 0xFFFF;
	g_box_picker_sprite_ready = false;

	for (slot_index = 0; slot_index < HGSS_BOX_SLOTS; slot_index++) {
		if (hgss_read_stroll_send_context(
					g_pending_save_path,
					(u8)g_box_picker_box,
					(u8)(slot_index + 1),
					&context,
					slot_error,
					sizeof(slot_error))) {
			g_box_picker_slots[slot_index] = context.source_slot;
			g_box_picker_context[slot_index] = context;
			g_box_picker_context_valid[slot_index] = true;
			continue;
		}

		g_box_picker_context_valid[slot_index] = false;
		if (!hgss_read_box_slot_summary(
						g_pending_save_path,
						(u8)g_box_picker_box,
						(u8)(slot_index + 1),
						&g_box_picker_slots[slot_index],
						slot_error,
						sizeof(slot_error))) {
			g_box_picker_reload_ok = false;
			memset(&g_box_picker_slots[slot_index], 0, sizeof(g_box_picker_slots[slot_index]));
		}
	}

	if (!g_box_picker_reload_ok)
		snprintf(g_box_picker_error, sizeof(g_box_picker_error), "Unable to inspect one or more slots");

	return g_box_picker_reload_ok;
}

static bool ww_box_selector_open(ww_box_picker_mode mode)
{
	if (!ww_prepare_selected_save_path())
		return false;

	g_box_picker_mode = mode;

	if (mode == WW_BOX_PICKER_SEND_SOURCE) {
		g_box_picker_box = hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value;
		g_box_picker_slot = hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value;
	} else if (mode == WW_BOX_PICKER_RETURN_SOURCE) {
		g_box_picker_box = hgss_stroll_return_menu_entries[RETURN_MENU_BOX].num_attr.value;
		g_box_picker_slot = hgss_stroll_return_menu_entries[RETURN_MENU_SOURCE_SLOT].num_attr.value;
	} else if (mode == WW_BOX_PICKER_RETURN_CAPTURE_1
			|| mode == WW_BOX_PICKER_RETURN_CAPTURE_2
			|| mode == WW_BOX_PICKER_RETURN_CAPTURE_3) {
		u8 capture_index = (u8)(mode - WW_BOX_PICKER_RETURN_CAPTURE_1);

		if (capture_index < WW_RETURN_CAPTURE_MAX
				&& g_return_captures[capture_index].target_box >= 1
				&& g_return_captures[capture_index].target_box <= HGSS_BOX_COUNT
				&& g_return_captures[capture_index].target_slot >= 1
				&& g_return_captures[capture_index].target_slot <= HGSS_BOX_SLOTS) {
			g_box_picker_box = g_return_captures[capture_index].target_box;
			g_box_picker_slot = g_return_captures[capture_index].target_slot;
		} else {
			g_box_picker_box = g_pending_return_box >= 1 && g_pending_return_box <= HGSS_BOX_COUNT
					? g_pending_return_box
					: 1;
			g_box_picker_slot = 1;
		}
	}

	if (g_box_picker_box < 1 || g_box_picker_box > HGSS_BOX_COUNT)
		g_box_picker_box = 1;
	if (g_box_picker_slot < 1 || g_box_picker_slot > HGSS_BOX_SLOTS)
		g_box_picker_slot = 1;

	ww_box_selector_reload();
	g_state = IN_BOX_SELECTOR;
	return true;
}

static void ww_box_selector_apply_choice(void)
{
	if (g_box_picker_mode == WW_BOX_PICKER_SEND_SOURCE) {
		hgss_stroll_send_menu_entries[SEND_MENU_BOX].num_attr.value = g_box_picker_box;
		hgss_stroll_send_menu_entries[SEND_MENU_SLOT].num_attr.value = g_box_picker_slot;
		g_pending_send_box = g_box_picker_box;
		g_pending_send_slot = g_box_picker_slot;
	} else if (g_box_picker_mode == WW_BOX_PICKER_RETURN_SOURCE) {
		hgss_stroll_return_menu_entries[RETURN_MENU_BOX].num_attr.value = g_box_picker_box;
		hgss_stroll_return_menu_entries[RETURN_MENU_SOURCE_SLOT].num_attr.value = g_box_picker_slot;
		g_pending_return_box = g_box_picker_box;
		g_pending_return_source_slot = g_box_picker_slot;
	} else if (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_1
			|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_2
			|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_3) {
		u8 capture_index = (u8)(g_box_picker_mode - WW_BOX_PICKER_RETURN_CAPTURE_1);

		if (capture_index < WW_RETURN_CAPTURE_MAX) {
			g_return_captures[capture_index].target_box = (u8)g_box_picker_box;
			g_return_captures[capture_index].target_slot = (u8)g_box_picker_slot;
		}
	}
}

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

static void ww_draw_2bpp_sprite(
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

static void ww_draw_indexed_icon(
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

static void ww_draw_item_token_sprite(u16 item_id, float x, float y, float scale)
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

static bool ww_route_selector_reload(void)
{
	u8 course_table[WW_OV112_COURSE_TABLE_SIZE];
	const u8 *record;
	u8 *small_narc_data = NULL;
	u32 small_narc_size = 0;
	u8 *item_icon_narc_data = NULL;
	u32 item_icon_narc_size = 0;
	u8 *poke_icon_narc_data = NULL;
	u32 poke_icon_narc_size = 0;
	u32 route_plus_one;
	u32 route_image_index;
	u32 slot_index;
	u32 group;
	u32 item_index;
	u32 preview_seed;
	s32 watts_required;

	g_route_selector_ready = false;
	g_route_selector_error[0] = '\0';
	g_route_preview_area_ready = false;
	g_route_selector_locked = false;
	g_route_selector_special_lock = false;
	g_route_selector_required_watts = 0;
	g_route_selector_current_watts = g_guided_send_context.pokewalker_watts;
	g_route_send_busy = false;
	g_route_selector_preview_seed = 0;

	for (slot_index = 0; slot_index < WW_ROUTE_PREVIEW_SLOT_COUNT; slot_index++)
		g_route_preview_selected_group[slot_index] = -1;
	for (group = 0; group < WW_ROUTE_SELECTED_SLOT_COUNT; group++)
		g_route_preview_selected_slots[group] = (u8)(group * 2u);

	if (!g_guided_send_context_ready) {
		snprintf(g_route_selector_error, sizeof(g_route_selector_error), "Select a Pokemon first");
		return false;
	}

	if (!ww_prepare_selected_nds_path()) {
		snprintf(g_route_selector_error, sizeof(g_route_selector_error), "Set HGSS ROM in Settings");
		return false;
	}

	if (g_route_selector_course >= WW_OV112_COURSE_COUNT)
		g_route_selector_course = 0;

	watts_required = g_route_course_watts_required[g_route_selector_course];
	g_route_selector_special_lock = watts_required < 0;
	if (!g_route_selector_special_lock)
		g_route_selector_required_watts = watts_required;
	g_route_selector_locked = g_route_selector_special_lock
			|| (!g_route_selector_special_lock && (u32)g_route_selector_required_watts > g_route_selector_current_watts);

	if (!ww_get_course_table_cached(g_pending_nds_path, course_table, NULL)) {
		snprintf(g_route_selector_error, sizeof(g_route_selector_error), "Unable to read route table from ROM");
		return false;
	}

	record = course_table + g_route_selector_course * WW_OV112_COURSE_RECORD_SIZE;
	route_plus_one = ww_read_u32_le(record + 0x04);
	if (route_plus_one > 0 && route_plus_one <= WW_OV112_ROUTE_IMAGE_COUNT) {
		route_image_index = route_plus_one - 1u;
	} else {
		u32 route_plus_one_low = route_plus_one & 0xFFu;

		if (route_plus_one_low > 0 && route_plus_one_low <= WW_OV112_ROUTE_IMAGE_COUNT)
			route_image_index = route_plus_one_low - 1u;
		else if (g_route_selector_course < WW_OV112_ROUTE_IMAGE_COUNT)
			route_image_index = g_route_selector_course;
		else
			route_image_index = g_route_selector_course % WW_OV112_ROUTE_IMAGE_COUNT;
	}

	g_route_preview_area_ready = ww_extract_route_area_sprite_from_nds(
			g_pending_nds_path,
			route_image_index,
			g_route_preview_area_sprite);

	g_route_preview_adv_types[0] = record[WW_OV112_ADV_TYPES_OFFSET + 0u];
	g_route_preview_adv_types[1] = record[WW_OV112_ADV_TYPES_OFFSET + 1u];
	g_route_preview_adv_types[2] = record[WW_OV112_ADV_TYPES_OFFSET + 2u];

	ww_load_narc_from_nds(g_pending_nds_path, "a/2/4/8", &small_narc_data, &small_narc_size);
	ww_load_narc_from_nds(g_pending_nds_path, "a/0/1/8", &item_icon_narc_data, &item_icon_narc_size);
	if (!item_icon_narc_data
			&& !ww_load_narc_from_nds(g_pending_nds_path, "itemtool/itemdata/item_icon", &item_icon_narc_data, &item_icon_narc_size))
		ww_load_narc_from_nds(g_pending_nds_path, "itemtool/itemdata/item_icon.narc", &item_icon_narc_data, &item_icon_narc_size);

	ww_load_narc_from_nds(g_pending_nds_path, "a/0/2/0", &poke_icon_narc_data, &poke_icon_narc_size);
	if (!poke_icon_narc_data
			&& !ww_load_narc_from_nds(
						g_pending_nds_path,
						"poketool/icongra/poke_icon/poke_icon",
						&poke_icon_narc_data,
						&poke_icon_narc_size)) {
		ww_load_narc_from_nds(
				g_pending_nds_path,
				"poketool/icongra/poke_icon/poke_icon.narc",
				&poke_icon_narc_data,
				&poke_icon_narc_size);
	}

	for (slot_index = 0; slot_index < WW_ROUTE_PREVIEW_SLOT_COUNT; slot_index++) {
		u32 base = WW_OV112_SLOT_OFFSET + slot_index * WW_OV112_SLOT_SIZE;
		u16 species_id = ww_read_u16_le(record + base + 0x00);
		u8 level = record[base + 0x02];
		u16 move0 = ww_read_u16_le(record + base + 0x08);
		u16 move1 = ww_read_u16_le(record + base + 0x0A);
		u16 move2 = ww_read_u16_le(record + base + 0x0C);
		u16 move3 = ww_read_u16_le(record + base + 0x0E);
		u16 min_steps = ww_read_u16_le(record + base + 0x10);
		u16 chance_raw = ww_read_u16_le(record + base + 0x12);
		u8 chance = chance_raw > 100 ? 100 : (u8)chance_raw;

		g_route_preview_slots[slot_index].species_id = species_id;
		g_route_preview_slots[slot_index].level = level == 0 ? 1 : (level > 100 ? 100 : level);
		g_route_preview_slots[slot_index].min_steps = min_steps;
		g_route_preview_slots[slot_index].chance = chance;
		g_route_preview_slots[slot_index].moves[0] = move0;
		g_route_preview_slots[slot_index].moves[1] = move1;
		g_route_preview_slots[slot_index].moves[2] = move2;
		g_route_preview_slots[slot_index].moves[3] = move3;
		snprintf(
				g_route_preview_slots[slot_index].species_name,
				sizeof(g_route_preview_slots[slot_index].species_name),
				"%s",
				ww_lookup_species_name(species_id));
		g_route_preview_slots[slot_index].sprite_ready = false;
		g_route_preview_slots[slot_index].color_icon_ready = false;
		g_route_preview_slots[slot_index].color_icon_width = 0;
		g_route_preview_slots[slot_index].color_icon_height = 0;
		memset(
				g_route_preview_slots[slot_index].sprite_frame0,
				0,
				sizeof(g_route_preview_slots[slot_index].sprite_frame0));
		memset(
				g_route_preview_slots[slot_index].color_icon_indices,
				0,
				sizeof(g_route_preview_slots[slot_index].color_icon_indices));
		memset(
				g_route_preview_slots[slot_index].color_icon_palette,
				0,
				sizeof(g_route_preview_slots[slot_index].color_icon_palette));

		if (poke_icon_narc_data && species_id > 0) {
			if (ww_extract_species_color_icon_from_narc(
						poke_icon_narc_data,
						poke_icon_narc_size,
						species_id,
						g_route_preview_slots[slot_index].color_icon_indices,
						g_route_preview_slots[slot_index].color_icon_palette,
						&g_route_preview_slots[slot_index].color_icon_width,
						&g_route_preview_slots[slot_index].color_icon_height)) {
				g_route_preview_slots[slot_index].color_icon_ready = true;
			}
		}

		if (small_narc_data && species_id > 0) {
			u8 small1[WW_OV112_ROUTE_IMAGE_SIZE];

			if (ww_extract_species_small_frames(
						small_narc_data,
						small_narc_size,
						species_id,
						g_route_preview_slots[slot_index].sprite_frame0,
						small1)) {
				ww_remap_sprite_2bpp_contrast(
						g_route_preview_slots[slot_index].sprite_frame0,
						sizeof(g_route_preview_slots[slot_index].sprite_frame0));
				g_route_preview_slots[slot_index].sprite_ready = true;
			}
		}
	}

	preview_seed = g_route_selector_session_seed;
	preview_seed ^= (g_route_selector_course + 1u) * 2654435761u;
	preview_seed ^= ((u32)g_guided_send_context.source_slot.species_id << 16);
	preview_seed ^= g_guided_send_context.source_slot.exp;
	if (preview_seed == 0)
		preview_seed = 0xA5A55A5Au;
	g_route_selector_preview_seed = preview_seed;

	for (group = 0; group < WW_ROUTE_SELECTED_SLOT_COUNT; group++) {
		u32 pair_base = group * 2u;
		u32 pick = ww_lcg_pair_pick(&preview_seed);
		u32 selected_slot = pair_base + pick;
		u32 alt_slot = pair_base + (pick ^ 0x1u);

		if (g_route_preview_slots[selected_slot].species_id == 0
				&& g_route_preview_slots[alt_slot].species_id != 0)
			selected_slot = alt_slot;

		g_route_preview_selected_slots[group] = (u8)selected_slot;
		g_route_preview_selected_group[selected_slot] = (s8)group;
	}

	for (item_index = 0; item_index < WW_OV112_ITEM_COUNT; item_index++) {
		u32 base = WW_OV112_ITEMS_OFFSET + item_index * WW_OV112_ITEM_SIZE;
		u16 item_id = ww_read_u16_le(record + base + 0x00);
		u16 min_steps = ww_read_u16_le(record + base + 0x02);
		u16 chance_raw = ww_read_u16_le(record + base + 0x04);
		u8 chance = chance_raw > 100 ? 100 : (u8)chance_raw;

		g_route_preview_items[item_index].item_id = item_id;
		g_route_preview_items[item_index].min_steps = min_steps;
		g_route_preview_items[item_index].chance = chance;
		g_route_preview_items[item_index].icon_ready = false;
		g_route_preview_items[item_index].icon_width = 0;
		g_route_preview_items[item_index].icon_height = 0;
		memset(
				g_route_preview_items[item_index].icon_indices,
				0,
				sizeof(g_route_preview_items[item_index].icon_indices));
		memset(
				g_route_preview_items[item_index].icon_palette,
				0,
				sizeof(g_route_preview_items[item_index].icon_palette));
		snprintf(
				g_route_preview_items[item_index].item_name,
				sizeof(g_route_preview_items[item_index].item_name),
				"%s",
				ww_lookup_item_name(item_id));

		if (item_icon_narc_data && item_id > 0) {
			if (ww_extract_item_icon_from_narc(
						item_icon_narc_data,
						item_icon_narc_size,
						item_id,
						g_route_preview_items[item_index].icon_indices,
						g_route_preview_items[item_index].icon_palette,
						&g_route_preview_items[item_index].icon_width,
						&g_route_preview_items[item_index].icon_height)) {
				g_route_preview_items[item_index].icon_ready = true;
			}
		}
	}

	if (small_narc_data)
		free(small_narc_data);
	if (item_icon_narc_data)
		free(item_icon_narc_data);
	if (poke_icon_narc_data)
		free(poke_icon_narc_data);

	g_route_selector_ready = true;
	return true;
}

static bool ww_route_selector_open(void)
{
	char context_error[128];

	if (!ww_prepare_selected_save_path())
		return false;
	if (!ww_prepare_selected_nds_path())
		return false;

	if (!hgss_read_stroll_send_context(
					g_pending_save_path,
					(u8)g_pending_send_box,
					(u8)g_pending_send_slot,
					&g_guided_send_context,
					context_error,
					sizeof(context_error))) {
		printf("%s\n", context_error[0] ? context_error : "failed reading source slot context");
		return false;
	}

	if (!g_guided_send_context.source_slot.occupied || g_guided_send_context.source_slot.species_id == 0) {
		printf("Selected source slot is empty\n");
		return false;
	}

	g_guided_send_context_ready = true;
	g_route_send_busy = false;
	g_pending_send_route_seed_valid = false;
	g_route_selector_session_seed = (u32)(svcGetSystemTick() & 0xFFFFFFFFu);
	g_route_selector_session_seed ^= (u32)osGetTime();
	g_route_selector_session_seed ^= g_ui_anim_tick * 3266489917u;
	if (g_route_selector_session_seed == 0)
		g_route_selector_session_seed = 0x6D2B79F5u;
	if (!ww_route_selector_reload()) {
		printf("Route preview unavailable: %s\n", g_route_selector_error[0] ? g_route_selector_error : "unknown error");
	}

	g_state = IN_ROUTE_SELECTOR;
	return true;
}

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
		draw_string(0.0f, 172.0f, 9.0f, "Please wait, returning to main menu...", true, 0);
		return;
	}

	draw_string(16.0f, 84.0f, 10.0f, "Return flow is idle.", false, 0);
}

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
	g_ui_anim_tick++;
	ww_draw_top_context();

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

	if (g_simple_mode) {
		C2D_TargetClear(target_top, C2D_Color32(0x10, 0x25, 0x2C, 0xFF));
		C2D_SceneBegin(target_top);
		ww_draw_simple_top_panel();
	}

	C2D_TargetClear(target, COLOR_BG);
	C2D_SceneBegin(target);

	if (g_state == IN_BOX_SELECTOR)
		ww_draw_box_selector();
	else if (g_state == IN_ROUTE_SELECTOR)
		ww_draw_route_selector();
	else if (g_state == IN_RETURN_SELECTOR)
		ww_draw_return_selector();
	else if (ww_is_simple_main_menu_active())
		ww_draw_simple_main_menu();
	else if (g_state == IN_FILE_BROWSER)
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
	u32 kDown = hidKeysDown() | (hidKeysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R));
	async_completed = ww_async_poll_completion();

	if (async_completed && g_return_apply_busy) {
		g_return_apply_busy = false;
		ww_return_flow_reset();
		g_state = IN_MENU;
		g_active_menu = ww_get_main_menu();
		return OP_UPDATE;
	}

	if (async_completed && g_route_send_busy) {
		g_route_send_busy = false;
		g_state = IN_MENU;
		g_active_menu = ww_get_main_menu();
		return OP_UPDATE;
	}

	if (g_return_apply_busy && g_state == IN_RETURN_SELECTOR)
		return OP_UPDATE;

	if (g_route_send_busy && g_state == IN_ROUTE_SELECTOR)
		return OP_UPDATE;

	if (kDown & KEY_START) {
		if (g_return_flow_active || g_return_apply_busy) {
			printf("Guided return in progress; complete it before exiting\n");
			return OP_UPDATE;
		}
		return OP_EXIT;
	}

	if (kDown) {
		if (g_state == IN_RETURN_SELECTOR) {
			if (g_return_step == WW_RETURN_STEP_CONFIRM) {
				if (kDown & KEY_A) {
					if (ww_return_fetch_trip_preview()) {
						if (!ww_box_selector_open(WW_BOX_PICKER_RETURN_SOURCE)) {
							printf("Unable to open box selector for return destination\n");
						} else {
							printf("Choose destination slot for returned Pokemon\n");
						}
					}
				} else if (kDown & KEY_B) {
					ww_return_flow_reset();
					g_state = IN_MENU;
					g_active_menu = ww_get_main_menu();
				}
				return OP_UPDATE;
			}

			if (g_return_step == WW_RETURN_STEP_CAPTURE_POLICY) {
				if (kDown & KEY_B) {
					if (!ww_box_selector_open(WW_BOX_PICKER_RETURN_SOURCE))
						printf("Unable to reopen destination selector\n");
					return OP_UPDATE;
				}

				if (g_return_capture_count == 0) {
					if (kDown & KEY_A)
						g_return_step = WW_RETURN_STEP_REVIEW;
					return OP_UPDATE;
				}

				if (kDown & KEY_A) {
					g_return_manual_capture_targets = false;
					ww_return_set_capture_auto_all();
					g_return_step = WW_RETURN_STEP_REVIEW;
				} else if (kDown & KEY_X) {
					u8 idx;

					g_return_manual_capture_targets = true;
					for (idx = 0; idx < g_return_capture_count && idx < WW_RETURN_CAPTURE_MAX; idx++) {
						g_return_captures[idx].target_box = 0;
						g_return_captures[idx].target_slot = 0;
					}
					if (!ww_return_open_capture_picker(0))
						printf("Unable to open capture selector\n");
				}
				return OP_UPDATE;
			}

			if (g_return_step == WW_RETURN_STEP_REVIEW) {
				if (kDown & KEY_A) {
					ww_start_guided_return_apply();
				} else if (kDown & KEY_B) {
					if (g_return_capture_count == 0)
						g_return_step = WW_RETURN_STEP_CAPTURE_POLICY;
					else
						g_return_step = WW_RETURN_STEP_CAPTURE_POLICY;
				}
				return OP_UPDATE;
			}

			return OP_UPDATE;
		}

		if (g_state == IN_ROUTE_SELECTOR) {
			if (kDown & KEY_LEFT) {
				if (g_route_selector_course > 0)
					g_route_selector_course--;
				else
					g_route_selector_course = WW_OV112_COURSE_COUNT - 1;
				ww_route_selector_reload();
			} else if (kDown & KEY_RIGHT) {
				g_route_selector_course = (g_route_selector_course + 1) % WW_OV112_COURSE_COUNT;
				ww_route_selector_reload();
			} else if (kDown & KEY_A) {
				if (g_route_selector_locked) {
					if (g_route_selector_special_lock) {
						printf("Selected route is event/special locked\n");
					} else {
						printf(
								"Route locked: need %ld watts (you have %lu)\n",
								(long)g_route_selector_required_watts,
								(unsigned long)g_route_selector_current_watts);
					}
				} else {
					g_pending_send_course = g_route_selector_course;
					hgss_stroll_send_menu_entries[SEND_MENU_ROUTE].num_attr.value = g_pending_send_course;
					call_stroll_send_from_save();
				}
			} else if (kDown & KEY_B) {
				g_state = IN_BOX_SELECTOR;
			}

			return OP_UPDATE;
		}

		if (g_state == IN_BOX_SELECTOR) {
			if (kDown & KEY_UP) {
				if (g_box_picker_slot > 6)
					g_box_picker_slot -= 6;
			} else if (kDown & KEY_DOWN) {
				if (g_box_picker_slot <= 24)
					g_box_picker_slot += 6;
			} else if (kDown & KEY_LEFT) {
				if ((g_box_picker_slot - 1) % 6 != 0)
					g_box_picker_slot--;
			} else if (kDown & KEY_RIGHT) {
				if (g_box_picker_slot % 6 != 0)
					g_box_picker_slot++;
			} else if (kDown & KEY_L) {
				if (g_box_picker_box > 1)
					g_box_picker_box--;
				else
					g_box_picker_box = HGSS_BOX_COUNT;
				ww_box_selector_reload();
			} else if (kDown & KEY_R) {
				if (g_box_picker_box < HGSS_BOX_COUNT)
					g_box_picker_box++;
				else
					g_box_picker_box = 1;
				ww_box_selector_reload();
			} else if (kDown & KEY_A) {
				ww_box_selector_apply_choice();

				if (g_return_flow_active) {
					if (g_box_picker_mode == WW_BOX_PICKER_RETURN_SOURCE) {
						hgss_box_slot_summary *selected = &g_box_picker_slots[g_box_picker_slot - 1];

						if (selected->occupied && selected->species_id != 0) {
							printf("Destination slot must be empty for returned Pokemon\n");
							return OP_UPDATE;
						}

						printf(
								"Return destination: box %lu slot %lu\n",
								(unsigned long)g_pending_return_box,
								(unsigned long)g_pending_return_source_slot);

						g_state = IN_RETURN_SELECTOR;
						g_return_step = WW_RETURN_STEP_CAPTURE_POLICY;
						return OP_UPDATE;
					}

					if (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_1
							|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_2
							|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_3) {
						u8 idx = (u8)(g_box_picker_mode - WW_BOX_PICKER_RETURN_CAPTURE_1);

						if (!ww_return_validate_capture_target(idx, (u8)g_box_picker_box, (u8)g_box_picker_slot)) {
							printf("Capture destination must be an empty unique slot\n");
							return OP_UPDATE;
						}

						printf(
								"Capture %u destination: box %lu slot %lu\n",
								(unsigned)(idx + 1),
								(unsigned long)g_box_picker_box,
								(unsigned long)g_box_picker_slot);

						idx++;
						if (idx < g_return_capture_count) {
							if (!ww_return_open_capture_picker(idx)) {
								printf("Unable to open next capture selector\n");
								g_state = IN_RETURN_SELECTOR;
								g_return_step = WW_RETURN_STEP_CAPTURE_POLICY;
							}
						} else {
							g_state = IN_RETURN_SELECTOR;
							g_return_step = WW_RETURN_STEP_REVIEW;
						}
						return OP_UPDATE;
					}
				}

				printf(
						"Selected box %lu slot %lu\n",
						(unsigned long)g_box_picker_box,
						(unsigned long)g_box_picker_slot);

				if (g_simple_mode && g_box_picker_mode == WW_BOX_PICKER_SEND_SOURCE) {
					if (!ww_route_selector_open()) {
						g_state = IN_MENU;
						g_active_menu = ww_get_main_menu();
					}
				} else {
					g_state = IN_MENU;
				}
			} else if (kDown & KEY_B) {
				if (g_return_flow_active) {
					if (g_box_picker_mode == WW_BOX_PICKER_RETURN_SOURCE) {
						printf("Return already confirmed: choose a destination slot\n");
						return OP_UPDATE;
					}

					if (g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_1
							|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_2
							|| g_box_picker_mode == WW_BOX_PICKER_RETURN_CAPTURE_3) {
						g_state = IN_RETURN_SELECTOR;
						g_return_step = WW_RETURN_STEP_CAPTURE_POLICY;
						return OP_UPDATE;
					}
				}

				g_state = IN_MENU;
				printf("Cancelled visual box selector\n");
			}

			return OP_UPDATE;
		}

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

		if (ww_handle_simple_main_menu_input(kDown))
			return OP_UPDATE;

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
							ww_clear_debug_console();
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
				g_active_menu = ww_get_main_menu();
				ww_clear_debug_console();
			}
		} 
		return OP_UPDATE;
	}

	return async_completed ? OP_UPDATE : OP_NONE;
}

