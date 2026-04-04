static bool ww_json_get_u32_from(const char *json, const char *key, u32 *out_value)
{
	char token[64];
	const char *cursor;
	char *endptr;
	unsigned long value;

	if (!json || !key || !out_value)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr(json, token);
	if (!cursor)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor)
		return false;
	cursor++;

	while (*cursor && isspace((unsigned char)*cursor))
		cursor++;

	value = strtoul(cursor, &endptr, 10);
	if (endptr == cursor || value > 0xFFFFFFFFUL)
		return false;

	*out_value = (u32)value;
	return true;
}

static bool ww_json_get_u32_from_range(const char *json, const char *json_end, const char *key, u32 *out_value)
{
	char token[64];
	const char *cursor;
	char *endptr;
	unsigned long value;

	if (!json || !json_end || !key || !out_value || json >= json_end)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr(json, token);
	if (!cursor || cursor >= json_end)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor || cursor >= json_end)
		return false;
	cursor++;

	while (cursor < json_end && isspace((unsigned char)*cursor))
		cursor++;
	if (cursor >= json_end)
		return false;

	value = strtoul(cursor, &endptr, 10);
	if (endptr == cursor || endptr > json_end || value > 0xFFFFFFFFUL)
		return false;

	*out_value = (u32)value;
	return true;
}

static bool ww_json_get_string_from_range(
		const char *json,
		const char *json_end,
		const char *key,
		char *out_str,
		u32 out_size)
{
	char token[64];
	const char *cursor;
	const char *start;
	const char *end;
	size_t len;

	if (!json || !json_end || !key || !out_str || out_size == 0 || json >= json_end)
		return false;

	snprintf(token, sizeof(token), "\"%s\"", key);
	cursor = strstr(json, token);
	if (!cursor || cursor >= json_end)
		return false;

	cursor = strchr(cursor, ':');
	if (!cursor || cursor >= json_end)
		return false;

	start = strchr(cursor, '"');
	if (!start || start >= json_end)
		return false;
	start++;

	end = strchr(start, '"');
	if (!end || end > json_end)
		return false;

	len = (size_t)(end - start);
	if (len >= out_size)
		len = out_size - 1;

	memcpy(out_str, start, len);
	out_str[len] = '\0';
	return true;
}

static bool ww_json_get_string_from(const char *json, const char *key, char *out_str, u32 out_size)
{
	if (!json)
		return false;

	return ww_json_get_string_from_range(json, json + strlen(json), key, out_str, out_size);
}

static const char *ww_json_find_object_end(const char *object_start, const char *limit)
{
	const char *cursor;
	int depth = 0;
	bool in_string = false;
	bool escaped = false;

	if (!object_start || !limit || object_start >= limit || *object_start != '{')
		return NULL;

	for (cursor = object_start; cursor < limit; cursor++) {
		char ch = *cursor;

		if (in_string) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (ch == '\\') {
				escaped = true;
				continue;
			}
			if (ch == '"')
				in_string = false;
			continue;
		}

		if (ch == '"') {
			in_string = true;
			continue;
		}

		if (ch == '{') {
			depth++;
		} else if (ch == '}') {
			depth--;
			if (depth == 0)
				return cursor;
		}
	}

	return NULL;
}

static const char *ww_json_find_array_end(const char *array_start, const char *limit)
{
	const char *cursor;
	int depth = 0;
	bool in_string = false;
	bool escaped = false;

	if (!array_start || !limit || array_start >= limit || *array_start != '[')
		return NULL;

	for (cursor = array_start; cursor < limit; cursor++) {
		char ch = *cursor;

		if (in_string) {
			if (escaped) {
				escaped = false;
				continue;
			}
			if (ch == '\\') {
				escaped = true;
				continue;
			}
			if (ch == '"')
				in_string = false;
			continue;
		}

		if (ch == '"') {
			in_string = true;
			continue;
		}

		if (ch == '[') {
			depth++;
		} else if (ch == ']') {
			depth--;
			if (depth == 0)
				return cursor;
		}
	}

	return NULL;
}

static bool ww_json_get_moves_from_range(const char *json, const char *json_end, u16 out_moves[4])
{
	const char *moves;
	const char *list_start;
	const char *list_end;
	const char *cursor;
	char *endptr;
	u32 parsed = 0;

	if (!json || !json_end || !out_moves || json >= json_end)
		return false;

	out_moves[0] = 0;
	out_moves[1] = 0;
	out_moves[2] = 0;
	out_moves[3] = 0;

	moves = strstr(json, "\"moves\"");
	if (!moves || moves >= json_end)
		return false;

	list_start = strchr(moves, '[');
	if (!list_start || list_start >= json_end)
		return false;
	list_end = ww_json_find_array_end(list_start, json_end);
	if (!list_end || list_end > json_end)
		return false;

	cursor = list_start + 1;
	while (cursor < list_end && parsed < 4) {
		unsigned long value;

		while (cursor < list_end && (isspace((unsigned char)*cursor) || *cursor == ','))
			cursor++;
		if (cursor >= list_end || *cursor == ']')
			break;

		value = strtoul(cursor, &endptr, 10);
		if (endptr == cursor || endptr > list_end)
			return false;

		if (value <= 0xFFFFUL)
			out_moves[parsed] = (u16)value;
		parsed++;
		cursor = endptr;
	}

	return parsed > 0;
}

