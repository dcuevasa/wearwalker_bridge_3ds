#include "network/wearwalker_api.h"
#include "network/wifi.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#define HTTP_CHUNK_SIZE 1024
#define HTTP_HEADER_MAX 2048

typedef struct {
	int status_code;
	const u8 *body;
	u32 body_size;
	s32 content_length;
	bool chunked;
} ww_http_response;

static bool g_endpoint_initialized;

static void ww_api_bootstrap_endpoint(void)
{
	if (g_endpoint_initialized)
		return;

	wifi_set_endpoint(WW_API_DEFAULT_HOST, WW_API_DEFAULT_PORT);
	g_endpoint_initialized = true;
}

bool ww_api_set_endpoint(const char *host, u16 port)
{
	ww_api_bootstrap_endpoint();
	return wifi_set_endpoint(host, port);
}

const char *ww_api_get_host(void)
{
	ww_api_bootstrap_endpoint();
	return wifi_get_host();
}

u16 ww_api_get_port(void)
{
	ww_api_bootstrap_endpoint();
	return wifi_get_port();
}

const char *ww_api_last_error(void)
{
	return wifi_last_error();
}

static bool ww_http_find_header_end(const u8 *response, u32 response_size, u32 *header_end)
{
	u32 i;

	if (!response || !response_size)
		return false;

	for (i = 0; i + 3 < response_size; i++) {
		if (response[i] == '\r' && response[i + 1] == '\n' && response[i + 2] == '\r' && response[i + 3] == '\n') {
			if (header_end)
				*header_end = i + 4;
			return true;
		}
	}

	return false;
}

static bool ww_http_header_has_chunked(const char *value)
{
	const char *cursor;

	if (!value)
		return false;

	cursor = value;
	while (*cursor) {
		const char *end;

		while (*cursor == ' ' || *cursor == '\t' || *cursor == ',')
			cursor++;
		if (!*cursor)
			break;

		end = cursor;
		while (*end && *end != ',')
			end++;

		if ((end - cursor) == 7 && strncasecmp(cursor, "chunked", 7) == 0)
			return true;

		cursor = end;
	}

	return false;
}

static bool ww_http_parse_response(const u8 *response, u32 response_size, ww_http_response *out)
{
	u32 header_end;
	u32 status_end;
	u32 line_start;
	char status_line[64] = {0};

	if (!response || !out)
		return false;
	if (!ww_http_find_header_end(response, response_size, &header_end))
		return false;

	status_end = 0;
	while (status_end + 1 < header_end && !(response[status_end] == '\r' && response[status_end + 1] == '\n'))
		status_end++;
	if (status_end + 1 >= header_end || status_end >= sizeof(status_line))
		return false;

	memcpy(status_line, response, status_end);
	status_line[status_end] = '\0';
	if (sscanf(status_line, "HTTP/%*s %d", &out->status_code) != 1)
		return false;

	out->body = response + header_end;
	out->body_size = response_size - header_end;
	out->content_length = -1;
	out->chunked = false;

	line_start = status_end + 2;
	while (line_start + 1 < header_end) {
		u32 line_end = line_start;
		u32 header_name_len;
		const u8 *colon;
		const u8 *value_start;
		u32 value_len;
		long parsed_len;
		char header_name[40];
		char header_value[128];

		while (line_end + 1 < header_end && !(response[line_end] == '\r' && response[line_end + 1] == '\n'))
			line_end++;
		if (line_end + 1 >= header_end)
			break;
		if (line_end == line_start) {
			line_start = line_end + 2;
			continue;
		}

		colon = memchr(response + line_start, ':', line_end - line_start);
		if (!colon) {
			line_start = line_end + 2;
			continue;
		}

		header_name_len = (u32)(colon - (response + line_start));
		if (header_name_len >= sizeof(header_name))
			header_name_len = sizeof(header_name) - 1;
		memcpy(header_name, response + line_start, header_name_len);
		header_name[header_name_len] = '\0';

		value_start = colon + 1;
		while (value_start < response + line_end && (*value_start == ' ' || *value_start == '\t'))
			value_start++;

		value_len = (u32)((response + line_end) - value_start);
		if (value_len >= sizeof(header_value))
			value_len = sizeof(header_value) - 1;
		memcpy(header_value, value_start, value_len);
		header_value[value_len] = '\0';

		if (strcasecmp(header_name, "Content-Length") == 0) {
			parsed_len = strtol(header_value, NULL, 10);
			if (parsed_len >= 0 && parsed_len <= INT_MAX)
				out->content_length = (s32)parsed_len;
		} else if (strcasecmp(header_name, "Transfer-Encoding") == 0) {
			if (ww_http_header_has_chunked(header_value))
				out->chunked = true;
		}

		line_start = line_end + 2;
	}

	return true;
}

