#pragma once

#include <3ds/types.h>
#include <stdbool.h>

#define WW_API_DEFAULT_HOST "127.0.0.1"
#define WW_API_DEFAULT_PORT 8080
#define WW_API_RESPONSE_MAX 4096
#define WW_EEPROM_SIZE 65536

typedef struct {
	char trainer[32];
	u32 steps;
	u32 watts;
} wearwalker_snapshot;

bool ww_api_set_endpoint(const char *host, u16 port);
const char *ww_api_get_host(void);
u16 ww_api_get_port(void);

bool ww_api_get_status(char *out_json, u32 out_size);
bool ww_api_get_snapshot(wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);

bool ww_api_command_set_steps(u32 steps, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_command_set_watts(u32 watts, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_command_set_trainer(const char *trainer_name, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);
bool ww_api_command_set_sync(u32 epoch_seconds, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size);

bool ww_api_export_eeprom(const char *out_path);
bool ww_api_import_eeprom(const char *in_path);