static bool ww_json_get_inventory_capture_moves_by_slot(const char *json, u32 target_slot, u16 out_moves[4])
{
	const char *inventory;
	const char *caught;
	const char *list_start;
	const char *list_end;
	const char *cursor;

	if (!json || !out_moves)
		return false;

	inventory = strstr(json, "\"inventory\"");
	if (!inventory)
		return false;

	caught = strstr(inventory, "\"caught\"");
	if (!caught)
		return false;

	list_start = strchr(caught, '[');
	if (!list_start)
		return false;
	list_end = ww_json_find_array_end(list_start, json + strlen(json));
	if (!list_end)
		return false;

	cursor = list_start + 1;
	while (cursor < list_end) {
		const char *entry;
		const char *entry_end;
		u32 slot;

		entry = strchr(cursor, '{');
		if (!entry || entry >= list_end)
			break;

		entry_end = ww_json_find_object_end(entry, list_end + 1);
		if (!entry_end)
			break;

		if (ww_json_get_u32_from_range(entry, entry_end + 1, "slot", &slot) && slot == target_slot)
			return ww_json_get_moves_from_range(entry, entry_end + 1, out_moves);

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_u32_after_token(const char *json, const char *anchor, const char *key, u32 *out_value)
{
	const char *cursor;

	if (!json || !anchor)
		return false;

	cursor = strstr(json, anchor);
	if (!cursor)
		return false;

	return ww_json_get_u32_from(cursor, key, out_value);
}

static bool ww_json_get_string_after_token(
		const char *json,
		const char *anchor,
		const char *key,
		char *out_str,
		u32 out_size)
{
	const char *cursor;

	if (!json || !anchor || !key || !out_str || out_size == 0)
		return false;

	cursor = strstr(json, anchor);
	if (!cursor)
		return false;

	return ww_json_get_string_from(cursor, key, out_str, out_size);
}

static u8 ww_estimate_level_from_exp(u32 exp)
{
	u8 level = 1;

	while (level < 100) {
		u64 next = (u64)(level + 1) * (u64)(level + 1) * (u64)(level + 1);
		if ((u64)exp < next)
			break;
		level++;
	}

	return level;
}

static bool ww_json_get_first_capture(
		const char *json,
		u16 *out_species,
		u8 *out_level,
		u16 out_moves[4],
		char *out_species_name,
		u32 out_species_name_size)
{
	const char *captures;
	const char *applied;
	const char *list_start;
	const char *list_end;
	const char *cursor;
	const char *entry;
	const char *entry_end;
	u32 species;
	u32 level = 10;
	u32 capture_slot = 0;

	if (!json || !out_species || !out_level)
		return false;

	if (out_species_name && out_species_name_size > 0)
		out_species_name[0] = '\0';
	if (out_moves) {
		out_moves[0] = 0;
		out_moves[1] = 0;
		out_moves[2] = 0;
		out_moves[3] = 0;
	}

	captures = strstr(json, "\"captures\"");
	if (!captures)
		return false;

	applied = strstr(captures, "\"applied\"");
	if (!applied)
		return false;

	list_start = strchr(applied, '[');
	if (!list_start)
		return false;
	list_end = ww_json_find_array_end(list_start, json + strlen(json));
	if (!list_end)
		return false;

	cursor = list_start + 1;
	while (cursor < list_end && isspace((unsigned char)*cursor))
		cursor++;
	if (cursor >= list_end || *cursor == ']')
		return false;

	entry = strchr(cursor, '{');
	if (!entry || entry >= list_end)
		return false;
	entry_end = ww_json_find_object_end(entry, list_end + 1);
	if (!entry_end)
		return false;

	if (!ww_json_get_u32_from_range(entry, entry_end + 1, "speciesId", &species))
		return false;
	ww_json_get_u32_from_range(entry, entry_end + 1, "level", &level);
	ww_json_get_u32_from_range(entry, entry_end + 1, "slot", &capture_slot);

	if (out_species_name && out_species_name_size > 0)
		ww_json_get_string_from_range(entry, entry_end + 1, "speciesName", out_species_name, out_species_name_size);

	if (out_moves) {
		if (!ww_json_get_inventory_capture_moves_by_slot(json, capture_slot, out_moves))
			out_moves[0] = 33;
	}

	if (species > 0xFFFF)
		return false;
	if (level == 0 || level > 100)
		level = 10;

	*out_species = (u16)species;
	*out_level = (u8)level;
	return true;
}

static u8 ww_json_get_caught_captures_from_sync(
		const char *sync_json,
		ww_return_capture_choice out_captures[WW_RETURN_CAPTURE_MAX])
{
	const char *domains;
	const char *inventory;
	const char *caught;
	const char *list_start;
	const char *list_end;
	const char *cursor;
	u8 parsed = 0;
	u8 i;

	if (!sync_json || !out_captures)
		return 0;

	for (i = 0; i < WW_RETURN_CAPTURE_MAX; i++)
		memset(&out_captures[i], 0, sizeof(out_captures[i]));

	domains = strstr(sync_json, "\"domains\"");
	if (!domains)
		return 0;

	inventory = strstr(domains, "\"inventory\"");
	if (!inventory)
		return 0;

	caught = strstr(inventory, "\"caught\"");
	if (!caught)
		return 0;

	list_start = strchr(caught, '[');
	if (!list_start)
		return 0;
	list_end = ww_json_find_array_end(list_start, sync_json + strlen(sync_json));
	if (!list_end)
		return 0;

	cursor = list_start + 1;
	while (cursor < list_end && parsed < WW_RETURN_CAPTURE_MAX) {
		const char *entry;
		const char *entry_end;
		u32 species = 0;
		u32 level = 10;
		u32 api_slot = 0;
		ww_return_capture_choice *capture;

		entry = strchr(cursor, '{');
		if (!entry || entry >= list_end)
			break;

		entry_end = ww_json_find_object_end(entry, list_end + 1);
		if (!entry_end)
			break;

		if (!ww_json_get_u32_from_range(entry, entry_end + 1, "speciesId", &species)
				|| species == 0
				|| species > 0xFFFFu) {
			cursor = entry_end + 1;
			continue;
		}

		ww_json_get_u32_from_range(entry, entry_end + 1, "level", &level);
		ww_json_get_u32_from_range(entry, entry_end + 1, "slot", &api_slot);
		if (level == 0 || level > 100)
			level = 10;

		capture = &out_captures[parsed];
		capture->present = true;
		capture->species_id = (u16)species;
		capture->level = (u8)level;
		capture->api_slot = api_slot;
		capture->target_box = 0;
		capture->target_slot = 0;
		capture->moves[0] = 0;
		capture->moves[1] = 0;
		capture->moves[2] = 0;
		capture->moves[3] = 0;

		if (!ww_json_get_string_from_range(
					entry,
					entry_end + 1,
					"speciesName",
					capture->species_name,
					sizeof(capture->species_name))) {
			snprintf(
					capture->species_name,
					sizeof(capture->species_name),
					"%s",
					ww_lookup_species_name(capture->species_id));
		}

		if (!ww_json_get_moves_from_range(entry, entry_end + 1, capture->moves))
			capture->moves[0] = 33;

		parsed++;
		cursor = entry_end + 1;
	}

	return parsed;
}

static bool ww_json_get_configured_route_species_name(
		const char *json,
		u32 route_slot,
		char *out_name,
		u32 out_name_size)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !out_name || out_name_size == 0)
		return false;

	anchor = strstr(json, "\"configuredRouteSlots\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 slot_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "slot", &slot_value) && slot_value == route_slot)
			return ww_json_get_string_from_range(cursor, entry_end + 1, "speciesName", out_name, out_name_size);

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_species_id(const char *json, u32 route_slot, u16 *out_species)
{
	const char *anchor;
	const char *cursor;
	const char *limit;
	u32 species_id;

	if (!json || !out_species)
		return false;

	anchor = strstr(json, "\"configuredRouteSlots\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 slot_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "slot", &slot_value) && slot_value == route_slot) {
			if (!ww_json_get_u32_from_range(cursor, entry_end + 1, "speciesId", &species_id))
				return false;
			if (species_id == 0 || species_id > 0xFFFFu)
				return false;

			*out_species = (u16)species_id;
			return true;
		}

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_slot_u32(
		const char *json,
		u32 route_slot,
		const char *field_name,
		u32 *out_value)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !field_name || !out_value)
		return false;

	anchor = strstr(json, "\"configuredRouteSlots\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 slot_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "slot", &slot_value) && slot_value == route_slot)
			return ww_json_get_u32_from_range(cursor, entry_end + 1, field_name, out_value);

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_item_name(
		const char *json,
		u32 route_item_index,
		char *out_name,
		u32 out_name_size)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !out_name || out_name_size == 0)
		return false;

	anchor = strstr(json, "\"configuredRouteItems\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 item_index_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "routeItemIndex", &item_index_value)
				&& item_index_value == route_item_index) {
			return ww_json_get_string_from_range(cursor, entry_end + 1, "itemName", out_name, out_name_size);
		}

		cursor = entry_end + 1;
	}

	return false;
}

static bool ww_json_get_configured_route_item_id(
		const char *json,
		u32 route_item_index,
		u32 *out_item_id)
{
	const char *anchor;
	const char *cursor;
	const char *limit;

	if (!json || !out_item_id)
		return false;

	anchor = strstr(json, "\"configuredRouteItems\"");
	if (!anchor)
		return false;

	limit = json + strlen(json);
	cursor = anchor;
	while ((cursor = strchr(cursor, '{')) != NULL && cursor < limit) {
		const char *entry_end;
		u32 item_index_value;

		entry_end = ww_json_find_object_end(cursor, limit);
		if (!entry_end)
			return false;

		if (ww_json_get_u32_from_range(cursor, entry_end + 1, "routeItemIndex", &item_index_value)
				&& item_index_value == route_item_index) {
			return ww_json_get_u32_from_range(cursor, entry_end + 1, "itemId", out_item_id);
		}

		cursor = entry_end + 1;
	}

	return false;
}

static u16 ww_read_u16_le(const u8 *ptr)
{
	return (u16)ptr[0] | ((u16)ptr[1] << 8);
}

static u32 ww_read_u32_le(const u8 *ptr)
{
	return (u32)ptr[0] |
			((u32)ptr[1] << 8) |
			((u32)ptr[2] << 16) |
			((u32)ptr[3] << 24);
}

static bool ww_nds_get_file_bounds_from_fat(const u8 *fat, u32 fat_size, u32 file_id, u32 *out_start, u32 *out_end);
static bool ww_read_file_range(FILE *f, u32 offset, u8 *out_buffer, u32 size);

static bool ww_course_record_is_plausible(const u8 *record)
{
	u32 route_plus_one;
	u32 slot;
	u32 valid_species = 0;
	u32 item;
	u32 adv_idx;

	if (!record)
		return false;

	route_plus_one = ww_read_u32_le(record + 0x04);
	if (route_plus_one == 0 || route_plus_one > WW_OV112_ROUTE_IMAGE_COUNT)
		return false;

	for (slot = 0; slot < WW_OV112_SLOT_COUNT; slot++) {
		u32 base = WW_OV112_SLOT_OFFSET + slot * WW_OV112_SLOT_SIZE;
		u16 species_id = ww_read_u16_le(record + base + 0x00);
		u8 level = record[base + 0x02];
		u16 chance = ww_read_u16_le(record + base + 0x12);

		if (species_id > WW_OV112_MAX_SPECIES_ID)
			return false;
		if (species_id != 0)
			valid_species++;
		if (level > 100)
			return false;
		if (species_id != 0 && level == 0)
			return false;
		if (chance > WW_OV112_MAX_SLOT_CHANCE)
			return false;
	}

	if (valid_species < 4)
		return false;

	for (item = 0; item < WW_OV112_ITEM_COUNT; item++) {
		u32 base = WW_OV112_ITEMS_OFFSET + item * WW_OV112_ITEM_SIZE;
		u16 chance = ww_read_u16_le(record + base + 0x04);

		if (chance > WW_OV112_MAX_ITEM_CHANCE)
			return false;
	}

	for (adv_idx = 0; adv_idx < WW_OV112_ADV_TYPES_COUNT; adv_idx++) {
		u8 type_id = record[WW_OV112_ADV_TYPES_OFFSET + adv_idx];

		if (type_id > WW_OV112_MAX_TYPE_ID)
			return false;
	}

	return true;
}

static bool ww_course_table_is_plausible(const u8 *table)
{
	u32 course_id;

	if (!table)
		return false;

	for (course_id = 0; course_id < WW_OV112_COURSE_COUNT; course_id++) {
		const u8 *record = table + course_id * WW_OV112_COURSE_RECORD_SIZE;

		if (!ww_course_record_is_plausible(record))
			return false;
	}

	return true;
}

static bool ww_find_course_table_offset_in_overlay(const u8 *overlay_data, u32 overlay_size, u32 *out_offset)
{
	u32 offset;

	if (!overlay_data || !out_offset)
		return false;
	if (overlay_size < WW_OV112_COURSE_TABLE_SIZE)
		return false;

	for (offset = 0; (u64)offset + (u64)WW_OV112_COURSE_TABLE_SIZE <= (u64)overlay_size; offset += 4u) {
		if (ww_course_table_is_plausible(overlay_data + offset)) {
			*out_offset = offset;
			return true;
		}
	}

	return false;
}