static bool ww_http_decode_chunked(const u8 *data, u32 data_size, char *out, u32 out_size, u32 *decoded_size)
{
	u32 cursor = 0;
	u32 written = 0;

	if (!data || !out || out_size == 0)
		return false;

	while (cursor < data_size) {
		u32 line_start = cursor;
		u32 line_len;
		unsigned long chunk_len;
		char chunk_len_str[16];
		char *endptr;

		while (cursor + 1 < data_size && !(data[cursor] == '\r' && data[cursor + 1] == '\n'))
			cursor++;
		if (cursor + 1 >= data_size)
			return false;

		line_len = cursor - line_start;
		if (line_len == 0 || line_len >= sizeof(chunk_len_str))
			return false;

		memcpy(chunk_len_str, data + line_start, line_len);
		chunk_len_str[line_len] = '\0';
		chunk_len = strtoul(chunk_len_str, &endptr, 16);
		if (endptr == chunk_len_str)
			return false;

		cursor += 2;
		if (chunk_len == 0) {
			out[written] = '\0';
			if (decoded_size)
				*decoded_size = written;
			return true;
		}

		if (chunk_len > (unsigned long)(data_size - cursor))
			return false;
		if (written + chunk_len >= out_size)
			return false;

		memcpy(out + written, data + cursor, chunk_len);
		written += (u32)chunk_len;
		cursor += (u32)chunk_len;

		if (cursor + 1 >= data_size || data[cursor] != '\r' || data[cursor + 1] != '\n')
			return false;
		cursor += 2;
	}

	return false;
}

static bool ww_http_extract_text_body(const u8 *response, u32 response_size, char *out, u32 out_size)
{
	ww_http_response parsed;
	u32 copy_size;

	if (!out || out_size == 0)
		return false;
	if (!ww_http_parse_response(response, response_size, &parsed))
		return false;
	if (parsed.status_code < 200 || parsed.status_code >= 300)
		return false;

	if (parsed.chunked)
		return ww_http_decode_chunked(parsed.body, parsed.body_size, out, out_size, NULL);

	if (parsed.content_length >= 0) {
		if ((u32)parsed.content_length > parsed.body_size)
			return false;
		copy_size = (u32)parsed.content_length;
	} else {
		copy_size = parsed.body_size;
	}

	copy_size = copy_size < out_size - 1 ? copy_size : out_size - 1;
	memcpy(out, parsed.body, copy_size);
	out[copy_size] = '\0';

	return true;
}

static bool ww_http_extract_text_body_any_status(const u8 *response, u32 response_size, char *out, u32 out_size)
{
	ww_http_response parsed;
	u32 copy_size;

	if (!out || out_size == 0)
		return false;
	if (!ww_http_parse_response(response, response_size, &parsed))
		return false;

	if (parsed.chunked) {
		return ww_http_decode_chunked(parsed.body, parsed.body_size, out, out_size, NULL);
	}

	if (parsed.content_length >= 0) {
		if ((u32)parsed.content_length > parsed.body_size)
			return false;
		copy_size = (u32)parsed.content_length;
	} else {
		copy_size = parsed.body_size;
	}

	copy_size = copy_size < out_size - 1 ? copy_size : out_size - 1;
	memcpy(out, parsed.body, copy_size);
	out[copy_size] = '\0';

	return true;
}

