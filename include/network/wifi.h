#pragma once

#include <3ds/types.h>
#include <stdbool.h>

#define WIFI_DEFAULT_HOST "127.0.0.1"
#define WIFI_DEFAULT_PORT 8080
#define WIFI_CONNECT_TIMEOUT_MS 25000
#define WIFI_DEFAULT_TIMEOUT_MS 30000
#define WIFI_INTERBYTE_TIMEOUT_MS 1000
#define WIFI_SOC_BUFFER_SIZE 0x100000
#define WIFI_SOC_ALIGN 0x1000

bool wifi_init(void);
void wifi_exit(void);

bool wifi_set_endpoint(const char *host, u16 port);
const char *wifi_get_host(void);
u16 wifi_get_port(void);

bool wifi_connect(void);
void wifi_disconnect(void);
bool wifi_is_connected(void);

void wifi_send_data(void *data, u32 size);
u32 wifi_recv_data(void *data, u32 size, u32 timeout_ms);

/* Debug: last error message from wifi operations (null if none) */
const char *wifi_last_error(void);
