#pragma once

#include <3ds/types.h>
#include <stdbool.h>

#define WW_API_DEFAULT_HOST "127.0.0.1"
#define WW_API_DEFAULT_PORT 8080
#define WW_API_RESPONSE_MAX 32768
#define WW_EEPROM_SIZE 65536

typedef struct {
	char trainer[32];
	u32 steps;
	u32 watts;
} wearwalker_snapshot;

bool ww_api_set_endpoint(const char *host, u16 port);
const char *ww_api_get_host(void);
u16 ww_api_get_port(void);
const char *ww_api_last_error(void);

bool ww_api_get_status(char *out_json, u32 out_size);
bool ww_api_get_snapshot(wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_get_sync_package(char *out_json, u32 out_size);

bool ww_api_command_set_steps(u32 steps, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_command_set_watts(u32 watts, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_command_set_trainer(const char *trainer_name, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_command_set_sync(u32 epoch_seconds, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_patch_identity(const char *trainer_name, u16 trainer_tid, u16 trainer_sid, char *out_json, u32 out_size);

bool ww_api_stroll_send(
		u16 species_id,
		u8 level,
		u8 course_id,
		bool clear_buffers,
		bool allow_locked_course,
		const char *nickname,
		u8 friendship,
		u16 held_item,
		const u16 moves[4],
		u8 variant_flags,
		u8 special_flags,
		char *out_json,
		u32 out_size);

bool ww_api_stroll_send_resolved_json(
		const char *json_body,
		char *out_json,
		u32 out_size);

bool ww_api_stroll_patch_sprite_block(
		const char *key,
		const u8 *data,
		u32 data_size,
		char *out_json,
		u32 out_size);

bool ww_api_stroll_return(
		u32 walked_steps,
		u16 bonus_watts,
		u8 auto_captures,
		bool replace_when_full,
		bool clear_caught_after_return,
		char *out_json,
		u32 out_size);

bool ww_api_export_eeprom(const char *out_path);
bool ww_api_import_eeprom(const char *in_path);