static bool ww_http_response_is_complete(const u8 *response, u32 response_size)
{
	ww_http_response parsed;
	u32 i;
	static const u8 chunk_end[] = {'0', '\r', '\n', '\r', '\n'};

	if (!response || response_size == 0)
		return false;
	if (!ww_http_parse_response(response, response_size, &parsed))
		return false;

	if (parsed.chunked) {
		if (parsed.body_size < sizeof(chunk_end))
			return false;

		for (i = 0; i + sizeof(chunk_end) <= parsed.body_size; i++) {
			if (memcmp(parsed.body + i, chunk_end, sizeof(chunk_end)) == 0)
				return true;
		}
		return false;
	}

	if (parsed.content_length >= 0)
		return parsed.body_size >= (u32)parsed.content_length;

	return false;
}

static bool ww_http_request_raw(const char *method, const char *path,
		const u8 *request_body, u32 request_body_size,
		u8 *response, u32 response_size, u32 *out_response_size)
{
	char header[512];
	u32 total = 0;
	const char *host;
	u16 port;

	ww_api_bootstrap_endpoint();
	host = wifi_get_host();
	port = wifi_get_port();

	if (!method || !path || !response || response_size == 0)
		return false;

	if (request_body && request_body_size) {
		snprintf(header, sizeof(header),
				"%s %s HTTP/1.1\r\n"
				"Host: %s:%u\r\n"
				"Connection: close\r\n"
				"Content-Type: application/octet-stream\r\n"
				"Content-Length: %lu\r\n\r\n",
				method, path, host, port, (unsigned long)request_body_size);
	} else {
		snprintf(header, sizeof(header),
				"%s %s HTTP/1.1\r\n"
				"Host: %s:%u\r\n"
				"Connection: close\r\n\r\n",
				method, path, host, port);
	}

	wifi_disconnect();
	if (!wifi_connect())
		return false;

	wifi_send_data(header, (u32)strlen(header));
	if (request_body && request_body_size)
		wifi_send_data((void *)request_body, request_body_size);

	{
		u32 timeout_ms = WIFI_DEFAULT_TIMEOUT_MS;
	while (total < response_size) {
		u32 got = wifi_recv_data(response + total, response_size - total, timeout_ms);
		if (!got)
			break;
		total += got;
		if (ww_http_response_is_complete(response, total))
			break;
		timeout_ms = WIFI_INTERBYTE_TIMEOUT_MS;
	}
	}
	wifi_disconnect();

	if (out_response_size)
		*out_response_size = total;
	return total > 0;
}

static bool ww_http_request_json(const char *method, const char *path,
		const char *request_json,
		u8 *response, u32 response_size, u32 *out_response_size)
{
	char header[512];
	u32 total = 0;
	u32 request_size = 0;
	const char *host;
	u16 port;

	ww_api_bootstrap_endpoint();
	host = wifi_get_host();
	port = wifi_get_port();

	if (!method || !path || !response || response_size == 0)
		return false;

	if (request_json)
		request_size = (u32)strlen(request_json);

	snprintf(header, sizeof(header),
			"%s %s HTTP/1.1\r\n"
			"Host: %s:%u\r\n"
			"Connection: close\r\n"
			"Accept: application/json\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %lu\r\n\r\n",
			method, path, host, port, (unsigned long)request_size);

	wifi_disconnect();
	if (!wifi_connect())
		return false;

	wifi_send_data(header, (u32)strlen(header));
	if (request_size)
		wifi_send_data((void *)request_json, request_size);

	{
		u32 timeout_ms = WIFI_DEFAULT_TIMEOUT_MS;
	while (total < response_size) {
		u32 got = wifi_recv_data(response + total, response_size - total, timeout_ms);
		if (!got)
			break;
		total += got;
		if (ww_http_response_is_complete(response, total))
			break;
		timeout_ms = WIFI_INTERBYTE_TIMEOUT_MS;
	}
	}
	wifi_disconnect();

	if (out_response_size)
		*out_response_size = total;
	return total > 0;
}