static bool ww_nds_find_arm9_overlay_entry(
		const u8 *overlay_table,
		u32 overlay_table_size,
		u32 overlay_id,
		u32 *out_ram_address,
		u32 *out_file_id,
		u32 *out_compression)
{
	u32 entry_count;
	u32 idx;

	if (!overlay_table || !out_ram_address || !out_file_id || !out_compression)
		return false;
	if (overlay_table_size < WW_NDS_OVERLAY_ENTRY_SIZE)
		return false;

	entry_count = overlay_table_size / WW_NDS_OVERLAY_ENTRY_SIZE;
	for (idx = 0; idx < entry_count; idx++) {
		u32 entry_offset = idx * WW_NDS_OVERLAY_ENTRY_SIZE;
		u32 entry_overlay_id = ww_read_u32_le(overlay_table + entry_offset);

		if (entry_overlay_id != overlay_id)
			continue;

		*out_ram_address = ww_read_u32_le(overlay_table + entry_offset + 4);
		*out_file_id = ww_read_u32_le(overlay_table + entry_offset + 24);
		*out_compression = ww_read_u32_le(overlay_table + entry_offset + 28);
		return true;
	}

	return false;
}

static bool ww_overlay_uncompress_backwards(u8 **in_out_data, u32 *in_out_size)
{
	u8 *buffer;
	u32 compressed_size;
	u32 footer_size;
	u32 extra_size;
	u32 output_size;
	u8 *expanded;
	u8 *end_ptr;
	u8 *end;
	u8 *start;
	u8 *dest;

	if (!in_out_data || !in_out_size || !*in_out_data)
		return false;

	buffer = *in_out_data;
	compressed_size = *in_out_size;
	if (compressed_size < 8)
		return false;

	footer_size = ww_read_u32_le(buffer + compressed_size - 8);
	extra_size = ww_read_u32_le(buffer + compressed_size - 4);
	output_size = compressed_size + extra_size;
	if (output_size <= compressed_size)
		return false;

	expanded = (u8 *)realloc(buffer, output_size);
	if (!expanded)
		return false;

	buffer = expanded;
	*in_out_data = buffer;
	end_ptr = buffer + compressed_size;
	dest = end_ptr + extra_size;
	end = end_ptr - (u8)(footer_size >> 24);
	start = end_ptr - (footer_size & 0x00FFFFFFu);

	if (start < buffer || end < buffer || end > end_ptr || start > end)
		return false;

	while (end > start) {
		u8 flags = *--end;
		int bit;

		for (bit = 0; bit < 8; bit++) {
			if ((flags & 0x80u) == 0) {
				if (end <= start)
					return false;
				*--dest = *--end;
			} else {
				int ip;
				int disp;

				if ((end - start) < 2)
					return false;

				ip = *--end;
				disp = *--end;
				disp = ((disp | (ip << 8)) & ~0xF000) + 2;
				ip += 0x20;

				while (ip >= 0) {
					dest[-1] = dest[disp];
					dest--;
					ip -= 0x10;
				}
			}

			if (end <= start)
				break;

			flags <<= 1;
		}
	}

	*in_out_data = buffer;
	*in_out_size = output_size;
	return true;
}

static bool ww_get_file_identity(const char *path, u64 *out_size, u64 *out_mtime)
{
	struct stat info;

	if (!path || !out_size || !out_mtime)
		return false;

	if (stat(path, &info) != 0)
		return false;
	if (!S_ISREG(info.st_mode))
		return false;

	*out_size = (u64)info.st_size;
	*out_mtime = (u64)info.st_mtime;
	return true;
}

static bool ww_load_overlay112_from_nds(
		const char *nds_path,
		u8 **out_overlay_data,
		u32 *out_overlay_size,
		u32 *out_ram_address)
{
	FILE *f = NULL;
	u8 header[0x58];
	u8 *fat_data = NULL;
	u8 *overlay_table = NULL;
	u8 *overlay_file = NULL;
	u32 overlay_file_size = 0;
	u32 fat_offset;
	u32 fat_size;
	u32 ovt_offset;
	u32 ovt_size;
	u32 ov112_ram_address;
	u32 ov112_file_id;
	u32 ov112_compression;
	u32 file_start;
	u32 file_end;
	long nds_size;
	bool ok = false;

	if (!nds_path || !out_overlay_data || !out_overlay_size || !out_ram_address)
		return false;

	*out_overlay_data = NULL;
	*out_overlay_size = 0;
	*out_ram_address = 0;

	f = fopen(nds_path, "rb");
	if (!f)
		goto cleanup;

	if (fseek(f, 0, SEEK_END) != 0)
		goto cleanup;
	nds_size = ftell(f);
	if (nds_size <= 0)
		goto cleanup;
	if (fseek(f, 0, SEEK_SET) != 0)
		goto cleanup;

	if (fread(header, 1, sizeof(header), f) != sizeof(header))
		goto cleanup;

	fat_offset = ww_read_u32_le(header + 0x48);
	fat_size = ww_read_u32_le(header + 0x4C);
	ovt_offset = ww_read_u32_le(header + 0x50);
	ovt_size = ww_read_u32_le(header + 0x54);

	if (fat_size == 0 || ovt_size == 0)
		goto cleanup;
	if ((u64)fat_offset + (u64)fat_size > (u64)nds_size)
		goto cleanup;
	if ((u64)ovt_offset + (u64)ovt_size > (u64)nds_size)
		goto cleanup;

	fat_data = (u8 *)malloc(fat_size);
	overlay_table = (u8 *)malloc(ovt_size);
	if (!fat_data || !overlay_table)
		goto cleanup;

	if (!ww_read_file_range(f, fat_offset, fat_data, fat_size))
		goto cleanup;
	if (!ww_read_file_range(f, ovt_offset, overlay_table, ovt_size))
		goto cleanup;

	if (!ww_nds_find_arm9_overlay_entry(
				overlay_table,
				ovt_size,
				WW_NDS_OV112_ID,
				&ov112_ram_address,
				&ov112_file_id,
				&ov112_compression)) {
		goto cleanup;
	}

	if (!ww_nds_get_file_bounds_from_fat(fat_data, fat_size, ov112_file_id, &file_start, &file_end))
		goto cleanup;
	if (file_end > (u32)nds_size)
		goto cleanup;

	overlay_file_size = file_end - file_start;
	overlay_file = (u8 *)malloc(overlay_file_size);
	if (!overlay_file)
		goto cleanup;

	if (!ww_read_file_range(f, file_start, overlay_file, overlay_file_size))
		goto cleanup;

	if (((ov112_compression >> 24) & 0x01u) != 0) {
		if (!ww_overlay_uncompress_backwards(&overlay_file, &overlay_file_size))
			goto cleanup;
	}

	*out_overlay_data = overlay_file;
	*out_overlay_size = overlay_file_size;
	*out_ram_address = ov112_ram_address;
	overlay_file = NULL;
	ok = true;

cleanup:
	if (f)
		fclose(f);
	if (fat_data)
		free(fat_data);
	if (overlay_table)
		free(overlay_table);
	if (overlay_file)
		free(overlay_file);
	return ok;
}

static bool ww_extract_course_table_from_nds(
		const char *nds_path,
		u8 out_table[WW_OV112_COURSE_TABLE_SIZE])
{
	u8 *overlay_data = NULL;
	u32 overlay_size = 0;
	u32 ov112_ram_address = 0;
	u32 table_offset;
	u8 candidate_table[WW_OV112_COURSE_TABLE_SIZE];
	bool ok = false;

	if (!nds_path || !out_table)
		return false;

	if (!ww_load_overlay112_from_nds(nds_path, &overlay_data, &overlay_size, &ov112_ram_address))
		goto cleanup;

	if (ov112_ram_address < WW_OV112_COURSE_TABLE_ADDR) {
		table_offset = WW_OV112_COURSE_TABLE_ADDR - ov112_ram_address;
		if ((u64)table_offset + (u64)WW_OV112_COURSE_TABLE_SIZE <= (u64)overlay_size) {
			memcpy(candidate_table, overlay_data + table_offset, WW_OV112_COURSE_TABLE_SIZE);
			if (ww_course_table_is_plausible(candidate_table)) {
				memcpy(out_table, candidate_table, WW_OV112_COURSE_TABLE_SIZE);
				ok = true;
				goto cleanup;
			}
		}
	}

	if (!ww_find_course_table_offset_in_overlay(overlay_data, overlay_size, &table_offset))
		goto cleanup;

	memcpy(out_table, overlay_data + table_offset, WW_OV112_COURSE_TABLE_SIZE);
	ok = true;

cleanup:
	if (overlay_data)
		free(overlay_data);
	return ok;
}

static void ww_ensure_rom_cache_dirs(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/wearwalker_bridge", 0777);
}

static bool ww_load_course_table_cache(
		u64 rom_size,
		u64 rom_mtime,
		u8 out_table[WW_OV112_COURSE_TABLE_SIZE])
{
	FILE *f = NULL;
	ww_rom_cache_header header;
	bool ok = false;

	if (!out_table)
		return false;

	f = fopen(WW_ROM_CACHE_PATH, "rb");
	if (!f)
		goto cleanup;

	if (fread(&header, sizeof(header), 1, f) != 1)
		goto cleanup;
	if (header.magic != WW_ROM_CACHE_MAGIC)
		goto cleanup;
	if (header.version != WW_ROM_CACHE_VERSION)
		goto cleanup;
	if (header.course_count != WW_OV112_COURSE_COUNT)
		goto cleanup;
	if (header.table_size != WW_OV112_COURSE_TABLE_SIZE)
		goto cleanup;
	if (header.rom_size != rom_size || header.rom_mtime != rom_mtime)
		goto cleanup;

	if (fread(out_table, 1, WW_OV112_COURSE_TABLE_SIZE, f) != WW_OV112_COURSE_TABLE_SIZE)
		goto cleanup;
	if (!ww_course_table_is_plausible(out_table))
		goto cleanup;

	ok = true;

cleanup:
	if (f)
		fclose(f);
	return ok;
}

