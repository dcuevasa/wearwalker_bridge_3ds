#include "wifi.h"

#include <3ds.h>
#include <3ds/services/soc.h>

#include <arpa/inet.h>
#include <malloc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
	bool inited;
	bool connected;
	u32 *soc_buffer;
	int socket_fd;
	char host[64];
	u16 port;
} wifi_context;

static wifi_context g_wifi = {
	.inited = false,
	.connected = false,
	.soc_buffer = NULL,
	.socket_fd = -1,
	.host = WIFI_DEFAULT_HOST,
	.port = WIFI_DEFAULT_PORT,
};

static bool wifi_resolve_host(const char *host, struct in_addr *out)
{
	if (!host || !out)
		return false;

	if (inet_aton(host, out) != 0)
		return true;

	struct hostent *entry = gethostbyname(host);
	if (!entry || entry->h_addrtype != AF_INET || !entry->h_addr_list || !entry->h_addr_list[0])
		return false;

	memcpy(out, entry->h_addr_list[0], sizeof(*out));
	return true;
}

bool wifi_init(void)
{
	Result ret;

	if (g_wifi.inited)
		return true;

	g_wifi.soc_buffer = (u32 *)memalign(WIFI_SOC_ALIGN, WIFI_SOC_BUFFER_SIZE);
	if (!g_wifi.soc_buffer)
		return false;

	ret = socInit(g_wifi.soc_buffer, WIFI_SOC_BUFFER_SIZE);
	if (R_FAILED(ret)) {
		free(g_wifi.soc_buffer);
		g_wifi.soc_buffer = NULL;
		return false;
	}

	g_wifi.inited = true;
	g_wifi.connected = false;
	g_wifi.socket_fd = -1;
	return true;
}

void wifi_disconnect(void)
{
	if (g_wifi.socket_fd >= 0) {
		close(g_wifi.socket_fd);
		g_wifi.socket_fd = -1;
	}
	g_wifi.connected = false;
}

void wifi_exit(void)
{
	if (!g_wifi.inited)
		return;

	wifi_disconnect();
	socExit();
	free(g_wifi.soc_buffer);
	g_wifi.soc_buffer = NULL;
	g_wifi.inited = false;
}

bool wifi_set_endpoint(const char *host, u16 port)
{
	size_t len;

	if (!host || !port)
		return false;

	len = strlen(host);
	if (len == 0 || len >= sizeof(g_wifi.host))
		return false;

	strncpy(g_wifi.host, host, sizeof(g_wifi.host) - 1);
	g_wifi.host[sizeof(g_wifi.host) - 1] = '\0';
	g_wifi.port = port;

	if (g_wifi.connected)
		wifi_disconnect();

	return true;
}

const char *wifi_get_host(void)
{
	return g_wifi.host;
}

u16 wifi_get_port(void)
{
	return g_wifi.port;
}

bool wifi_connect(void)
{
	struct in_addr host_addr;
	struct sockaddr_in addr;

	if (!g_wifi.inited && !wifi_init())
		return false;
	if (g_wifi.connected)
		return true;
	if (!wifi_resolve_host(g_wifi.host, &host_addr))
		return false;

	g_wifi.socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (g_wifi.socket_fd < 0)
		return false;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(g_wifi.port);
	addr.sin_addr = host_addr;

	if (connect(g_wifi.socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wifi_disconnect();
		return false;
	}

	g_wifi.connected = true;
	return true;
}

bool wifi_is_connected(void)
{
	return g_wifi.connected;
}

void wifi_send_data(void *data, u32 size)
{
	u8 *ptr = (u8 *)data;

	if (!data || size == 0)
		return;
	if (!wifi_connect())
		return;

	while (size) {
		int sent = send(g_wifi.socket_fd, ptr, size, 0);
		if (sent <= 0) {
			wifi_disconnect();
			return;
		}

		ptr += sent;
		size -= sent;
	}
}

u32 wifi_recv_data(void *data, u32 size, u32 timeout_ms)
{
	u8 *ptr = (u8 *)data;
	u32 received = 0;
	u32 wait_ms;

	if (!data || size == 0)
		return 0;
	if (!wifi_connect())
		return 0;

	wait_ms = timeout_ms ? timeout_ms : WIFI_DEFAULT_TIMEOUT_MS;

	while (received < size) {
		fd_set readfds;
		struct timeval timeout;

		FD_ZERO(&readfds);
		FD_SET(g_wifi.socket_fd, &readfds);
		timeout.tv_sec = wait_ms / 1000;
		timeout.tv_usec = (wait_ms % 1000) * 1000;

		int ready = select(g_wifi.socket_fd + 1, &readfds, NULL, NULL, &timeout);
		if (ready <= 0)
			break;

		int chunk = recv(g_wifi.socket_fd, ptr + received, size - received, 0);
		if (chunk <= 0) {
			wifi_disconnect();
			break;
		}

		received += chunk;
		wait_ms = WIFI_INTERBYTE_TIMEOUT_MS;
	}

	return received;
}