static bool ww_json_get_u32(const char *json, const char *key, u32 *out_value)
{
	char token[64];
	char *cursor;
	char *endptr;
	unsigned long value;

	if (!json || !key || !out_value)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr((char *)json, token);
	if (!cursor)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor)
		return false;
	cursor++;

	while (*cursor && isspace((unsigned char)*cursor))
		cursor++;

	value = strtoul(cursor, &endptr, 10);
	if (endptr == cursor)
		return false;

	*out_value = (u32)value;
	return true;
}

static bool ww_json_get_string(const char *json, const char *key, char *out_str, u32 out_size)
{
	char token[64];
	char *cursor;
	char *start;
	char *end;
	u32 len;

	if (!json || !key || !out_str || out_size == 0)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr((char *)json, token);
	if (!cursor)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor)
		return false;

	start = strchr(cursor, '\"');
	if (!start)
		return false;
	start++;
	end = strchr(start, '\"');
	if (!end)
		return false;

	len = (u32)(end - start);
	if (len >= out_size)
		len = out_size - 1;

	memcpy(out_str, start, len);
	out_str[len] = '\0';
	return true;
}

static void ww_snapshot_from_json(const char *json, wearwalker_snapshot *out_snapshot)
{
	if (!json || !out_snapshot)
		return;

	if (!ww_json_get_string(json, "trainerName", out_snapshot->trainer, sizeof(out_snapshot->trainer)) &&
			!ww_json_get_string(json, "trainer", out_snapshot->trainer, sizeof(out_snapshot->trainer))) {
		snprintf(out_snapshot->trainer, sizeof(out_snapshot->trainer), "UNKNOWN");
	}

	if (!ww_json_get_u32(json, "steps", &out_snapshot->steps))
		out_snapshot->steps = 0;
	if (!ww_json_get_u32(json, "watts", &out_snapshot->watts))
		out_snapshot->watts = 0;
}

static bool ww_api_command_request_json(const char *method, const char *path, const char *body_json,
		wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size)
{
	u8 *response = NULL;
	u32 response_size;
	char *json_buf = NULL;
	bool ok = false;

	response = (u8 *)malloc(WW_API_RESPONSE_MAX);
	json_buf = (char *)malloc(WW_API_RESPONSE_MAX);
	if (!response || !json_buf)
		goto cleanup;

	if (!ww_http_request_json(method, path, body_json, response, WW_API_RESPONSE_MAX, &response_size))
		goto cleanup;

	if (!ww_http_extract_text_body(response, response_size, json_buf, WW_API_RESPONSE_MAX)) {
		/* Keep backend error JSON for callers even when HTTP status is not 2xx. */
		ww_http_extract_text_body_any_status(response, response_size, json_buf, WW_API_RESPONSE_MAX);

		if (out_json && out_size > 0) {
			u32 json_copy_size = (u32)strlen(json_buf);
			if (json_copy_size >= out_size)
				json_copy_size = out_size - 1;
			memcpy(out_json, json_buf, json_copy_size);
			out_json[json_copy_size] = '\0';
		}
		goto cleanup;
	}

	if (out_snapshot)
		ww_snapshot_from_json(json_buf, out_snapshot);

	if (out_json && out_size > 0) {
		u32 json_copy_size = (u32)strlen(json_buf);
		if (json_copy_size >= out_size)
			json_copy_size = out_size - 1;
		memcpy(out_json, json_buf, json_copy_size);
		out_json[json_copy_size] = '\0';
	}

	ok = true;

cleanup:
	if (response)
		free(response);
	if (json_buf)
		free(json_buf);
	return ok;
}

