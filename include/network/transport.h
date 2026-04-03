#pragma once

#include <3ds/types.h>
#include <stdbool.h>

typedef struct {
	const char *name;
	bool (*init)(void);
	void (*enable)(void);
	void (*disable)(void);
	void (*send_data)(void *data, u32 size);
	u32 (*recv_data)(void *data, u32 size, u32 timeout_ms);
	void (*cleanup)(void);
} transport_backend;

bool transport_set_backend(const transport_backend *backend);
const transport_backend *transport_get_backend(void);
bool transport_init(void);
void transport_cleanup(void);
void transport_enable(void);
void transport_disable(void);
void transport_send_data(void *data, u32 size);
u32 transport_recv_data(void *data, u32 size, u32 timeout_ms);
