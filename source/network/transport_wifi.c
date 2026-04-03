#include "network/transport_wifi.h"
#include "network/wifi.h"

static bool wifi_backend_init(void)
{
	return wifi_init();
}

static void wifi_backend_enable(void)
{
	wifi_connect();
}

static void wifi_backend_disable(void)
{
	wifi_disconnect();
}

static void wifi_backend_send(void *data, u32 size)
{
	wifi_send_data(data, size);
}

static u32 wifi_backend_recv(void *data, u32 size, u32 timeout_ms)
{
	return wifi_recv_data(data, size, timeout_ms);
}

static void wifi_backend_cleanup(void)
{
	wifi_exit();
}

static const transport_backend g_wifi_backend = {
	.name = "wifi",
	.init = wifi_backend_init,
	.enable = wifi_backend_enable,
	.disable = wifi_backend_disable,
	.send_data = wifi_backend_send,
	.recv_data = wifi_backend_recv,
	.cleanup = wifi_backend_cleanup,
};

const transport_backend *transport_wifi_backend_get(void)
{
	return &g_wifi_backend;
}