static bool ww_json_string_is_safe(const char *value)
{
	const unsigned char *cursor;

	if (!value)
		return false;

	cursor = (const unsigned char *)value;
	while (*cursor) {
		if (*cursor < 0x20 || *cursor == '"' || *cursor == '\\')
			return false;
		cursor++;
	}

	return true;
}

static bool ww_hex_encode(const u8 *data, u32 data_size, char *out_hex, u32 out_hex_size)
{
	static const char hex_chars[] = "0123456789abcdef";
	u32 index;

	if (!data || !out_hex)
		return false;
	if (out_hex_size < data_size * 2 + 1)
		return false;

	for (index = 0; index < data_size; index++) {
		u8 value = data[index];
		out_hex[index * 2] = hex_chars[(value >> 4) & 0x0F];
		out_hex[index * 2 + 1] = hex_chars[value & 0x0F];
	}
	out_hex[data_size * 2] = '\0';
	return true;
}

bool ww_api_get_status(char *out_json, u32 out_size)
{
	u8 *response;
	u32 response_size;
	bool ok = false;

	if (!out_json || out_size == 0)
		return false;

	response = (u8 *)malloc(WW_API_RESPONSE_MAX);
	if (!response)
		return false;

	if (!ww_http_request_raw("GET", "/api/v1/bridge/status", NULL, 0,
				response, WW_API_RESPONSE_MAX, &response_size))
		goto cleanup;

	ok = ww_http_extract_text_body(response, response_size, out_json, out_size);

cleanup:
	free(response);
	return ok;
}

bool ww_api_get_snapshot(wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size)
{
	u8 *response = NULL;
	u32 response_size;
	char *json_buf = NULL;
	bool ok = false;

	if (!out_snapshot)
		return false;

	response = (u8 *)malloc(WW_API_RESPONSE_MAX);
	json_buf = (char *)malloc(WW_API_RESPONSE_MAX);
	if (!response || !json_buf)
		goto cleanup;

	if (!ww_http_request_raw("GET", "/api/v1/device/snapshot", NULL, 0,
				response, WW_API_RESPONSE_MAX, &response_size))
		goto cleanup;

	if (!ww_http_extract_text_body(response, response_size, json_buf, WW_API_RESPONSE_MAX))
		goto cleanup;

	if (!ww_json_get_string(json_buf, "trainerName", out_snapshot->trainer, sizeof(out_snapshot->trainer)) &&
			!ww_json_get_string(json_buf, "trainer", out_snapshot->trainer, sizeof(out_snapshot->trainer))) {
		snprintf(out_snapshot->trainer, sizeof(out_snapshot->trainer), "UNKNOWN");
	}

	if (!ww_json_get_u32(json_buf, "steps", &out_snapshot->steps))
		out_snapshot->steps = 0;
	if (!ww_json_get_u32(json_buf, "watts", &out_snapshot->watts))
		out_snapshot->watts = 0;

	if (out_json && out_size > 0) {
		u32 json_copy_size = (u32)strlen(json_buf);
		if (json_copy_size >= out_size)
			json_copy_size = out_size - 1;
		memcpy(out_json, json_buf, json_copy_size);
		out_json[json_copy_size] = '\0';
	}

	ok = true;

cleanup:
	if (response)
		free(response);
	if (json_buf)
		free(json_buf);
	return ok;
}

bool ww_api_get_sync_package(char *out_json, u32 out_size)
{
	u8 *response;
	u32 response_size;
	bool ok = false;

	if (!out_json || out_size == 0)
		return false;

	response = (u8 *)malloc(WW_API_RESPONSE_MAX);
	if (!response)
		return false;

	if (!ww_http_request_raw("GET", "/api/v1/sync/package", NULL, 0,
				response, WW_API_RESPONSE_MAX, &response_size))
		goto cleanup;

	ok = ww_http_extract_text_body(response, response_size, out_json, out_size);

cleanup:
	free(response);
	return ok;
}