static void ww_save_course_table_cache(
		u64 rom_size,
		u64 rom_mtime,
		const u8 table[WW_OV112_COURSE_TABLE_SIZE])
{
	FILE *f;
	ww_rom_cache_header header;

	if (!table)
		return;

	ww_ensure_rom_cache_dirs();
	f = fopen(WW_ROM_CACHE_PATH, "wb");
	if (!f)
		return;

	memset(&header, 0, sizeof(header));
	header.magic = WW_ROM_CACHE_MAGIC;
	header.version = WW_ROM_CACHE_VERSION;
	header.rom_size = rom_size;
	header.rom_mtime = rom_mtime;
	header.table_size = WW_OV112_COURSE_TABLE_SIZE;
	header.course_count = WW_OV112_COURSE_COUNT;

	if (fwrite(&header, sizeof(header), 1, f) == 1)
		fwrite(table, 1, WW_OV112_COURSE_TABLE_SIZE, f);

	fclose(f);
}

static bool ww_get_course_table_cached(
		const char *nds_path,
		u8 out_table[WW_OV112_COURSE_TABLE_SIZE],
		bool *out_cache_hit)
{
	u64 rom_size;
	u64 rom_mtime;

	if (!nds_path || !out_table)
		return false;

	if (out_cache_hit)
		*out_cache_hit = false;

	if (!ww_get_file_identity(nds_path, &rom_size, &rom_mtime))
		return false;

	if (ww_load_course_table_cache(rom_size, rom_mtime, out_table)) {
		if (out_cache_hit)
			*out_cache_hit = true;
		return true;
	}

	if (!ww_extract_course_table_from_nds(nds_path, out_table))
		return false;
	if (!ww_course_table_is_plausible(out_table))
		return false;

	ww_save_course_table_cache(rom_size, rom_mtime, out_table);
	return true;
}

static bool ww_extract_route_area_sprite_from_nds(
		const char *nds_path,
		u32 route_image_index,
		u8 out_sprite[WW_OV112_ROUTE_IMAGE_SIZE])
{
	u8 *overlay_file = NULL;
	u32 overlay_file_size = 0;
	u32 ov112_ram_address = 0;
	u32 pointer_table_offset;
	u32 pointer_offset;
	u32 sprite_address;
	u32 sprite_offset;
	bool ok = false;

	if (!nds_path || !out_sprite)
		return false;
	if (route_image_index >= WW_OV112_ROUTE_IMAGE_COUNT)
		return false;

	if (!ww_load_overlay112_from_nds(nds_path, &overlay_file, &overlay_file_size, &ov112_ram_address))
		goto cleanup;

	if (ov112_ram_address >= WW_OV112_ROUTE_IMAGE_TABLE_ADDR)
		goto cleanup;

	pointer_table_offset = WW_OV112_ROUTE_IMAGE_TABLE_ADDR - ov112_ram_address;
	pointer_offset = pointer_table_offset + (route_image_index + 1u) * 4u;
	if (pointer_offset + 4u > overlay_file_size)
		goto cleanup;

	sprite_address = ww_read_u32_le(overlay_file + pointer_offset);
	if (sprite_address < ov112_ram_address)
		goto cleanup;

	sprite_offset = sprite_address - ov112_ram_address;
	if (sprite_offset + WW_OV112_ROUTE_IMAGE_SIZE > overlay_file_size)
		goto cleanup;

	memcpy(out_sprite, overlay_file + sprite_offset, WW_OV112_ROUTE_IMAGE_SIZE);
	ok = true;

cleanup:
	if (overlay_file)
		free(overlay_file);
	return ok;
}

static bool ww_nds_find_file_id_from_fnt(const u8 *fnt, u32 fnt_size, const char *path, u32 *out_file_id)
{
	char path_copy[128];
	char components[8][32];
	char *saveptr = NULL;
	char *token;
	u32 component_count = 0;
	u16 total_dirs;
	u16 current_dir_id = 0xF000;
	u32 part;

	if (!fnt || !path || !out_file_id || fnt_size < 8)
		return false;

	total_dirs = ww_read_u16_le(fnt + 6);
	if (total_dirs == 0)
		return false;

	snprintf(path_copy, sizeof(path_copy), "%s", path);
	token = strtok_r(path_copy, "/", &saveptr);
	while (token && component_count < 8) {
		if (token[0]) {
			snprintf(components[component_count], sizeof(components[component_count]), "%s", token);
			component_count++;
		}
		token = strtok_r(NULL, "/", &saveptr);
	}

	if (component_count == 0)
		return false;

	for (part = 0; part < component_count; part++) {
		u32 dir_index;
		u32 dir_entry_offset;
		u32 subtable_offset;
		u32 cursor;
		u16 file_id;
		bool found = false;
		bool last_component = (part + 1 == component_count);
		u32 target_len = (u32)strlen(components[part]);

		if (current_dir_id < 0xF000)
			return false;

		dir_index = (u32)(current_dir_id - 0xF000);
		if (dir_index >= total_dirs)
			return false;

		dir_entry_offset = dir_index * 8;
		if (dir_entry_offset + 8 > fnt_size)
			return false;

		subtable_offset = ww_read_u32_le(fnt + dir_entry_offset);
		file_id = ww_read_u16_le(fnt + dir_entry_offset + 4);
		cursor = subtable_offset;
		if (cursor >= fnt_size)
			return false;

		while (cursor < fnt_size) {
			u8 type_len;
			u32 name_len;
			bool is_dir;
			bool name_match;

			type_len = fnt[cursor++];
			if (type_len == 0)
				break;

			name_len = (u32)(type_len & 0x7F);
			is_dir = (type_len & 0x80u) != 0;
			if (cursor + name_len > fnt_size)
				return false;

			name_match = (target_len == name_len) && (memcmp(fnt + cursor, components[part], name_len) == 0);
			cursor += name_len;

			if (is_dir) {
				u16 child_dir_id;

				if (cursor + 2 > fnt_size)
					return false;
				child_dir_id = ww_read_u16_le(fnt + cursor);
				cursor += 2;

				if (name_match) {
					if (last_component)
						return false;
					current_dir_id = child_dir_id;
					found = true;
					break;
				}
			} else {
				if (name_match) {
					if (!last_component)
						return false;
					*out_file_id = file_id;
					return true;
				}
				file_id++;
			}
		}

		if (!found)
			return false;
	}

	return false;
}

static bool ww_nds_get_file_bounds_from_fat(const u8 *fat, u32 fat_size, u32 file_id, u32 *out_start, u32 *out_end)
{
	u32 entry_offset;
	u32 entry_count;
	u32 start;
	u32 end;

	if (!fat || !out_start || !out_end || fat_size < 8)
		return false;

	entry_count = fat_size / 8;
	if (file_id >= entry_count)
		return false;

	entry_offset = file_id * 8;
	if (entry_offset + 8 > fat_size)
		return false;

	start = ww_read_u32_le(fat + entry_offset);
	end = ww_read_u32_le(fat + entry_offset + 4);
	if (start >= end)
		return false;

	*out_start = start;
	*out_end = end;
	return true;
}

static bool ww_read_file_range(FILE *f, u32 offset, u8 *out_buffer, u32 size)
{
	if (!f || !out_buffer || size == 0)
		return false;

	if (fseek(f, (long)offset, SEEK_SET) != 0)
		return false;

	return fread(out_buffer, 1, size, f) == size;
}

static bool ww_load_narc_from_nds(const char *nds_path, const char *narc_path, u8 **out_narc_data, u32 *out_narc_size)
{
	FILE *f = NULL;
	u8 header[0x50];
	u8 *fnt_data = NULL;
	u8 *fat_data = NULL;
	u8 *narc_data = NULL;
	u32 fnt_offset;
	u32 fnt_size;
	u32 fat_offset;
	u32 fat_size;
	u32 file_id;
	u32 file_start;
	u32 file_end;
	long nds_size;
	bool ok = false;

	if (!nds_path || !narc_path || !out_narc_data || !out_narc_size)
		return false;

	*out_narc_data = NULL;
	*out_narc_size = 0;

	f = fopen(nds_path, "rb");
	if (!f)
		goto cleanup;

	if (fseek(f, 0, SEEK_END) != 0)
		goto cleanup;
	nds_size = ftell(f);
	if (nds_size <= 0)
		goto cleanup;
	if (fseek(f, 0, SEEK_SET) != 0)
		goto cleanup;

	if (fread(header, 1, sizeof(header), f) != sizeof(header))
		goto cleanup;

	fnt_offset = ww_read_u32_le(header + 0x40);
	fnt_size = ww_read_u32_le(header + 0x44);
	fat_offset = ww_read_u32_le(header + 0x48);
	fat_size = ww_read_u32_le(header + 0x4C);

	if (fnt_size == 0 || fat_size == 0)
		goto cleanup;
	if ((u64)fnt_offset + (u64)fnt_size > (u64)nds_size)
		goto cleanup;
	if ((u64)fat_offset + (u64)fat_size > (u64)nds_size)
		goto cleanup;

	fnt_data = (u8 *)malloc(fnt_size);
	fat_data = (u8 *)malloc(fat_size);
	if (!fnt_data || !fat_data)
		goto cleanup;

	if (!ww_read_file_range(f, fnt_offset, fnt_data, fnt_size))
		goto cleanup;
	if (!ww_read_file_range(f, fat_offset, fat_data, fat_size))
		goto cleanup;

	if (!ww_nds_find_file_id_from_fnt(fnt_data, fnt_size, narc_path, &file_id))
		goto cleanup;

	if (!ww_nds_get_file_bounds_from_fat(fat_data, fat_size, file_id, &file_start, &file_end))
		goto cleanup;
	if (file_end > (u32)nds_size)
		goto cleanup;

	narc_data = (u8 *)malloc(file_end - file_start);
	if (!narc_data)
		goto cleanup;
	if (!ww_read_file_range(f, file_start, narc_data, file_end - file_start))
		goto cleanup;

	*out_narc_data = narc_data;
	*out_narc_size = file_end - file_start;
	narc_data = NULL;
	ok = true;

cleanup:
	if (f)
		fclose(f);
	if (fnt_data)
		free(fnt_data);
	if (fat_data)
		free(fat_data);
	if (narc_data)
		free(narc_data);
	return ok;
}

