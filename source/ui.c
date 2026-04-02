#include "ui.h"
#include "wearwalker_api.h"

#include <3ds.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char g_wearwalker_host[64] = WW_API_DEFAULT_HOST;
static char g_wearwalker_trainer[32] = "WWBRIDGE";
static u32 g_pending_steps = 1000;
static u32 g_pending_watts = 100;
static u32 g_pending_sync = 0;
static char g_pending_trainer[32] = "WWBRIDGE";

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
} ww_async_task;

typedef struct {
	Thread thread;
	LightLock lock;
	bool running;
	bool finished;
	ww_async_task task;
	bool success;
	char json[WW_API_RESPONSE_MAX];
	wearwalker_snapshot snapshot;
} ww_async_context;

static ww_async_context g_ww_async;
static s32 g_ui_thread_prio = 0x30;

#define WW_ASYNC_STACK_SIZE (24 * 1024)

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
		default:
			return "request";
	}
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
	char pending_trainer[sizeof(g_pending_trainer)];

	snprintf(pending_trainer, sizeof(pending_trainer), "%s", g_pending_trainer);

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
	char json[WW_API_RESPONSE_MAX];
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

// Main menu
menu_entry main_menu_entries[] = {
	{"WearWalker API test menu", ENTRY_CHANGEMENU, .new_menu = &wearwalker_wifi_menu},
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
	g_pending_steps = wearwalker_wifi_menu_entries[WW_MENU_CMD_STEPS_VALUE].num_attr.value;
	g_pending_watts = wearwalker_wifi_menu_entries[WW_MENU_CMD_WATTS_VALUE].num_attr.value;
	g_pending_sync = wearwalker_wifi_menu_entries[WW_MENU_CMD_SYNC_VALUE].num_attr.value;
	snprintf(g_pending_trainer, sizeof(g_pending_trainer), "%s", g_wearwalker_trainer);

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

	if (g_state == IN_SELECTION)
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

	if (kDown) {
		if (kDown & KEY_START) {
			return OP_EXIT;
		} else if (kDown & KEY_UP) {
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