bool ww_api_command_set_steps(u32 steps, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size)
{
	char body[64];

	snprintf(body, sizeof(body), "{\"steps\":%lu}", (unsigned long)steps);
	return ww_api_command_request_json("POST", "/api/v1/device/commands/set-steps", body, out_snapshot, out_json, out_size);
}

bool ww_api_command_set_watts(u32 watts, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size)
{
	char body[64];

	if (watts > 0xFFFF)
		return false;

	snprintf(body, sizeof(body), "{\"watts\":%lu}", (unsigned long)watts);
	return ww_api_command_request_json("POST", "/api/v1/device/commands/set-watts", body, out_snapshot, out_json, out_size);
}

bool ww_api_command_set_trainer(const char *trainer_name, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size)
{
	char body[96];
	int written;

	if (!trainer_name || !trainer_name[0])
		return false;
	if (!ww_json_string_is_safe(trainer_name))
		return false;

	written = snprintf(body, sizeof(body), "{\"name\":\"%s\"}", trainer_name);
	if (written < 0 || (u32)written >= sizeof(body))
		return false;

	return ww_api_command_request_json("POST", "/api/v1/device/commands/set-trainer", body, out_snapshot, out_json, out_size);
}

bool ww_api_command_set_sync(u32 epoch_seconds, wearwalker_snapshot *out_snapshot, char *out_json, u32 out_size)
{
	char body[64];

	snprintf(body, sizeof(body), "{\"epoch\":%lu}", (unsigned long)epoch_seconds);
	return ww_api_command_request_json("POST", "/api/v1/device/commands/set-sync", body, out_snapshot, out_json, out_size);
}

bool ww_api_patch_identity(const char *trainer_name, u16 trainer_tid, u16 trainer_sid, char *out_json, u32 out_size)
{
	char body[192];
	int written;

	if (!trainer_name || !trainer_name[0])
		trainer_name = "WWBRIDGE";
	if (!ww_json_string_is_safe(trainer_name))
		return false;

	written = snprintf(
			body,
			sizeof(body),
			"{\"trainerName\":\"%s\",\"trainerTid\":%u,\"trainerSid\":%u}",
			trainer_name,
			(unsigned)trainer_tid,
			(unsigned)trainer_sid);
	if (written < 0 || (u32)written >= sizeof(body))
		return false;

	return ww_api_command_request_json("PATCH", "/api/v1/device/identity", body, NULL, out_json, out_size);
}

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
		u32 out_size)
{
	char body[512];
	u16 move1 = 0;
	u16 move2 = 0;
	u16 move3 = 0;
	u16 move4 = 0;
	int written;

	if (species_id == 0)
		return false;
	if (level == 0 || level > 100)
		level = 10;
	if (moves) {
		move1 = moves[0];
		move2 = moves[1];
		move3 = moves[2];
		move4 = moves[3];
	}

	if (nickname && nickname[0]) {
		if (!ww_json_string_is_safe(nickname))
			return false;

		written = snprintf(
				body,
				sizeof(body),
				"{\"speciesId\":%u,\"level\":%u,\"courseId\":%u,\"nickname\":\"%s\",\"friendship\":%u,\"heldItem\":%u,\"moves\":[%u,%u,%u,%u],\"variantFlags\":%u,\"specialFlags\":%u,\"clearBuffers\":%s,\"allowLockedCourse\":%s}",
				(unsigned)species_id,
				(unsigned)level,
				(unsigned)course_id,
				nickname,
				(unsigned)friendship,
				(unsigned)held_item,
				(unsigned)move1,
				(unsigned)move2,
				(unsigned)move3,
				(unsigned)move4,
				(unsigned)variant_flags,
				(unsigned)special_flags,
				clear_buffers ? "true" : "false",
				allow_locked_course ? "true" : "false");
	} else {
		written = snprintf(
				body,
				sizeof(body),
				"{\"speciesId\":%u,\"level\":%u,\"courseId\":%u,\"friendship\":%u,\"heldItem\":%u,\"moves\":[%u,%u,%u,%u],\"variantFlags\":%u,\"specialFlags\":%u,\"clearBuffers\":%s,\"allowLockedCourse\":%s}",
				(unsigned)species_id,
				(unsigned)level,
				(unsigned)course_id,
				(unsigned)friendship,
				(unsigned)held_item,
				(unsigned)move1,
				(unsigned)move2,
				(unsigned)move3,
				(unsigned)move4,
				(unsigned)variant_flags,
				(unsigned)special_flags,
				clear_buffers ? "true" : "false",
				allow_locked_course ? "true" : "false");
	}
	if (written < 0 || (u32)written >= sizeof(body))
		return false;

	return ww_api_command_request_json("POST", "/api/v1/stroll/send", body, NULL, out_json, out_size);
}