static bool ww_narc_get_file_by_index(const u8 *narc, u32 narc_size, u32 file_index, const u8 **out_file, u32 *out_size)
{
	u32 offset = 16;
	u16 chunk_count;
	u32 i;
	u32 btaf_offset = 0;
	u32 gmif_offset = 0;
	u32 btaf_size;
	u16 num_files;
	u32 entry_offset;
	u32 data_start;
	u32 data_end;
	u32 gmif_data_start;
	u32 gmif_size;
	u32 gmif_data_size;

	if (!narc || !out_file || !out_size || narc_size < 16)
		return false;
	if (memcmp(narc, "NARC", 4) != 0)
		return false;

	chunk_count = ww_read_u16_le(narc + 0x0E);
	for (i = 0; i < chunk_count; i++) {
		u32 chunk_size;

		if (offset + 8 > narc_size)
			return false;

		chunk_size = ww_read_u32_le(narc + offset + 4);
		if (chunk_size < 8 || offset + chunk_size > narc_size)
			return false;

		if (memcmp(narc + offset, "BTAF", 4) == 0)
			btaf_offset = offset;
		else if (memcmp(narc + offset, "GMIF", 4) == 0)
			gmif_offset = offset;

		offset += chunk_size;
	}

	if (!btaf_offset || !gmif_offset)
		return false;

	btaf_size = ww_read_u32_le(narc + btaf_offset + 4);
	if (btaf_offset + btaf_size > narc_size || btaf_size < 12)
		return false;

	num_files = ww_read_u16_le(narc + btaf_offset + 8);
	if (file_index >= num_files)
		return false;

	entry_offset = btaf_offset + 12 + file_index * 8;
	if (entry_offset + 8 > btaf_offset + btaf_size)
		return false;

	data_start = ww_read_u32_le(narc + entry_offset);
	data_end = ww_read_u32_le(narc + entry_offset + 4);
	if (data_start > data_end)
		return false;

	gmif_size = ww_read_u32_le(narc + gmif_offset + 4);
	if (gmif_size < 8 || gmif_offset + gmif_size > narc_size)
		return false;

	gmif_data_start = gmif_offset + 8;
	gmif_data_size = gmif_size - 8;
	if (data_end > gmif_data_size)
		return false;

	*out_file = narc + gmif_data_start + data_start;
	*out_size = data_end - data_start;
	return true;
}

static bool ww_decompress_lz10(const u8 *compressed, u32 compressed_size, u8 **out_data, u32 *out_size)
{
	u32 decompressed_size;
	u8 *output;

	if (!compressed || !out_data || !out_size || compressed_size < 4)
		return false;
	if (compressed[0] != 0x10)
		return false;

	decompressed_size = (u32)compressed[1] |
			((u32)compressed[2] << 8) |
			((u32)compressed[3] << 16);
	if (decompressed_size == 0)
		return false;

	output = (u8 *)malloc(decompressed_size);
	if (!output)
		return false;

	if (!decompress(output, decompressed_size, NULL, (void *)compressed, compressed_size)) {
		free(output);
		return false;
	}

	*out_data = output;
	*out_size = decompressed_size;
	return true;
}

static void ww_remap_sprite_2bpp_contrast(u8 *sprite_data, u32 size)
{
	/* Preserve native HGSS 2bpp shades to avoid over-thick black lines on Pokewalker LCD. */
	static const u8 shade_map[4] = {0, 1, 2, 3};
	u32 i;

	if (!sprite_data || size == 0)
		return;

	for (i = 0; i < size; i++) {
		u8 value = sprite_data[i];
		u8 p0 = shade_map[value & 0x03u];
		u8 p1 = shade_map[(value >> 2) & 0x03u];
		u8 p2 = shade_map[(value >> 4) & 0x03u];
		u8 p3 = shade_map[(value >> 6) & 0x03u];

		sprite_data[i] = (u8)(p0 | (p1 << 2) | (p2 << 4) | (p3 << 6));
	}
}

static bool ww_extract_species_large_frames(
		const u8 *narc_data,
		u32 narc_size,
		u16 species_id,
		u8 out_large0[0x300],
		u8 out_large1[0x300])
{
	u8 *decompressed = NULL;
	u32 sprite_index;
	const u8 *compressed;
	u32 compressed_size;
	u32 decompressed_size;
	bool ok = false;

	if (!narc_data || narc_size == 0 || !out_large0 || !out_large1 || species_id == 0)
		return false;

	sprite_index = (u32)species_id - 1;
	if (!ww_narc_get_file_by_index(narc_data, narc_size, sprite_index, &compressed, &compressed_size))
		goto cleanup;

	if (!ww_decompress_lz10(compressed, compressed_size, &decompressed, &decompressed_size))
		goto cleanup;

	if (decompressed_size < 0x600)
		goto cleanup;

	memcpy(out_large0, decompressed, 0x300);
	memcpy(out_large1, decompressed + 0x300, 0x300);
	ok = true;

cleanup:
	if (decompressed)
		free(decompressed);
	return ok;
}

static bool ww_extract_species_small_frames(
		const u8 *narc_data,
		u32 narc_size,
		u16 species_id,
		u8 out_small0[0x0C0],
		u8 out_small1[0x0C0])
{
	u8 *decompressed = NULL;
	u32 sprite_index;
	const u8 *compressed;
	u32 compressed_size;
	u32 decompressed_size;
	bool ok = false;

	if (!narc_data || narc_size == 0 || !out_small0 || !out_small1 || species_id == 0)
		return false;

	sprite_index = (u32)species_id;
	if (!ww_narc_get_file_by_index(narc_data, narc_size, sprite_index, &compressed, &compressed_size))
		goto cleanup;

	if (!ww_decompress_lz10(compressed, compressed_size, &decompressed, &decompressed_size))
		goto cleanup;

	if (decompressed_size < 0x180)
		goto cleanup;

	memcpy(out_small0, decompressed, 0x0C0);
	memcpy(out_small1, decompressed + 0x0C0, 0x0C0);
	ok = true;

cleanup:
	if (decompressed)
		free(decompressed);
	return ok;
}

static bool ww_nitro_find_chunk(const u8 *data, u32 data_size, const char chunk_id[4], u32 *out_offset, u32 *out_size)
{
	u32 offset = 16;
	u16 chunk_count;
	u32 chunk_index;

	if (!data || !chunk_id || !out_offset || !out_size || data_size < 16)
		return false;

	chunk_count = ww_read_u16_le(data + 0x0E);
	for (chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
		u32 chunk_size;

		if (offset + 8 > data_size)
			return false;

		chunk_size = ww_read_u32_le(data + offset + 4);
		if (chunk_size < 8 || offset + chunk_size > data_size)
			return false;

		if (memcmp(data + offset, chunk_id, 4) == 0) {
			*out_offset = offset;
			*out_size = chunk_size;
			return true;
		}

		offset += chunk_size;
	}

	return false;
}

static bool ww_unpack_narc_member_lz10(
		const u8 *member_data,
		u32 member_size,
		const u8 **out_data,
		u32 *out_size,
		u8 **out_allocated)
{
	u8 *decompressed = NULL;
	u32 decompressed_size = 0;

	if (!member_data || !out_data || !out_size || !out_allocated || member_size == 0)
		return false;

	*out_data = NULL;
	*out_size = 0;
	*out_allocated = NULL;

	if (member_data[0] == 0x10) {
		if (!ww_decompress_lz10(member_data, member_size, &decompressed, &decompressed_size))
			return false;

		*out_data = decompressed;
		*out_size = decompressed_size;
		*out_allocated = decompressed;
		return true;
	}

	*out_data = member_data;
	*out_size = member_size;
	return true;
}

static u32 ww_bgr555_to_c2d_color(u16 bgr555, bool transparent)
{
	u8 r5 = (u8)(bgr555 & 0x1Fu);
	u8 g5 = (u8)((bgr555 >> 5) & 0x1Fu);
	u8 b5 = (u8)((bgr555 >> 10) & 0x1Fu);
	u8 r8 = (u8)((r5 << 3) | (r5 >> 2));
	u8 g8 = (u8)((g5 << 3) | (g5 >> 2));
	u8 b8 = (u8)((b5 << 3) | (b5 >> 2));

	return C2D_Color32(r8, g8, b8, transparent ? 0x00 : 0xFF);
}

