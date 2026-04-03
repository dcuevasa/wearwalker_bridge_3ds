#include "hgss/hgss_patcher.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HGSS_SAVE_SIZE 0x80000
#define HGSS_PARTITION_SIZE 0x40000
#define HGSS_GENERAL_SIZE 0xF628
#define HGSS_BLOCK_FOOTER_SIZE 0x10
#define HGSS_BLOCK_COUNTER_REL 0x14

#define HGSS_WALKER_INFO_OFFSET 0xE704
#define HGSS_WALKER_TRIP_COUNTER_OFFSET (HGSS_WALKER_INFO_OFFSET - 0x4)
#define HGSS_WALKER_STEPS_OFFSET HGSS_WALKER_INFO_OFFSET
#define HGSS_WALKER_WATTS_OFFSET (HGSS_WALKER_INFO_OFFSET + 0x4)
#define HGSS_WALKER_COURSE_FLAGS_OFFSET (HGSS_WALKER_INFO_OFFSET + 0x8)

#define HGSS_FOOTER_FIRST (HGSS_GENERAL_SIZE - HGSS_BLOCK_COUNTER_REL)
#define HGSS_FOOTER_SECOND (HGSS_FOOTER_FIRST + HGSS_PARTITION_SIZE)

#define FOOTER_FIRST 0
#define FOOTER_SECOND 1
#define FOOTER_SAME 2

static void hgss_set_error(char *out_error, size_t out_error_size, const char *message)
{
	if (!out_error || out_error_size == 0)
		return;

	snprintf(out_error, out_error_size, "%s", message ? message : "unknown error");
}

