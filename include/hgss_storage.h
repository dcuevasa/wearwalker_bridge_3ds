#pragma once

#include <3ds/types.h>
#include <stdbool.h>
#include <stddef.h>

#define HGSS_BOX_COUNT 18
#define HGSS_BOX_SLOTS 30

typedef struct {
	bool occupied;
	u16 species_id;
	u32 exp;
	u8 friendship;
	u8 version;
} hgss_box_slot_summary;

typedef struct {
	hgss_box_slot_summary source_slot;
	u16 held_item;
	u16 moves[4];
	u8 variant_flags;
	u8 special_flags;
	char nickname[24];
	char trainer_name[16];
	u16 trainer_tid;
	u16 trainer_sid;
	u32 pokewalker_steps;
	u32 pokewalker_watts;
	u32 pokewalker_course_flags;
} hgss_stroll_send_context;

typedef struct {
	u16 source_species_before;
	u32 source_exp_before;
	u8 source_friendship_before;
	bool source_slot_cleared;
	bool walker_pair_written;
} hgss_stroll_send_report;

typedef struct {
	u16 source_species_before;
	u32 source_exp_before;
	u32 source_exp_after;
	u8 source_friendship_before;
	u8 source_friendship_after;
	s32 target_slot;
	u16 capture_species;
	u8 capture_level;
	bool capture_skipped_no_space;
	u32 pokewalker_steps_after;
	u32 pokewalker_watts_after;
	u32 pokewalker_course_flags_after;
	bool source_restored_from_pair;
	bool trip_counter_incremented;
} hgss_stroll_return_report;

bool hgss_read_box_slot_summary(
		const char *save_path,
		u8 box_number,
		u8 slot_number,
		hgss_box_slot_summary *out_summary,
		char *out_error,
		size_t out_error_size);

bool hgss_read_stroll_send_context(
		const char *save_path,
		u8 box_number,
		u8 slot_number,
		hgss_stroll_send_context *out_context,
		char *out_error,
		size_t out_error_size);

bool hgss_apply_stroll_send(
		const char *save_path,
		u8 box_number,
		u8 source_slot_number,
		u16 expected_source_species,
		bool clear_source_slot,
		hgss_stroll_send_report *out_report,
		char *out_error,
		size_t out_error_size);

bool hgss_apply_stroll_return(
		const char *save_path,
		u8 box_number,
		u8 source_slot_number,
		u8 target_slot_number,
		u16 expected_source_species,
		u32 exp_gain,
		u32 walked_steps,
		u16 capture_species,
		u8 capture_level,
		const u16 capture_moves[4],
		const char *capture_species_name,
		u32 pokewalker_steps,
		u32 pokewalker_watts,
		u32 pokewalker_course_flags,
		bool increment_trip_counter,
		hgss_stroll_return_report *out_report,
		char *out_error,
		size_t out_error_size);