static bool ww_decode_ncgr_32x32_4bpp(const u8 *ncgr_data, u32 ncgr_size, u8 out_indices[32 * 32], u8 *out_width, u8 *out_height)
{
	u32 rahc_offset;
	u32 rahc_size;
	u32 pixel_data_size;
	const u8 *pixel_data;
	u32 tile_index;
	u32 nonzero = 0;

	if (!ncgr_data || !out_indices || !out_width || !out_height)
		return false;
	if (ncgr_size < 16 || memcmp(ncgr_data, "RGCN", 4) != 0)
		return false;
	if (!ww_nitro_find_chunk(ncgr_data, ncgr_size, "RAHC", &rahc_offset, &rahc_size))
		return false;
	if (rahc_size < 0x20 || rahc_offset + 0x20 > ncgr_size)
		return false;

	pixel_data_size = ww_read_u32_le(ncgr_data + rahc_offset + 0x18);
	if (pixel_data_size < 0x200)
		return false;
	if (rahc_offset + 0x20 + pixel_data_size > ncgr_size)
		return false;

	pixel_data = ncgr_data + rahc_offset + 0x20;
	memset(out_indices, 0, 32 * 32);

	for (tile_index = 0; tile_index < 16; tile_index++) {
		const u8 *tile = pixel_data + tile_index * 32u;
		u32 tile_x = tile_index % 4u;
		u32 tile_y = tile_index / 4u;
		u32 py;

		for (py = 0; py < 8u; py++) {
			u32 px;

			for (px = 0; px < 8u; px++) {
				u8 packed = tile[py * 4u + (px >> 1u)];
				u8 color_index = (px & 1u) ? (packed >> 4) : (packed & 0x0Fu);
				u32 out_x = tile_x * 8u + px;
				u32 out_y = tile_y * 8u + py;

				if (color_index != 0)
					nonzero++;
				out_indices[out_y * 32u + out_x] = color_index;
			}
		}
	}

	if (nonzero < 8u)
		return false;

	*out_width = 32;
	*out_height = 32;
	return true;
}

static bool ww_decode_nclr_colors(const u8 *nclr_data, u32 nclr_size, u32 *out_colors, u32 out_cap, u32 *out_color_count)
{
	u32 ttlp_offset;
	u32 ttlp_size;
	u32 palette_size;
	u32 color_count;
	const u8 *palette_data;
	u32 i;

	if (!nclr_data || !out_colors || out_cap == 0 || !out_color_count)
		return false;
	if (nclr_size < 16 || memcmp(nclr_data, "RLCN", 4) != 0)
		return false;
	if (!ww_nitro_find_chunk(nclr_data, nclr_size, "TTLP", &ttlp_offset, &ttlp_size))
		return false;
	if (ttlp_size < 0x18 || ttlp_offset + 0x18 > nclr_size)
		return false;

	palette_size = ww_read_u32_le(nclr_data + ttlp_offset + 0x10);
	if (palette_size < 32)
		return false;
	if (ttlp_offset + 0x18 + palette_size > nclr_size)
		return false;

	palette_data = nclr_data + ttlp_offset + 0x18;
	color_count = palette_size / 2u;
	if (color_count > out_cap)
		color_count = out_cap;

	for (i = 0; i < color_count; i++) {
		u16 color555 = ww_read_u16_le(palette_data + i * 2u);
		out_colors[i] = ww_bgr555_to_c2d_color(color555, false);
	}

	*out_color_count = color_count;

	return color_count >= 16u;
}

static bool ww_copy_palette16(const u32 *src_colors, u32 src_count, u32 palette_index, u32 out_palette[16])
{
	u32 base;
	u32 i;

	if (!src_colors || !out_palette)
		return false;

	base = palette_index * 16u;
	if (base + 16u > src_count)
		return false;

	for (i = 0; i < 16u; i++) {
		u32 color = src_colors[base + i];
		if (i == 0u)
			color &= 0x00FFFFFFu;
		else
			color = (color & 0x00FFFFFFu) | 0xFF000000u;
		out_palette[i] = color;
	}

	return true;
}

static u32 ww_species_to_icon_member_index(u16 species_id)
{
	if (species_id >= 1u && species_id <= 493u)
		return (u32)species_id + 7u;
	return 7u;
}

static u32 ww_species_to_icon_palette_index(u16 species_id)
{
	if (species_id < WW_HGSS_MON_ICON_PALETTE_COUNT)
		return (u32)g_hgss_mon_icon_palette_idx[species_id];
	return 0u;
}

static bool ww_extract_species_color_icon_from_narc(
		const u8 *poke_icon_narc,
		u32 poke_icon_narc_size,
		u16 species_id,
		u8 out_indices[32 * 32],
		u32 out_palette[16],
		u8 *out_width,
		u8 *out_height)
{
	u32 icon_member_index;
	u32 palette_index;
	const u8 *icon_member;
	u32 icon_member_size;
	const u8 *palette_member;
	u32 palette_member_size;
	const u8 *icon_data;
	u32 icon_size;
	const u8 *palette_data;
	u32 palette_size;
	u8 *owned_icon = NULL;
	u8 *owned_palette = NULL;
	u32 colors[256];
	u32 color_count = 0;
	bool ok = false;

	if (!poke_icon_narc || poke_icon_narc_size == 0 || !out_indices || !out_palette || !out_width || !out_height)
		return false;

	icon_member_index = ww_species_to_icon_member_index(species_id);
	palette_index = ww_species_to_icon_palette_index(species_id);

	if (!ww_narc_get_file_by_index(poke_icon_narc, poke_icon_narc_size, icon_member_index, &icon_member, &icon_member_size))
		return false;
	if (!ww_narc_get_file_by_index(poke_icon_narc, poke_icon_narc_size, 0u, &palette_member, &palette_member_size))
		return false;
	if (!ww_unpack_narc_member_lz10(icon_member, icon_member_size, &icon_data, &icon_size, &owned_icon))
		goto cleanup;
	if (!ww_unpack_narc_member_lz10(palette_member, palette_member_size, &palette_data, &palette_size, &owned_palette))
		goto cleanup;
	if (!ww_decode_ncgr_32x32_4bpp(icon_data, icon_size, out_indices, out_width, out_height))
		goto cleanup;
	if (!ww_decode_nclr_colors(palette_data, palette_size, colors, sizeof(colors) / sizeof(colors[0]), &color_count))
		goto cleanup;
	if (!ww_copy_palette16(colors, color_count, palette_index, out_palette))
		goto cleanup;

	ok = true;

cleanup:
	if (owned_icon)
		free(owned_icon);
	if (owned_palette)
		free(owned_palette);
	return ok;
}

static bool ww_decode_item_icon_nclr(const u8 *nclr_data, u32 nclr_size, u32 out_palette[16])
{
	u32 colors[256];
	u32 color_count = 0;

	if (!out_palette)
		return false;
	if (!ww_decode_nclr_colors(nclr_data, nclr_size, colors, sizeof(colors) / sizeof(colors[0]), &color_count))
		return false;
	return ww_copy_palette16(colors, color_count, 0u, out_palette);
}

static bool ww_lookup_item_icon_indices(u16 item_id, u16 *out_ncgr_index, u16 *out_nclr_index)
{
	if (!out_ncgr_index || !out_nclr_index)
		return false;
	if (item_id >= WW_HGSS_ITEM_ICON_COUNT)
		return false;

	*out_ncgr_index = g_hgss_item_icon_ncgr[item_id];
	*out_nclr_index = g_hgss_item_icon_nclr[item_id];
	return true;
}

static bool ww_extract_item_icon_from_narc(
		const u8 *item_icon_narc,
		u32 item_icon_narc_size,
		u16 item_id,
		u8 out_indices[32 * 32],
		u32 out_palette[16],
		u8 *out_width,
		u8 *out_height)
{
	u16 ncgr_index;
	u16 nclr_index;
	const u8 *ncgr_member;
	u32 ncgr_member_size;
	const u8 *nclr_member;
	u32 nclr_member_size;
	const u8 *ncgr_data;
	u32 ncgr_size;
	const u8 *nclr_data;
	u32 nclr_size;
	u8 *owned_ncgr = NULL;
	u8 *owned_nclr = NULL;
	bool ok = false;

	if (!item_icon_narc || item_icon_narc_size == 0 || !out_indices || !out_palette || !out_width || !out_height)
		return false;
	if (!ww_lookup_item_icon_indices(item_id, &ncgr_index, &nclr_index))
		return false;
	if (!ww_narc_get_file_by_index(item_icon_narc, item_icon_narc_size, ncgr_index, &ncgr_member, &ncgr_member_size))
		return false;
	if (!ww_narc_get_file_by_index(item_icon_narc, item_icon_narc_size, nclr_index, &nclr_member, &nclr_member_size))
		return false;
	if (!ww_unpack_narc_member_lz10(ncgr_member, ncgr_member_size, &ncgr_data, &ncgr_size, &owned_ncgr))
		goto cleanup;
	if (!ww_unpack_narc_member_lz10(nclr_member, nclr_member_size, &nclr_data, &nclr_size, &owned_nclr))
		goto cleanup;
	if (!ww_decode_ncgr_32x32_4bpp(ncgr_data, ncgr_size, out_indices, out_width, out_height))
		goto cleanup;
	if (!ww_decode_item_icon_nclr(nclr_data, nclr_size, out_palette))
		goto cleanup;

	ok = true;

cleanup:
	if (owned_ncgr)
		free(owned_ncgr);
	if (owned_nclr)
		free(owned_nclr);
	return ok;
}

