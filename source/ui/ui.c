#include "ui/ui.h"
#include "hgss/hgss_item_icon_map.h"
#include "hgss/hgss_mon_icon_palette_map.h"
#include "hgss/hgss_patcher.h"
#include "hgss/hgss_storage.h"
#include "device/pokewalker_lookup.h"
#include "core/utils.h"
#include "network/wearwalker_api.h"
#include "ui/components/ui_sprite_components.h"
#include "ui/logic/ui_path_utils.h"

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
static void ww_reset_top_render_target(void);

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
static void ww_draw_simple_top_panel(void);
static void ww_async_progress_set(u32 percent, const char *label);
static void ww_async_progress_get(u32 *out_percent, char *out_label, size_t out_label_size);

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
#define WW_ASYNC_TIMEOUT_STROLL_SEND_MS (20ull * 60ull * 1000ull)
#define WW_ASYNC_TIMEOUT_STROLL_RETURN_MS (20ull * 60ull * 1000ull)
#define WW_ASYNC_PRESTART_WINDOW_MS 2500ull

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
	bool cancel_requested;
	bool remote_started;
	u64 started_ms;
	u64 timeout_ms;
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

#include "logic/ui_logic.c"
#include "views/ui_file_browser_view.c"
#include "logic/ui_async_logic.c"

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
	if (!g_wearwalker_host || !g_wearwalker_host[0]) {
		ww_set_top_status("Invalid host (empty)", 4000);
		return false;
	}

	/* warn if loopback or wildcard address is used */
	if (strcmp(g_wearwalker_host, "127.0.0.1") == 0 || strcmp(g_wearwalker_host, "0.0.0.0") == 0) {
		ww_set_top_status("Host is loopback; set LAN IP to connect from 3DS", 6000);
		/* still allow applying if user insists */
	}

	if (!ww_api_set_endpoint(g_wearwalker_host, port)) {
		ww_set_top_status("Invalid WearWalker endpoint", 4000);
		return false;
	}

	ww_sync_port_entries(port);
	{
		char msg[128];
		snprintf(msg, sizeof(msg), "Using WearWalker endpoint %s:%u", g_wearwalker_host, (unsigned)port);
		ww_set_top_status(msg, 4000);
	}
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
	if (!g_debug_console_ready || !g_console_enabled)
		return;

	consoleSelect(&g_header_console);
	consoleClear();
	consoleSelect(&logs);
	consoleClear();
}

static void ww_reset_top_render_target(void)
{
	/* Ensure TOP screen returns to C2D-compatible format after console usage. */
	gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSwapBuffersGpu();
	gspWaitForVBlank();
	target_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
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

#include "views/ui_box_selector_view.c"
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

#include "views/ui_route_return_views.c"
#include "views/ui_simple_top_panel_view.c"
/* initial_text may be NULL to leave input empty */
s32 numpad_input(const char *hint_text, u8 digits, const char *initial_text)
{
	char buf[32];
	SwkbdState swkbd;
	SwkbdButton button = SWKBD_BUTTON_NONE;

	swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, digits);
	swkbdSetHintText(&swkbd, hint_text);
	swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
	swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
	/* set initial text if provided */
	if (initial_text && initial_text[0])
		snprintf(buf, sizeof(buf), "%s", initial_text);
	else
		buf[0] = '\0';
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

/* Top-screen transient status message (drawn on simple top panel). */
char g_top_status_msg[128] = "";
u32 g_top_status_expire_tick = 0;

void ww_set_top_status(const char *msg, u32 duration_ms)
{
	if (!msg) {
		g_top_status_msg[0] = '\0';
		g_top_status_expire_tick = 0;
		return;
	}
	snprintf(g_top_status_msg, sizeof(g_top_status_msg), "%s", msg);
	/* approximate ticks: UI advances once per frame; assume ~16ms/frame */
	g_top_status_expire_tick = g_ui_anim_tick + (duration_ms / 16u) + 1u;
}

// menu_entry must be of type ENTRY_SELATTR
void goto_item(menu_entry *entry)
{
	char str[] = "Go to item";
	s32 value = numpad_input(str, 3, NULL);

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
	{
		char initbuf[32];
		snprintf(initbuf, sizeof(initbuf), "%u", (unsigned)entry->num_attr.value);
		value = numpad_input(strbuf, digits, initbuf);
	}

	if (value != -1) {
		value = value > entry->num_attr.max ? entry->num_attr.max : value;
		value = value < (s16) entry->num_attr.min ? entry->num_attr.min : value;
		entry->num_attr.value = value;
	}
}

#include "logic/ui_actions_controller.c"

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
	{
		if (kDown & KEY_B) {
			if (ww_async_request_cancel_before_start()) {
				g_return_apply_busy = false;
				ww_return_flow_reset();
				g_state = IN_MENU;
				g_active_menu = ww_get_main_menu();
				printf("Guided return cancelled before remote start\n");
			} else {
				printf("Cannot cancel now: guided return already started remotely\n");
			}
		}
		return OP_UPDATE;
	}

	if (g_route_send_busy && g_state == IN_ROUTE_SELECTOR)
	{
		if (kDown & KEY_B) {
			if (ww_async_request_cancel_before_start()) {
				g_route_send_busy = false;
				g_state = IN_MENU;
				g_active_menu = ww_get_main_menu();
				printf("Send cancelled before remote start\n");
			} else {
				printf("Cannot cancel now: send already started remotely\n");
			}
		}
		return OP_UPDATE;
	}

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

