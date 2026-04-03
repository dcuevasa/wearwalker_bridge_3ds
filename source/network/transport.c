#include "network/transport.h"

static const transport_backend *g_backend;
static bool g_backend_ready;

bool transport_set_backend(const transport_backend *backend)
{
	g_backend = backend;
	g_backend_ready = false;

	return g_backend != NULL;
}

const transport_backend *transport_get_backend(void)
{
	return g_backend;
}

bool transport_init(void)
{
	if (!g_backend)
		return false;
	if (g_backend_ready)
		return true;
	if (!g_backend->init)
		return false;

	g_backend_ready = g_backend->init();
	return g_backend_ready;
}

void transport_cleanup(void)
{
	if (g_backend && g_backend->cleanup)
		g_backend->cleanup();

	g_backend_ready = false;
}

void transport_enable(void)
{
	if (g_backend && g_backend->enable)
		g_backend->enable();
}

void transport_disable(void)
{
	if (g_backend && g_backend->disable)
		g_backend->disable();
}

void transport_send_data(void *data, u32 size)
{
	if (g_backend && g_backend->send_data)
		g_backend->send_data(data, size);
}

u32 transport_recv_data(void *data, u32 size, u32 timeout_ms)
{
	if (!g_backend || !g_backend->recv_data)
		return 0;

	return g_backend->recv_data(data, size, timeout_ms);
}