static void ww_patch_sprite_block_counted(const char *key, const u8 *data, u32 size, u32 *applied, u32 *failed)
{
	if (ww_api_stroll_patch_sprite_block(key, data, size, NULL, 0)) {
		if (applied)
			(*applied)++;
	} else {
		if (failed)
			(*failed)++;
	}
}

static void ww_patch_species_image_sprites(
		const u8 *small_narc_data,
		u32 small_narc_size,
		const u8 *large_narc_data,
		u32 large_narc_size,
		u16 species_id,
		const char *small0_key,
		const char *small1_key,
		const char *large0_key,
		const char *large1_key,
		u32 *applied,
		u32 *failed)
{
	u8 large0[0x300];
	u8 large1[0x300];
	u8 small0[0x0C0];
	u8 small1[0x0C0];

	if (small0_key && small1_key) {
		if (!ww_extract_species_small_frames(small_narc_data, small_narc_size, species_id, small0, small1)) {
			if (failed)
				*failed += 2;
		} else {
			ww_remap_sprite_2bpp_contrast(small0, sizeof(small0));
			ww_remap_sprite_2bpp_contrast(small1, sizeof(small1));
			ww_patch_sprite_block_counted(small0_key, small0, sizeof(small0), applied, failed);
			ww_patch_sprite_block_counted(small1_key, small1, sizeof(small1), applied, failed);
		}
	}

	if (large0_key && large1_key) {
		if (!ww_extract_species_large_frames(large_narc_data, large_narc_size, species_id, large0, large1)) {
			if (failed)
				*failed += 2;
		} else {
			ww_remap_sprite_2bpp_contrast(large0, sizeof(large0));
			ww_remap_sprite_2bpp_contrast(large1, sizeof(large1));
			ww_patch_sprite_block_counted(large0_key, large0, sizeof(large0), applied, failed);
			ww_patch_sprite_block_counted(large1_key, large1, sizeof(large1), applied, failed);
		}
	}
}

static u32 ww_lcg_next(u32 *state)
{
	if (!state)
		return 0;

	*state = (*state * 1664525u) + 1013904223u;
	return *state;
}

static u32 ww_lcg_pair_pick(u32 *state)
{
	/* Do not use LCG LSB: it alternates predictably and creates obvious pair patterns. */
	return (ww_lcg_next(state) >> 16) & 0x1u;
}

static void ww_sanitize_json_ascii(const char *src, char *dst, size_t dst_size)
{
	size_t pos = 0;

	if (!dst || dst_size == 0)
		return;

	if (!src) {
		dst[0] = '\0';
		return;
	}

	while (*src && pos + 1 < dst_size) {
		unsigned char c = (unsigned char)*src++;
		if (c < 0x20 || c == '"' || c == '\\')
			dst[pos++] = '?';
		else
			dst[pos++] = (char)c;
	}

	dst[pos] = '\0';
}

static bool ww_json_append(char **cursor, size_t *remaining, const char *fmt, ...)
{
	va_list args;
	int written;

	if (!cursor || !remaining || !*cursor || !fmt || *remaining == 0)
		return false;

	va_start(args, fmt);
	written = vsnprintf(*cursor, *remaining, fmt, args);
	va_end(args);

	if (written < 0 || (size_t)written >= *remaining)
		return false;

	*cursor += written;
	*remaining -= (size_t)written;
	return true;
}

static bool ww_build_resolved_stroll_send_json(
		const hgss_stroll_send_context *send_context,
		u8 level,
		u32 course_id,
		bool force_route_seed,
		u32 forced_route_seed,
		bool clear_buffers,
		bool allow_locked_course,
		const u8 course_table[WW_OV112_COURSE_TABLE_SIZE],
		u64 rom_size,
		u64 rom_mtime,
		u32 *out_route_image_index,
		u32 *out_route_seed,
		char *out_body,
		u32 out_body_size)
{
	const u8 *record;
	char safe_nickname[48];
	char *cursor;
	size_t remaining;
	u32 route_plus_one;
	u32 route_image_index;
	u32 route_seed;
	u8 adv_type0;
	u8 adv_type1;
	u8 adv_type2;
	u32 group;
	u32 item_index;

	if (!send_context || !course_table || !out_body || out_body_size < 256)
		return false;
	if (course_id >= WW_OV112_COURSE_COUNT)
		return false;

	record = course_table + course_id * WW_OV112_COURSE_RECORD_SIZE;
	route_plus_one = ww_read_u32_le(record + 0x04);
	if (route_plus_one > 0 && route_plus_one <= WW_OV112_ROUTE_IMAGE_COUNT) {
		route_image_index = route_plus_one - 1u;
	} else {
		u32 route_plus_one_low = route_plus_one & 0xFFu;

		if (route_plus_one_low > 0 && route_plus_one_low <= WW_OV112_ROUTE_IMAGE_COUNT)
			route_image_index = route_plus_one_low - 1u;
		else if (course_id < WW_OV112_ROUTE_IMAGE_COUNT)
			route_image_index = course_id;
		else
			route_image_index = course_id % WW_OV112_ROUTE_IMAGE_COUNT;
	}
	if (force_route_seed) {
		route_seed = forced_route_seed;
	} else {
		route_seed = (course_id + 1u) * 2654435761u;
		route_seed ^= ((u32)send_context->source_slot.species_id << 16);
		route_seed ^= send_context->source_slot.exp;
	}
	adv_type0 = record[WW_OV112_ADV_TYPES_OFFSET + 0u];
	adv_type1 = record[WW_OV112_ADV_TYPES_OFFSET + 1u];
	adv_type2 = record[WW_OV112_ADV_TYPES_OFFSET + 2u];
	if (route_seed == 0)
		route_seed = 0xA5A55A5Au;

	if (out_route_image_index)
		*out_route_image_index = route_image_index;
	if (out_route_seed)
		*out_route_seed = route_seed;

	ww_sanitize_json_ascii(send_context->nickname, safe_nickname, sizeof(safe_nickname));
	cursor = out_body;
	remaining = out_body_size;

	if (!ww_json_append(
				&cursor,
				&remaining,
				"{\"speciesId\":%u,\"level\":%u,\"courseId\":%u,\"nickname\":\"%s\",\"friendship\":%u,\"heldItem\":%u,\"moves\":[%u,%u,%u,%u],\"variantFlags\":%u,\"specialFlags\":%u,\"clearBuffers\":%s,\"allowLockedCourse\":%s,\"resolvedRouteConfig\":{\"schemaVersion\":1,\"romSize\":%llu,\"romMtime\":%llu,\"routeImageIndex\":%u,\"routeSeed\":%u,\"advantagedTypes\":[%u,%u,%u],\"slots\":[",
				(unsigned)send_context->source_slot.species_id,
				(unsigned)level,
				(unsigned)course_id,
				safe_nickname,
				(unsigned)send_context->source_slot.friendship,
				(unsigned)send_context->held_item,
				(unsigned)send_context->moves[0],
				(unsigned)send_context->moves[1],
				(unsigned)send_context->moves[2],
				(unsigned)send_context->moves[3],
				(unsigned)send_context->variant_flags,
				(unsigned)send_context->special_flags,
				clear_buffers ? "true" : "false",
				allow_locked_course ? "true" : "false",
				(unsigned long long)rom_size,
				(unsigned long long)rom_mtime,
				(unsigned)route_image_index,
				(unsigned)route_seed,
				(unsigned)adv_type0,
				(unsigned)adv_type1,
				(unsigned)adv_type2)) {
		return false;
	}

	for (group = 0; group < 3; group++) {
		u32 pair_base = group * 2u;
		u32 pair_pick = ww_lcg_pair_pick(&route_seed);
		u32 source_pair_index = pair_base + pair_pick;
		u32 base = WW_OV112_SLOT_OFFSET + source_pair_index * WW_OV112_SLOT_SIZE;
		u16 species_id = ww_read_u16_le(record + base + 0x00);
		u8 slot_level = record[base + 0x02];
		u8 gender = record[base + 0x07];
		u16 move0 = ww_read_u16_le(record + base + 0x08);
		u16 move1 = ww_read_u16_le(record + base + 0x0A);
		u16 move2 = ww_read_u16_le(record + base + 0x0C);
		u16 move3 = ww_read_u16_le(record + base + 0x0E);
		u16 min_steps = ww_read_u16_le(record + base + 0x10);
		u16 chance_raw = ww_read_u16_le(record + base + 0x12);
		u8 chance = chance_raw > 0xFFu ? 0xFFu : (u8)chance_raw;
		char safe_species_name[48];

		if (species_id == 0) {
			u32 alt_pair_index = pair_base + (pair_pick ^ 0x1u);
			u32 alt_base = WW_OV112_SLOT_OFFSET + alt_pair_index * WW_OV112_SLOT_SIZE;
			u16 alt_species_id = ww_read_u16_le(record + alt_base + 0x00);

			if (alt_species_id != 0) {
				source_pair_index = alt_pair_index;
				base = alt_base;
				species_id = alt_species_id;
				slot_level = record[base + 0x02];
				gender = record[base + 0x07];
				move0 = ww_read_u16_le(record + base + 0x08);
				move1 = ww_read_u16_le(record + base + 0x0A);
				move2 = ww_read_u16_le(record + base + 0x0C);
				move3 = ww_read_u16_le(record + base + 0x0E);
				min_steps = ww_read_u16_le(record + base + 0x10);
				chance_raw = ww_read_u16_le(record + base + 0x12);
				chance = chance_raw > 0xFFu ? 0xFFu : (u8)chance_raw;
			} else {
				species_id = send_context->source_slot.species_id;
				if (species_id == 0)
					species_id = 1;
			}
		}

		if (slot_level == 0)
			slot_level = 1;
		if (slot_level > 100)
			slot_level = 100;

		ww_sanitize_json_ascii(ww_lookup_species_name(species_id), safe_species_name, sizeof(safe_species_name));

		if (!ww_json_append(
					&cursor,
					&remaining,
					"%s{\"slot\":%u,\"sourcePairIndex\":%u,\"speciesId\":%u,\"speciesName\":\"%s\",\"level\":%u,\"gender\":%u,\"moves\":[%u,%u,%u,%u],\"minSteps\":%u,\"chance\":%u}",
					group == 0 ? "" : ",",
					(unsigned)group,
					(unsigned)source_pair_index,
					(unsigned)species_id,
					safe_species_name,
					(unsigned)slot_level,
					(unsigned)gender,
					(unsigned)move0,
					(unsigned)move1,
					(unsigned)move2,
					(unsigned)move3,
					(unsigned)min_steps,
					(unsigned)chance)) {
			return false;
		}
	}

	if (!ww_json_append(&cursor, &remaining, "],\"items\":["))
		return false;

	for (item_index = 0; item_index < WW_OV112_ITEM_COUNT; item_index++) {
		u32 base = WW_OV112_ITEMS_OFFSET + item_index * WW_OV112_ITEM_SIZE;
		u16 item_id = ww_read_u16_le(record + base + 0x00);
		u16 min_steps = ww_read_u16_le(record + base + 0x02);
		u16 chance_raw = ww_read_u16_le(record + base + 0x04);
		u8 chance = chance_raw > 0xFFu ? 0xFFu : (u8)chance_raw;
		char safe_item_name[48];

		ww_sanitize_json_ascii(ww_lookup_item_name(item_id), safe_item_name, sizeof(safe_item_name));

		if (!ww_json_append(
					&cursor,
					&remaining,
					"%s{\"routeItemIndex\":%u,\"itemId\":%u,\"itemName\":\"%s\",\"minSteps\":%u,\"chance\":%u}",
					item_index == 0 ? "" : ",",
					(unsigned)item_index,
					(unsigned)item_id,
					safe_item_name,
					(unsigned)min_steps,
					(unsigned)chance)) {
			return false;
		}
	}

	if (!ww_json_append(&cursor, &remaining, "]}}"))
		return false;

	return true;
}

