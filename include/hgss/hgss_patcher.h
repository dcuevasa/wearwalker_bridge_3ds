#pragma once

#include <3ds/types.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	u32 general_offset;
	u32 steps_before;
	u32 steps_after;
	u32 watts_before;
	u32 watts_after;
	u32 course_flags_before;
	u32 course_flags_after;
	u16 trip_counter_before;
	u16 trip_counter_after;
	bool trip_counter_incremented;
} hgss_patch_report;

bool hgss_patch_file(
		const char *save_path,
		u32 steps,
		u32 watts,
		u32 course_flags,
		bool increment_trip_counter,
		hgss_patch_report *out_report,
		char *out_error,
		size_t out_error_size);