bool ww_api_stroll_send_resolved_json(const char *json_body, char *out_json, u32 out_size)
{
	if (!json_body || !json_body[0])
		return false;

	return ww_api_command_request_json("POST", "/api/v1/stroll/send", json_body, NULL, out_json, out_size);
}

bool ww_api_stroll_patch_sprite_block(
		const char *key,
		const u8 *data,
		u32 data_size,
		char *out_json,
		u32 out_size)
{
	char *payload_hex = NULL;
	char *body = NULL;
	u32 payload_hex_size;
	u32 body_size;
	int written;
	bool ok = false;

	if (!key || !key[0] || !data || data_size == 0)
		return false;
	if (!ww_json_string_is_safe(key))
		return false;

	payload_hex_size = data_size * 2 + 1;
	if (payload_hex_size < data_size)
		return false;

	payload_hex = (char *)malloc(payload_hex_size);
	if (!payload_hex)
		goto cleanup;

	if (!ww_hex_encode(data, data_size, payload_hex, payload_hex_size))
		goto cleanup;

	body_size = (u32)strlen(key) + payload_hex_size + 96;
	body = (char *)malloc(body_size);
	if (!body)
		goto cleanup;

	written = snprintf(
			body,
			body_size,
			"{\"patches\":[{\"key\":\"%s\",\"dataHex\":\"%s\"}]}",
			key,
			payload_hex);
	if (written < 0 || (u32)written >= body_size)
		goto cleanup;

	ok = ww_api_command_request_json("POST", "/api/v2/stroll/sprite-patches", body, NULL, out_json, out_size);

cleanup:
	if (payload_hex)
		free(payload_hex);
	if (body)
		free(body);
	return ok;
}

bool ww_api_stroll_return(
		u32 walked_steps,
		u16 bonus_watts,
		u8 auto_captures,
		bool replace_when_full,
		bool clear_caught_after_return,
		char *out_json,
		u32 out_size)
{
	char body[256];
	int written;

	written = snprintf(
			body,
			sizeof(body),
			"{\"walkedSteps\":%lu,\"bonusWatts\":%u,\"autoCaptures\":%u,\"replaceWhenFull\":%s,\"clearCaughtAfterReturn\":%s}",
			(unsigned long)walked_steps,
			(unsigned)bonus_watts,
			(unsigned)auto_captures,
			replace_when_full ? "true" : "false",
			clear_caught_after_return ? "true" : "false");
	if (written < 0 || (u32)written >= sizeof(body))
		return false;

	return ww_api_command_request_json("POST", "/api/v1/stroll/return", body, NULL, out_json, out_size);
}