static void ww_apply_dynamic_pokemon_sprite_patches(
		const char *nds_path,
		const hgss_stroll_send_context *send_context,
		const char *send_json,
		u32 *out_applied,
		u32 *out_failed)
{
	const char *small_narc_path = "a/2/4/8";
	const char *large_narc_path = "a/2/5/6";
	u8 *small_narc_data = NULL;
	u8 *large_narc_data = NULL;
	u8 area_sprite[WW_OV112_ROUTE_IMAGE_SIZE];
	u32 small_narc_size = 0;
	u32 large_narc_size = 0;
	u32 applied = 0;
	u32 failed = 0;
	u32 slot;
	u32 route_image_index;
	u16 join_species = 0;
	u32 join_best_chance = 0;
	bool join_species_found = false;
	bool join_has_chance = false;
	bool route_image_index_found = false;

	if (!nds_path || !nds_path[0] || !send_context || !send_json) {
		if (out_applied)
			*out_applied = 0;
		if (out_failed)
			*out_failed = 0;
		return;
	}

	route_image_index_found = ww_json_get_u32_from(send_json, "selectedRouteImageIndex", &route_image_index)
			|| ww_json_get_u32_from(send_json, "routeImageIndex", &route_image_index);

	if (route_image_index_found
			&& ww_extract_route_area_sprite_from_nds(nds_path, route_image_index, area_sprite)) {
		ww_patch_sprite_block_counted("areaSprite", area_sprite, sizeof(area_sprite), &applied, &failed);
	} else {
		failed++;
	}

	ww_load_narc_from_nds(nds_path, small_narc_path, &small_narc_data, &small_narc_size);
	ww_load_narc_from_nds(nds_path, large_narc_path, &large_narc_data, &large_narc_size);

	if (!small_narc_data && !large_narc_data) {
		if (out_applied)
			*out_applied = applied;
		if (out_failed)
			*out_failed = failed + 12;
		return;
	}

	ww_patch_species_image_sprites(
			small_narc_data,
			small_narc_size,
			large_narc_data,
			large_narc_size,
			send_context->source_slot.species_id,
			"walkPokeSmall0",
			"walkPokeSmall1",
			"walkPokeLarge0",
			"walkPokeLarge1",
			&applied,
			&failed);

	for (slot = 0; slot < 3; slot++) {
		u16 route_species;
		u32 slot_chance;
		char small0_key[24];
		char small1_key[24];

		if (!ww_json_get_configured_route_species_id(send_json, slot, &route_species)) {
			failed += 2;
			continue;
		}

		snprintf(small0_key, sizeof(small0_key), "routePoke%luSmall0", (unsigned long)slot);
		snprintf(small1_key, sizeof(small1_key), "routePoke%luSmall1", (unsigned long)slot);

		if (!join_species_found) {
			join_species = route_species;
			join_species_found = true;
		}

		if (ww_json_get_configured_route_slot_u32(send_json, slot, "chance", &slot_chance)) {
			if (!join_has_chance || slot_chance > join_best_chance) {
				join_species = route_species;
				join_best_chance = slot_chance;
				join_has_chance = true;
			}
		}

		ww_patch_species_image_sprites(
				small_narc_data,
				small_narc_size,
				NULL,
				0,
				route_species,
				small0_key,
				small1_key,
				NULL,
				NULL,
				&applied,
				&failed);
	}

	if (join_species_found) {
		ww_patch_species_image_sprites(
				NULL,
				0,
				large_narc_data,
				large_narc_size,
				join_species,
				NULL,
				NULL,
				"joinPokeLarge0",
				"joinPokeLarge1",
				&applied,
				&failed);
	}

	if (out_applied)
		*out_applied = applied;
	if (out_failed)
		*out_failed = failed;

	if (small_narc_data)
		free(small_narc_data);
	if (large_narc_data)
		free(large_narc_data);
}

static bool ww_patch_text_sprite(const char *key, u8 width, u32 size, const char *text, bool centered)
{
	u8 sprite[0x180];
	const char *value = (text && text[0]) ? text : "?";

	if (size > sizeof(sprite))
		return false;

	memset(sprite, 0, sizeof(sprite));
	string_to_img(sprite, width, value, centered);
	return ww_api_stroll_patch_sprite_block(key, sprite, size, NULL, 0);
}

static bool ww_patch_text_sprite_80x16(const char *key, const char *text)
{
	return ww_patch_text_sprite(key, 80, 0x140, text, true);
}

static bool ww_patch_text_sprite_96x16(const char *key, const char *text)
{
	return ww_patch_text_sprite(key, 96, 0x180, text, false);
}

static void ww_apply_dynamic_name_sprite_patches(
		const hgss_stroll_send_context *send_context,
		const char *send_json,
		u32 course_id,
		u32 *out_applied,
		u32 *out_failed)
{
	char area_name[32];
	char route_name[32];
	char item_name[48];
	u32 slot;
	u32 applied = 0;
	u32 failed = 0;

	if (!send_context || !send_json) {
		if (out_applied)
			*out_applied = 0;
		if (out_failed)
			*out_failed = 0;
		return;
	}

	if (ww_patch_text_sprite_80x16("walkPokeName", send_context->nickname))
		applied++;
	else
		failed++;

	if (ww_patch_text_sprite_80x16("trainerCardName", send_context->trainer_name))
		applied++;
	else
		failed++;

	if (!ww_json_get_string_from(send_json, "selectedCourseName", area_name, sizeof(area_name)))
		snprintf(area_name, sizeof(area_name), "COURSE %lu", (unsigned long)course_id);

	if (ww_patch_text_sprite_80x16("areaNameSprite", area_name))
		applied++;
	else
		failed++;

	for (slot = 0; slot < 3; slot++) {
		const char *patch_key = slot == 0 ? "routePoke0Name" : (slot == 1 ? "routePoke1Name" : "routePoke2Name");

		if (!ww_json_get_configured_route_species_name(send_json, slot, route_name, sizeof(route_name)))
			snprintf(route_name, sizeof(route_name), "POKE %lu", (unsigned long)(slot + 1));

		if (ww_patch_text_sprite_80x16(patch_key, route_name))
			applied++;
		else
			failed++;
	}

	for (slot = 0; slot < 10; slot++) {
		char patch_key[24];
		u32 item_id;

		snprintf(patch_key, sizeof(patch_key), "routeItem%luName", (unsigned long)slot);

		if (!ww_json_get_configured_route_item_name(send_json, slot, item_name, sizeof(item_name))) {
			if (ww_json_get_configured_route_item_id(send_json, slot, &item_id))
				snprintf(item_name, sizeof(item_name), "ITEM %lu", (unsigned long)item_id);
			else
				snprintf(item_name, sizeof(item_name), "ITEM %lu", (unsigned long)(slot + 1));
		}

		if (ww_patch_text_sprite_96x16(patch_key, item_name))
			applied++;
		else
			failed++;
	}

	if (out_applied)
		*out_applied = applied;
	if (out_failed)
		*out_failed = failed;
}