static void hgss_commit_sdmc_archive(void)
{
	FS_Archive sdmc_archive;

	if (R_SUCCEEDED(FSUSER_OpenArchive(&sdmc_archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
		FSUSER_ControlArchive(sdmc_archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
		FSUSER_CloseArchive(sdmc_archive);
	}
}

static void hgss_force_sync(FILE *f)
{
	int fd;

	if (!f)
		return;

	fd = fileno(f);
	if (fd >= 0)
		fsync(fd);

	hgss_commit_sdmc_archive();
}

static u16 hgss_read_u16_le(const u8 *data, u32 offset)
{
	return (u16)(data[offset] | (data[offset + 1] << 8));
}

static u32 hgss_read_u32_le(const u8 *data, u32 offset)
{
	return (u32)(data[offset] |
			(data[offset + 1] << 8) |
			(data[offset + 2] << 16) |
			(data[offset + 3] << 24));
}

static void hgss_write_u16_le(u8 *data, u32 offset, u16 value)
{
	data[offset] = (u8)(value & 0xFF);
	data[offset + 1] = (u8)((value >> 8) & 0xFF);
}

static void hgss_write_u32_le(u8 *data, u32 offset, u32 value)
{
	data[offset] = (u8)(value & 0xFF);
	data[offset + 1] = (u8)((value >> 8) & 0xFF);
	data[offset + 2] = (u8)((value >> 16) & 0xFF);
	data[offset + 3] = (u8)((value >> 24) & 0xFF);
}

static u16 hgss_crc16_ccitt(const u8 *data, size_t length)
{
	u8 top = 0xFF;
	u8 bot = 0xFF;
	size_t i;

	for (i = 0; i < length; i++) {
		u8 x = data[i] ^ top;
		x ^= (u8)(x >> 4);
		top = (u8)((bot ^ ((x >> 3) & 0x1F) ^ ((x << 4) & 0xFF)) & 0xFF);
		bot = (u8)((x ^ ((x << 5) & 0xFF)) & 0xFF);
	}

	return (u16)(((u16)top << 8) | bot);
}

static int hgss_compare_counters(u32 counter1, u32 counter2)
{
	if (counter1 == 0xFFFFFFFF && counter2 != 0xFFFFFFFE)
		return FOOTER_SECOND;
	if (counter2 == 0xFFFFFFFF && counter1 != 0xFFFFFFFE)
		return FOOTER_FIRST;
	if (counter1 > counter2)
		return FOOTER_FIRST;
	if (counter1 < counter2)
		return FOOTER_SECOND;
	return FOOTER_SAME;
}

static int hgss_compare_footers(const u8 *data, u32 offset1, u32 offset2)
{
	u32 major1 = hgss_read_u32_le(data, offset1);
	u32 major2 = hgss_read_u32_le(data, offset2);
	int major = hgss_compare_counters(major1, major2);
	u32 minor1;
	u32 minor2;
	int minor;

	if (major != FOOTER_SAME)
		return major;

	minor1 = hgss_read_u32_le(data, offset1 + 4);
	minor2 = hgss_read_u32_le(data, offset2 + 4);
	minor = hgss_compare_counters(minor1, minor2);
	return minor == FOOTER_SECOND ? FOOTER_SECOND : FOOTER_FIRST;
}

static u32 hgss_active_general_offset(const u8 *data)
{
	int active_index = hgss_compare_footers(data, HGSS_FOOTER_FIRST, HGSS_FOOTER_SECOND);

	return active_index == FOOTER_FIRST ? 0 : HGSS_PARTITION_SIZE;
}

static void hgss_patch_general_checksum(u8 *data, u32 general_offset)
{
	u32 checksum_offset = general_offset + HGSS_GENERAL_SIZE - 2;
	u16 checksum = hgss_crc16_ccitt(data + general_offset, HGSS_GENERAL_SIZE - HGSS_BLOCK_FOOTER_SIZE);

	hgss_write_u16_le(data, checksum_offset, checksum);
}

bool hgss_patch_file(
		const char *save_path,
		u32 steps,
		u32 watts,
		u32 course_flags,
		bool increment_trip_counter,
		hgss_patch_report *out_report,
		char *out_error,
		size_t out_error_size)
{
	FILE *f = NULL;
	u8 *payload = NULL;
	u32 general_offset;
	u32 steps_offset;
	u32 watts_offset;
	u32 flags_offset;
	u32 trip_offset;
	hgss_patch_report report;
	size_t read_size;
	bool ok = false;

	if (!save_path || !save_path[0]) {
		hgss_set_error(out_error, out_error_size, "save path is empty");
		return false;
	}

	memset(&report, 0, sizeof(report));

	f = fopen(save_path, "rb+");
	if (!f) {
		hgss_set_error(out_error, out_error_size, "unable to open save file");
		goto cleanup;
	}

	payload = (u8 *)malloc(HGSS_SAVE_SIZE);
	if (!payload) {
		hgss_set_error(out_error, out_error_size, "out of memory");
		goto cleanup;
	}

	read_size = fread(payload, 1, HGSS_SAVE_SIZE, f);
	if (read_size != HGSS_SAVE_SIZE) {
		hgss_set_error(out_error, out_error_size, "invalid HGSS save size (expected 0x80000 bytes)");
		goto cleanup;
	}

	general_offset = hgss_active_general_offset(payload);
	steps_offset = general_offset + HGSS_WALKER_STEPS_OFFSET;
	watts_offset = general_offset + HGSS_WALKER_WATTS_OFFSET;
	flags_offset = general_offset + HGSS_WALKER_COURSE_FLAGS_OFFSET;
	trip_offset = general_offset + HGSS_WALKER_TRIP_COUNTER_OFFSET;

	report.general_offset = general_offset;
	report.steps_before = hgss_read_u32_le(payload, steps_offset);
	report.watts_before = hgss_read_u32_le(payload, watts_offset);
	report.course_flags_before = hgss_read_u32_le(payload, flags_offset);
	report.trip_counter_before = hgss_read_u16_le(payload, trip_offset);

	hgss_write_u32_le(payload, steps_offset, steps);
	hgss_write_u32_le(payload, watts_offset, watts);
	hgss_write_u32_le(payload, flags_offset, course_flags);

	report.steps_after = steps;
	report.watts_after = watts;
	report.course_flags_after = course_flags;
	report.trip_counter_after = report.trip_counter_before;
	report.trip_counter_incremented = false;

	if (increment_trip_counter) {
		report.trip_counter_after = (u16)(report.trip_counter_before + 1);
		report.trip_counter_incremented = true;
		hgss_write_u16_le(payload, trip_offset, report.trip_counter_after);
	}

	hgss_patch_general_checksum(payload, general_offset);

	if (fseek(f, 0, SEEK_SET) != 0) {
		hgss_set_error(out_error, out_error_size, "failed to seek save file for write");
		goto cleanup;
	}

	if (fwrite(payload, 1, HGSS_SAVE_SIZE, f) != HGSS_SAVE_SIZE) {
		hgss_set_error(out_error, out_error_size, "failed to write patched save");
		goto cleanup;
	}

	if (fflush(f) != 0) {
		hgss_set_error(out_error, out_error_size, "failed to flush patched save");
		goto cleanup;
	}
	hgss_force_sync(f);

	if (out_report)
		*out_report = report;

	ok = true;

cleanup:
	if (payload)
		free(payload);
	if (f)
		fclose(f);

	return ok;
}