bool ww_api_export_eeprom(const char *out_path)
{
	char header[256];
	u8 chunk[HTTP_CHUNK_SIZE];
	u8 header_buf[HTTP_HEADER_MAX];
	ww_http_response parsed;
	u32 header_size = 0;
	u32 written = 0;
	bool header_done = false;
	bool ok = false;
	FILE *f;

	if (!out_path)
		return false;

	ww_api_bootstrap_endpoint();

	f = fopen(out_path, "wb");
	if (!f)
		return false;

	snprintf(header, sizeof(header),
			"GET /api/v1/eeprom/export HTTP/1.1\r\n"
			"Host: %s:%u\r\n"
			"Connection: close\r\n\r\n",
			wifi_get_host(), wifi_get_port());

	wifi_disconnect();
	if (!wifi_connect()) {
		fclose(f);
		return false;
	}

	wifi_send_data(header, (u32)strlen(header));

	while (1) {
		u32 got = wifi_recv_data(chunk, sizeof(chunk), WIFI_DEFAULT_TIMEOUT_MS);
		if (!got)
			break;

		if (!header_done) {
			if (header_size + got > sizeof(header_buf)) {
				goto cleanup;
			}

			memcpy(header_buf + header_size, chunk, got);
			header_size += got;

			if (!ww_http_find_header_end(header_buf, header_size, NULL))
				continue;

			if (!ww_http_parse_response(header_buf, header_size, &parsed))
				goto cleanup;
			if (parsed.status_code < 200 || parsed.status_code >= 300)
				goto cleanup;
			if (parsed.chunked)
				goto cleanup;
			if (parsed.content_length != WW_EEPROM_SIZE)
				goto cleanup;

			header_done = true;
			if (parsed.body_size > 0) {
				if (parsed.body_size > WW_EEPROM_SIZE)
					goto cleanup;
				if (fwrite(parsed.body, 1, parsed.body_size, f) != parsed.body_size)
					goto cleanup;
				written += parsed.body_size;
			}
		} else {
			u32 remaining = WW_EEPROM_SIZE - written;

			if (got > remaining)
				goto cleanup;
			if (fwrite(chunk, 1, got, f) != got)
				goto cleanup;
			written += got;
		}

		if (header_done && written == WW_EEPROM_SIZE) {
			ok = true;
			break;
		}
	}

	if (header_done && written == WW_EEPROM_SIZE)
		ok = true;

cleanup:
	wifi_disconnect();
	fclose(f);

	if (!ok) {
		remove(out_path);
		return false;
	}

	return true;
}

bool ww_api_import_eeprom(const char *in_path)
{
	char header[512];
	u8 *response = NULL;
	u32 response_size;
	ww_http_response parsed;
	FILE *f;
	u8 buffer[HTTP_CHUNK_SIZE];
	u32 size;
	u32 sent = 0;
	bool ok = false;

	if (!in_path)
		return false;

	ww_api_bootstrap_endpoint();

	f = fopen(in_path, "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	size = (u32)ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size != WW_EEPROM_SIZE) {
		fclose(f);
		return false;
	}

	response = (u8 *)malloc(WW_API_RESPONSE_MAX);
	if (!response) {
		fclose(f);
		return false;
	}

	snprintf(header, sizeof(header),
			"PUT /api/v1/eeprom/import HTTP/1.1\r\n"
			"Host: %s:%u\r\n"
			"Connection: close\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Content-Length: %lu\r\n\r\n",
			wifi_get_host(), wifi_get_port(), (unsigned long)size);

	wifi_disconnect();
	if (!wifi_connect()) {
		fclose(f);
		goto cleanup;
	}

	wifi_send_data(header, (u32)strlen(header));

	while (sent < size) {
		u32 read = (u32)fread(buffer, 1, sizeof(buffer), f);
		if (!read)
			break;
		wifi_send_data(buffer, read);
		sent += read;
	}
	fclose(f);

	if (sent != size) {
		wifi_disconnect();
		goto cleanup;
	}

	response_size = 0;
	while (response_size < WW_API_RESPONSE_MAX) {
		u32 got = wifi_recv_data(response + response_size, WW_API_RESPONSE_MAX - response_size, WIFI_DEFAULT_TIMEOUT_MS);
		if (!got)
			break;
		response_size += got;
	}
	wifi_disconnect();

	if (!response_size)
		goto cleanup;

	if (!ww_http_parse_response(response, response_size, &parsed))
		goto cleanup;

	ok = parsed.status_code >= 200 && parsed.status_code < 300;

cleanup:
	if (response)
		free(response);
	return ok;
}
