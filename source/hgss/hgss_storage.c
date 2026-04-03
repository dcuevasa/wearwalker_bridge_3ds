#include "hgss/hgss_storage.h"
#include "hgss/hgss_species_abilities.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define HGSS_SAVE_SIZE 0x80000
#define HGSS_PARTITION_SIZE 0x40000

#define HGSS_GENERAL_SIZE 0xF628
#define HGSS_GENERAL_GAP 0xD8
#define HGSS_STORAGE_SIZE 0x12310
#define HGSS_STORAGE_START (HGSS_GENERAL_SIZE + HGSS_GENERAL_GAP)

#define HGSS_BLOCK_FOOTER_SIZE 0x10
#define HGSS_BLOCK_COUNTER_REL 0x14

#define HGSS_WALKER_INFO_OFFSET 0xE704
#define HGSS_WALKER_PAIR_OFFSET 0xE5E0
#define HGSS_WALKER_PAIR_SIZE HGSS_BOX_SLOT_SIZE
#define HGSS_WALKER_TRIP_COUNTER_OFFSET (HGSS_WALKER_INFO_OFFSET - 0x4)
#define HGSS_WALKER_STEPS_OFFSET HGSS_WALKER_INFO_OFFSET
#define HGSS_WALKER_WATTS_OFFSET (HGSS_WALKER_INFO_OFFSET + 0x4)
#define HGSS_WALKER_COURSE_FLAGS_OFFSET (HGSS_WALKER_INFO_OFFSET + 0x8)

#define HGSS_TRAINER_INFO_OFFSET 0x64
#define HGSS_TRAINER_NAME_SIZE 16
#define HGSS_TRAINER_ID32_REL 0x10
#define HGSS_TRAINER_GENDER_REL 0x18
#define HGSS_TRAINER_LANGUAGE_REL 0x19

#define HGSS_BOX_SLOT_SIZE 136
#define HGSS_BOX_STRIDE 0x1000

#define PK4_OFFSET_PID 0x00
#define PK4_OFFSET_SANITY 0x04
#define PK4_OFFSET_CHECKSUM 0x06
#define PK4_OFFSET_SPECIES 0x08
#define PK4_OFFSET_HELD_ITEM 0x0A
#define PK4_OFFSET_ID32 0x0C
#define PK4_OFFSET_EXP 0x10
#define PK4_OFFSET_FRIENDSHIP 0x14
#define PK4_OFFSET_ABILITY 0x15
#define PK4_OFFSET_LANGUAGE 0x17
#define PK4_OFFSET_MOVE1 0x28
#define PK4_OFFSET_PP1 0x30
#define PK4_OFFSET_PP_UPS 0x34
#define PK4_OFFSET_IV32 0x38
#define PK4_OFFSET_FLAGS 0x40
#define PK4_OFFSET_EGG_LOCATION_EXT 0x44
#define PK4_OFFSET_MET_LOCATION_EXT 0x46
#define PK4_OFFSET_NICKNAME 0x48
#define PK4_OFFSET_VERSION 0x5F
#define PK4_OFFSET_OT_NAME 0x68
#define PK4_OFFSET_EGG_DATE 0x78
#define PK4_OFFSET_MET_DATE 0x7B
#define PK4_OFFSET_EGG_LOCATION_DP 0x7E
#define PK4_OFFSET_MET_LOCATION_DP 0x80
#define PK4_OFFSET_POKERUS 0x82
#define PK4_OFFSET_BALL_DPPT 0x83
#define PK4_OFFSET_MET_LEVEL_OTG 0x84
#define PK4_OFFSET_BALL_HGSS 0x86
#define PK4_OFFSET_WALKING_MOOD 0x87
#define PK4_OFFSET_UNUSED_RIBBON_BITS 0x64

#define HGSS_PK4_NICKNAME_CHARS 11
#define HGSS_TRAINER_NAME_CHARS 8

#define LOCATION_POKEWALKER_4 233
#define BALL_POKE 4

#define FOOTER_FIRST 0
#define FOOTER_SECOND 1
#define FOOTER_SAME 2

static const u8 g_block_position[] = {
	0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
	0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
	2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1,
	1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2,
	2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0,
	2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0,
	0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
	0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
};

static const u8 g_block_position_invert[] = {
	0, 1, 2, 4,
	3, 5, 6, 7,
	12, 18, 13, 19,
	8, 10, 14, 20,
	16, 22, 9, 11,
	15, 21, 17, 23,
	0, 1, 2, 4,
	3, 5, 6, 7,
};

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
	u32 footer1 = HGSS_GENERAL_SIZE - HGSS_BLOCK_COUNTER_REL;
	u32 footer2 = footer1 + HGSS_PARTITION_SIZE;
	int active_index = hgss_compare_footers(data, footer1, footer2);

	return active_index == FOOTER_FIRST ? 0 : HGSS_PARTITION_SIZE;
}

static u32 hgss_active_storage_offset(const u8 *data)
{
	u32 footer1 = HGSS_STORAGE_START + HGSS_STORAGE_SIZE - HGSS_BLOCK_COUNTER_REL;
	u32 footer2 = footer1 + HGSS_PARTITION_SIZE;
	int active_index = hgss_compare_footers(data, footer1, footer2);

	return HGSS_STORAGE_START + (active_index == FOOTER_FIRST ? 0 : HGSS_PARTITION_SIZE);
}

static u32 hgss_backup_general_offset(u32 active_general_offset)
{
	return active_general_offset == 0 ? HGSS_PARTITION_SIZE : 0;
}

static u32 hgss_backup_storage_offset(u32 active_storage_offset)
{
	return active_storage_offset == HGSS_STORAGE_START ?
			(HGSS_STORAGE_START + HGSS_PARTITION_SIZE) : HGSS_STORAGE_START;
}

static u32 hgss_storage_slot_offset(u32 storage_offset, u8 box_index, u8 slot_index)
{
	return storage_offset + (u32)box_index * HGSS_BOX_STRIDE + (u32)slot_index * HGSS_BOX_SLOT_SIZE;
}

static u16 hgss_pk4_add16_checksum(const u8 *data)
{
	u16 checksum = 0;
	u32 offset;

	for (offset = 8; offset + 1 < HGSS_BOX_SLOT_SIZE; offset += 2)
		checksum = (u16)((checksum + hgss_read_u16_le(data, offset)) & 0xFFFF);

	return checksum;
}

static u32 hgss_lcrng_next(u32 seed)
{
	return (u32)(0x41C64E6Du * seed + 0x00006073u);
}

static u8 hgss_species_gender_ratio(u16 species_id)
{
	switch (species_id) {
		case 29: /* Nidoran-F */
		case 30: /* Nidorina */
		case 31: /* Nidoqueen */
		case 113: /* Chansey */
		case 115: /* Kangaskhan */
		case 124: /* Jynx */
		case 238: /* Smoochum */
		case 241: /* Miltank */
		case 242: /* Blissey */
		case 314: /* Illumise */
		case 380: /* Latias */
		case 478: /* Froslass */
		case 488: /* Cresselia */
			return 0xFE; /* Female only */

		case 32: /* Nidoran-M */
		case 33: /* Nidorino */
		case 34: /* Nidoking */
		case 106: /* Hitmonlee */
		case 107: /* Hitmonchan */
		case 128: /* Tauros */
		case 236: /* Tyrogue */
		case 237: /* Hitmontop */
		case 313: /* Volbeat */
		case 381: /* Latios */
		case 475: /* Gallade */
			return 0x00; /* Male only */

		case 81:  /* Magnemite */
		case 82:  /* Magneton */
		case 100: /* Voltorb */
		case 101: /* Electrode */
		case 120: /* Staryu */
		case 121: /* Starmie */
		case 132: /* Ditto */
		case 137: /* Porygon */
		case 144: /* Articuno */
		case 145: /* Zapdos */
		case 146: /* Moltres */
		case 150: /* Mewtwo */
		case 151: /* Mew */
		case 201: /* Unown */
		case 233: /* Porygon2 */
		case 243: /* Raikou */
		case 244: /* Entei */
		case 245: /* Suicune */
		case 249: /* Lugia */
		case 250: /* Ho-Oh */
		case 251: /* Celebi */
		case 292: /* Shedinja */
		case 337: /* Lunatone */
		case 338: /* Solrock */
		case 343: /* Baltoy */
		case 344: /* Claydol */
		case 374: /* Beldum */
		case 375: /* Metang */
		case 376: /* Metagross */
		case 377: /* Regirock */
		case 378: /* Regice */
		case 379: /* Registeel */
		case 382: /* Kyogre */
		case 383: /* Groudon */
		case 384: /* Rayquaza */
		case 385: /* Jirachi */
		case 386: /* Deoxys */
		case 436: /* Bronzor */
		case 437: /* Bronzong */
		case 462: /* Magnezone */
		case 474: /* Porygon-Z */
		case 479: /* Rotom */
		case 480: /* Uxie */
		case 481: /* Mesprit */
		case 482: /* Azelf */
		case 483: /* Dialga */
		case 484: /* Palkia */
		case 486: /* Regigigas */
		case 487: /* Giratina */
		case 491: /* Darkrai */
		case 492: /* Shaymin */
		case 493: /* Arceus */
			return 0xFF; /* Genderless */

		default:
			return 0x7F; /* 50/50 fallback */
	}
}

static u8 hgss_gender_from_pid_and_ratio(u32 pid, u8 ratio)
{
	if (ratio == 0xFF)
		return 2;
	if (ratio == 0xFE)
		return 1;
	if (ratio == 0x00)
		return 0;
	return (pid & 0xFF) < ratio ? 1 : 0;
}

static u32 hgss_pokewalker_pid(u32 trainer_id32, u8 nature, u8 desired_gender, u8 gender_ratio)
{
	u16 tid16 = (u16)(trainer_id32 & 0xFFFF);
	u16 sid16 = (u16)((trainer_id32 >> 16) & 0xFFFF);
	u32 local_nature = (u32)(nature % 24);
	u32 pid;
	u8 pid_gender;

	/* Mirrors PKHeX PokewalkerRNG.GetPID. */
	pid = (u32)(((((u32)(tid16 ^ sid16) >> 8) ^ 0xFFu) << 24) & 0xFFFFFFFFu);
	pid = (u32)((pid + local_nature - (pid % 25u)) & 0xFFFFFFFFu);

	if (gender_ratio == 0x00 || gender_ratio >= 0xFE)
		return pid;

	pid_gender = ((pid & 0xFFu) < gender_ratio) ? 1 : 0;
	if (desired_gender == pid_gender)
		return pid;

	if (desired_gender == 0) {
		pid = (u32)((pid + ((((gender_ratio - (pid & 0xFFu)) / 25u) + 1u) * 25u)) & 0xFFFFFFFFu);
		if ((local_nature & 1u) != (pid & 1u))
			pid = (u32)((pid + 25u) & 0xFFFFFFFFu);
	} else {
		pid = (u32)((pid - (((((pid & 0xFFu) - gender_ratio) / 25u) + 1u) * 25u)) & 0xFFFFFFFFu);
		if ((local_nature & 1u) != (pid & 1u))
			pid = (u32)((pid - 25u) & 0xFFFFFFFFu);
	}

	return pid;
}

static u8 hgss_select_capture_ability(u16 species_id, u32 pid)
{
	u8 ability1 = 0;
	u8 ability2 = 0;

	if (species_id < HGSS_SPECIES_ABILITY_COUNT) {
		ability1 = g_hgss_species_abilities[species_id][0];
		ability2 = g_hgss_species_abilities[species_id][1];
	}

	if (ability1 == 0 && ability2 == 0)
		return 1;
	if (ability1 == 0)
		ability1 = ability2;
	if (ability2 == 0)
		ability2 = ability1;

	if (ability2 != ability1 && (pid & 1u) != 0)
		return ability2;

	return ability1;
}

static u16 hgss_g4_char_to_value(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (u16)(0x0121 + (u16)(ch - '0'));
	if (ch >= 'A' && ch <= 'Z')
		return (u16)(0x012B + (u16)(ch - 'A'));
	if (ch >= 'a' && ch <= 'z')
		return (u16)(0x0145 + (u16)(ch - 'a'));
	if (ch == ' ')
		return 0x01CE;

	switch (ch) {
		case '!':
			return 0x00E1;
		case '?':
			return 0x00E2;
		case ',':
			return 0x00F9;
		case '.':
			return 0x00F8;
		case '-':
			return 0x00F1;
		case '\'':
			return 0x01B3;
		default:
			return 0x01AC;
	}
}

static void hgss_encode_g4_string(const char *value, u8 *out_encoded, u32 max_chars)
{
	u32 index;

	if (!out_encoded || max_chars == 0)
		return;

	memset(out_encoded, 0, max_chars * 2);

	for (index = 0; index < max_chars; index++) {
		char ch = '\0';

		if (value)
			ch = value[index];
		if (ch == '\0') {
			hgss_write_u16_le(out_encoded, index * 2, 0xFFFF);
			return;
		}

		hgss_write_u16_le(out_encoded, index * 2, hgss_g4_char_to_value(ch));
	}
}

static char hgss_g4_value_to_char(u16 value)
{
	if (value >= 0x0121 && value <= 0x012A)
		return (char)('0' + (value - 0x0121));
	if (value >= 0x012B && value <= 0x0144)
		return (char)('A' + (value - 0x012B));
	if (value >= 0x0145 && value <= 0x015E)
		return (char)('a' + (value - 0x0145));
	if (value == 0x01CE)
		return ' ';

	switch (value) {
		case 0x00E1:
			return '!';
		case 0x00E2:
			return '?';
		case 0x00F9:
			return ',';
		case 0x00F8:
			return '.';
		case 0x00F1:
			return '-';
		case 0x01B3:
			return '\'';
		case 0x01AC:
			return '?';
		default:
			return '?';
	}
}

static void hgss_decode_g4_string(const u8 *encoded, u32 max_chars, char *out_text, u32 out_size)
{
	u32 index;
	u32 written = 0;

	if (!out_text || out_size == 0)
		return;

	out_text[0] = '\0';
	if (!encoded || max_chars == 0)
		return;

	for (index = 0; index < max_chars && written + 1 < out_size; index++) {
		u16 value = hgss_read_u16_le(encoded, index * 2);

		if (value == 0x0000 || value == 0xFFFF)
			break;

		out_text[written++] = hgss_g4_value_to_char(value);
	}

	out_text[written] = '\0';
}

static bool hgss_pk4_is_encrypted(const u8 *stored)
{
	return hgss_read_u32_le(stored, PK4_OFFSET_UNUSED_RIBBON_BITS) != 0;
}

static void hgss_pk4_crypt_array(u8 *data, size_t length, u32 seed)
{
	u32 state = seed;
	size_t offset;

	for (offset = 0; offset + 1 < length; offset += 2) {
		u16 xor_word;
		u16 value;

		state = hgss_lcrng_next(state);
		xor_word = (u16)((state >> 16) & 0xFFFF);
		value = (u16)(hgss_read_u16_le(data, (u32)offset) ^ xor_word);
		hgss_write_u16_le(data, (u32)offset, value);
	}
}

static void hgss_pk4_shuffle_blocks(u8 *data, u8 sv)
{
	u8 perm[4] = {0, 1, 2, 3};
	u8 slot_of[4] = {0, 1, 2, 3};
	const u8 *table;
	u32 i;

	if (!data)
		return;
	if (sv == 0)
		return;
	if (sv >= 32)
		sv %= 32;

	table = &g_block_position[(u32)sv * 4];
	for (i = 0; i < 3; i++) {
		u8 desired = table[i];
		u8 j = slot_of[desired];
		u8 block_at_i;
		u8 temp[32];

		if (j == i)
			continue;

		memcpy(temp, data + i * 32, sizeof(temp));
		memcpy(data + i * 32, data + (u32)j * 32, sizeof(temp));
		memcpy(data + (u32)j * 32, temp, sizeof(temp));

		block_at_i = perm[i];
		perm[j] = block_at_i;
		slot_of[block_at_i] = j;
	}
}

static void hgss_pk4_decrypt_stored(const u8 *stored, u8 *decrypted)
{
	u32 pid;
	u16 checksum;
	u8 sv;

	memcpy(decrypted, stored, HGSS_BOX_SLOT_SIZE);
	if (!hgss_pk4_is_encrypted(decrypted))
		return;

	pid = hgss_read_u32_le(decrypted, PK4_OFFSET_PID);
	checksum = hgss_read_u16_le(decrypted, PK4_OFFSET_CHECKSUM);
	sv = (u8)((pid >> 13) & 31);

	hgss_pk4_crypt_array(decrypted + 8, HGSS_BOX_SLOT_SIZE - 8, checksum);
	hgss_pk4_shuffle_blocks(decrypted + 8, sv);
}

static void hgss_pk4_encrypt_stored(const u8 *decrypted, u8 *stored)
{
	u32 pid;
	u16 checksum;
	u8 sv;

	memcpy(stored, decrypted, HGSS_BOX_SLOT_SIZE);
	hgss_write_u16_le(stored, PK4_OFFSET_SANITY, 0);
	checksum = hgss_pk4_add16_checksum(stored);
	hgss_write_u16_le(stored, PK4_OFFSET_CHECKSUM, checksum);

	pid = hgss_read_u32_le(stored, PK4_OFFSET_PID);
	sv = (u8)((pid >> 13) & 31);
	sv = g_block_position_invert[sv];

	hgss_pk4_shuffle_blocks(stored + 8, sv);
	hgss_pk4_crypt_array(stored + 8, HGSS_BOX_SLOT_SIZE - 8, checksum);
}

static bool hgss_find_first_empty_slot(const u8 *payload, u32 storage_offset, u8 box_index, u8 exclude_slot, u8 *out_slot)
{
	u8 slot;

	for (slot = 0; slot < HGSS_BOX_SLOTS; slot++) {
		u32 offset;
		u8 raw[HGSS_BOX_SLOT_SIZE];
		u8 dec[HGSS_BOX_SLOT_SIZE];

		if (slot == exclude_slot)
			continue;

		offset = hgss_storage_slot_offset(storage_offset, box_index, slot);
		memcpy(raw, payload + offset, sizeof(raw));
		hgss_pk4_decrypt_stored(raw, dec);
		if (hgss_read_u16_le(dec, PK4_OFFSET_SPECIES) == 0) {
			if (out_slot)
				*out_slot = slot;
			return true;
		}
	}

	return false;
}

static bool hgss_find_first_empty_slot_any_box(
		const u8 *payload,
		u32 storage_offset,
		u8 exclude_box,
		u8 exclude_slot,
		u8 *out_box,
		u8 *out_slot)
{
	u8 box;

	for (box = 0; box < HGSS_BOX_COUNT; box++) {
		u8 local_slot = 0;
		u8 local_exclude = box == exclude_box ? exclude_slot : 0xFFu;

		if (hgss_find_first_empty_slot(payload, storage_offset, box, local_exclude, &local_slot)) {
			if (out_box)
				*out_box = box;
			if (out_slot)
				*out_slot = local_slot;
			return true;
		}
	}

	return false;
}

static void hgss_patch_general_checksum(u8 *payload, u32 general_offset)
{
	u32 checksum_offset = general_offset + HGSS_GENERAL_SIZE - 2;
	u16 checksum = hgss_crc16_ccitt(payload + general_offset, HGSS_GENERAL_SIZE - HGSS_BLOCK_FOOTER_SIZE);

	hgss_write_u16_le(payload, checksum_offset, checksum);
}

static void hgss_patch_storage_checksum(u8 *payload, u32 storage_offset)
{
	u32 checksum_offset = storage_offset + HGSS_STORAGE_SIZE - 2;
	u16 checksum = hgss_crc16_ccitt(payload + storage_offset, HGSS_STORAGE_SIZE - HGSS_BLOCK_FOOTER_SIZE);

	hgss_write_u16_le(payload, checksum_offset, checksum);
}

static void hgss_build_capture_pk4(
		u8 *out_decrypted,
		u16 species_id,
		u8 level,
		const u16 capture_moves[4],
		const char *capture_species_name,
		u8 version,
		u32 trainer_id32,
		u8 trainer_ot_gender,
		u8 trainer_language,
		const u8 *trainer_ot_name)
{
	u32 seed;
	u32 pid;
	u32 exp;
	u8 nature;
	u8 gender_ratio;
	u8 desired_gender;
	u8 final_gender;
	u8 flags;
	u16 moves[4] = {0, 0, 0, 0};
	bool has_hint_moves = false;
	u8 move_index;
	const char *species_name;
	time_t now;
	struct tm *tm_now;

	memset(out_decrypted, 0, HGSS_BOX_SLOT_SIZE);

	seed = 0xC0DEC0DEu ^ trainer_id32 ^ ((u32)species_id << 16) ^ ((u32)level << 8);
	seed = hgss_lcrng_next(seed);
	nature = (u8)((seed >> 8) % 24u);
	gender_ratio = hgss_species_gender_ratio(species_id);
	if (gender_ratio == 0xFF)
		desired_gender = 2;
	else if (gender_ratio == 0xFE)
		desired_gender = 1;
	else
		desired_gender = 0;

	pid = hgss_pokewalker_pid(trainer_id32, nature, desired_gender, gender_ratio);
	final_gender = hgss_gender_from_pid_and_ratio(pid, gender_ratio);

	hgss_write_u32_le(out_decrypted, PK4_OFFSET_PID, pid);
	hgss_write_u16_le(out_decrypted, PK4_OFFSET_SANITY, 0);
	hgss_write_u16_le(out_decrypted, PK4_OFFSET_CHECKSUM, 0);
	hgss_write_u16_le(out_decrypted, PK4_OFFSET_SPECIES, species_id);
	hgss_write_u32_le(out_decrypted, PK4_OFFSET_ID32, trainer_id32);

	exp = (u32)level * (u32)level * (u32)level;
	hgss_write_u32_le(out_decrypted, PK4_OFFSET_EXP, exp);

	out_decrypted[PK4_OFFSET_FRIENDSHIP] = 70;
	out_decrypted[PK4_OFFSET_ABILITY] = hgss_select_capture_ability(species_id, pid);
	out_decrypted[PK4_OFFSET_LANGUAGE] = trainer_language;
	hgss_write_u32_le(out_decrypted, PK4_OFFSET_IV32, 0);
	flags = (u8)(out_decrypted[PK4_OFFSET_FLAGS] & 0x01);
	flags = (u8)(flags | ((final_gender & 0x03) << 1));
	out_decrypted[PK4_OFFSET_FLAGS] = flags;

	hgss_write_u16_le(out_decrypted, PK4_OFFSET_EGG_LOCATION_EXT, 0);
	hgss_write_u16_le(out_decrypted, PK4_OFFSET_MET_LOCATION_EXT, LOCATION_POKEWALKER_4);
	species_name = (capture_species_name && capture_species_name[0]) ? capture_species_name : "PIDGEY";
	hgss_encode_g4_string(species_name, out_decrypted + PK4_OFFSET_NICKNAME, HGSS_PK4_NICKNAME_CHARS);
	out_decrypted[PK4_OFFSET_VERSION] = version;
	if (trainer_ot_name)
		memcpy(out_decrypted + PK4_OFFSET_OT_NAME, trainer_ot_name, HGSS_TRAINER_NAME_SIZE);

	now = time(NULL);
	tm_now = localtime(&now);
	if (tm_now && tm_now->tm_year >= 100 && tm_now->tm_year <= 199) {
		out_decrypted[PK4_OFFSET_MET_DATE + 0] = (u8)(tm_now->tm_year - 100);
		out_decrypted[PK4_OFFSET_MET_DATE + 1] = (u8)(tm_now->tm_mon + 1);
		out_decrypted[PK4_OFFSET_MET_DATE + 2] = (u8)tm_now->tm_mday;
	}

	hgss_write_u16_le(out_decrypted, PK4_OFFSET_EGG_LOCATION_DP, 0);
	hgss_write_u16_le(out_decrypted, PK4_OFFSET_MET_LOCATION_DP, LOCATION_POKEWALKER_4);

	out_decrypted[PK4_OFFSET_POKERUS] = 0;
	out_decrypted[PK4_OFFSET_BALL_DPPT] = BALL_POKE;
	out_decrypted[PK4_OFFSET_MET_LEVEL_OTG] = (u8)((level & 0x7F) | ((trainer_ot_gender & 0x1) << 7));
	out_decrypted[PK4_OFFSET_BALL_HGSS] = BALL_POKE;
	out_decrypted[PK4_OFFSET_WALKING_MOOD] = 0;

	for (move_index = 0; move_index < 4; move_index++) {
		u16 move = 0;

		if (capture_moves)
			move = capture_moves[move_index];
		moves[move_index] = move;
		if (move != 0)
			has_hint_moves = true;
	}
	if (!has_hint_moves)
		moves[0] = 33;

	for (move_index = 0; move_index < 4; move_index++) {
		hgss_write_u16_le(out_decrypted, PK4_OFFSET_MOVE1 + (u32)move_index * 2, moves[move_index]);
		out_decrypted[PK4_OFFSET_PP1 + move_index] = moves[move_index] != 0 ? 5 : 0;
	}
	memset(out_decrypted + PK4_OFFSET_PP_UPS, 0, 4);
}

bool hgss_read_stroll_send_context(
		const char *save_path,
		u8 box_number,
		u8 slot_number,
		hgss_stroll_send_context *out_context,
		char *out_error,
		size_t out_error_size)
{
	FILE *f = NULL;
	u8 *payload = NULL;
	u32 general_offset;
	u32 storage_offset;
	u32 slot_offset;
	u8 raw[HGSS_BOX_SLOT_SIZE];
	u8 dec[HGSS_BOX_SLOT_SIZE];
	u8 trainer_raw[HGSS_TRAINER_NAME_SIZE];
	hgss_stroll_send_context context;
	size_t read_size;
	bool ok = false;
	u32 trainer_id32;
	u32 source_id32;
	u32 pid;
	u16 source_tid;
	u16 source_sid;
	u16 shiny_xor;
	u8 source_gender;
	u8 move_index;

	if (!save_path || !save_path[0]) {
		hgss_set_error(out_error, out_error_size, "save path is empty");
		return false;
	}
	if (!out_context) {
		hgss_set_error(out_error, out_error_size, "output context is required");
		return false;
	}
	if (box_number == 0 || box_number > HGSS_BOX_COUNT) {
		hgss_set_error(out_error, out_error_size, "box number out of range (1..18)");
		return false;
	}
	if (slot_number == 0 || slot_number > HGSS_BOX_SLOTS) {
		hgss_set_error(out_error, out_error_size, "slot number out of range (1..30)");
		return false;
	}

	memset(&context, 0, sizeof(context));

	f = fopen(save_path, "rb");
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
	storage_offset = hgss_active_storage_offset(payload);
	slot_offset = hgss_storage_slot_offset(storage_offset, (u8)(box_number - 1), (u8)(slot_number - 1));
	memcpy(raw, payload + slot_offset, sizeof(raw));
	hgss_pk4_decrypt_stored(raw, dec);

	context.source_slot.species_id = hgss_read_u16_le(dec, PK4_OFFSET_SPECIES);
	context.source_slot.occupied = context.source_slot.species_id != 0;
	context.source_slot.exp = hgss_read_u32_le(dec, PK4_OFFSET_EXP);
	context.source_slot.friendship = dec[PK4_OFFSET_FRIENDSHIP];
	context.source_slot.version = dec[PK4_OFFSET_VERSION];

	context.held_item = hgss_read_u16_le(dec, PK4_OFFSET_HELD_ITEM);
	for (move_index = 0; move_index < 4; move_index++)
		context.moves[move_index] = hgss_read_u16_le(dec, PK4_OFFSET_MOVE1 + (u32)move_index * 2);

	hgss_decode_g4_string(dec + PK4_OFFSET_NICKNAME, HGSS_PK4_NICKNAME_CHARS, context.nickname, sizeof(context.nickname));

	source_gender = (u8)((dec[PK4_OFFSET_FLAGS] >> 1) & 0x03);
	if (source_gender == 1)
		context.variant_flags |= 0x20;

	source_id32 = hgss_read_u32_le(dec, PK4_OFFSET_ID32);
	pid = hgss_read_u32_le(dec, PK4_OFFSET_PID);
	source_tid = (u16)(source_id32 & 0xFFFFu);
	source_sid = (u16)((source_id32 >> 16) & 0xFFFFu);
	shiny_xor = (u16)(((pid & 0xFFFFu) ^ ((pid >> 16) & 0xFFFFu) ^ source_tid ^ source_sid) & 0xFFFFu);
	if (shiny_xor < 8)
		context.special_flags |= 0x02;

	trainer_id32 = hgss_read_u32_le(payload, general_offset + HGSS_TRAINER_INFO_OFFSET + HGSS_TRAINER_ID32_REL);
	context.trainer_tid = (u16)(trainer_id32 & 0xFFFFu);
	context.trainer_sid = (u16)((trainer_id32 >> 16) & 0xFFFFu);
	memcpy(
			trainer_raw,
			payload + general_offset + HGSS_TRAINER_INFO_OFFSET,
			sizeof(trainer_raw));
	hgss_decode_g4_string(trainer_raw, HGSS_TRAINER_NAME_CHARS, context.trainer_name, sizeof(context.trainer_name));
	if (!context.trainer_name[0])
		snprintf(context.trainer_name, sizeof(context.trainer_name), "WWBRIDGE");

	context.pokewalker_steps = hgss_read_u32_le(payload, general_offset + HGSS_WALKER_STEPS_OFFSET);
	context.pokewalker_watts = hgss_read_u32_le(payload, general_offset + HGSS_WALKER_WATTS_OFFSET);
	context.pokewalker_course_flags = hgss_read_u32_le(payload, general_offset + HGSS_WALKER_COURSE_FLAGS_OFFSET);

	*out_context = context;
	ok = true;

cleanup:
	if (payload)
		free(payload);
	if (f)
		fclose(f);

	return ok;
}

bool hgss_read_box_slot_summary(
		const char *save_path,
		u8 box_number,
		u8 slot_number,
		hgss_box_slot_summary *out_summary,
		char *out_error,
		size_t out_error_size)
{
	hgss_stroll_send_context context;

	if (!out_summary) {
		hgss_set_error(out_error, out_error_size, "output summary is required");
		return false;
	}

	if (!hgss_read_stroll_send_context(
				save_path,
				box_number,
				slot_number,
				&context,
				out_error,
				out_error_size)) {
		return false;
	}

	*out_summary = context.source_slot;
	return true;
}

bool hgss_apply_stroll_send(
		const char *save_path,
		u8 box_number,
		u8 source_slot_number,
		u16 expected_source_species,
		bool clear_source_slot,
		hgss_stroll_send_report *out_report,
		char *out_error,
		size_t out_error_size)
{
	FILE *f = NULL;
	u8 *payload = NULL;
	u32 general_offset;
	u32 general_backup_offset;
	u32 storage_offset;
	u32 storage_backup_offset;
	u32 source_offset;
	u32 source_backup_offset;
	u32 pair_offset;
	u32 pair_backup_offset;
	u8 source_raw[HGSS_BOX_SLOT_SIZE];
	u8 source_dec[HGSS_BOX_SLOT_SIZE];
	hgss_stroll_send_report report;
	u16 source_species;
	size_t read_size;
	bool ok = false;

	if (!save_path || !save_path[0]) {
		hgss_set_error(out_error, out_error_size, "save path is empty");
		return false;
	}
	if (box_number == 0 || box_number > HGSS_BOX_COUNT) {
		hgss_set_error(out_error, out_error_size, "box number out of range (1..18)");
		return false;
	}
	if (source_slot_number == 0 || source_slot_number > HGSS_BOX_SLOTS) {
		hgss_set_error(out_error, out_error_size, "source slot out of range (1..30)");
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
	general_backup_offset = hgss_backup_general_offset(general_offset);
	storage_offset = hgss_active_storage_offset(payload);
	storage_backup_offset = hgss_backup_storage_offset(storage_offset);
	pair_offset = general_offset + HGSS_WALKER_PAIR_OFFSET;
	pair_backup_offset = general_backup_offset + HGSS_WALKER_PAIR_OFFSET;

	source_offset = hgss_storage_slot_offset(storage_offset, (u8)(box_number - 1), (u8)(source_slot_number - 1));
	source_backup_offset = hgss_storage_slot_offset(storage_backup_offset, (u8)(box_number - 1), (u8)(source_slot_number - 1));
	memcpy(source_raw, payload + source_offset, sizeof(source_raw));
	hgss_pk4_decrypt_stored(source_raw, source_dec);

	source_species = hgss_read_u16_le(source_dec, PK4_OFFSET_SPECIES);
	if (source_species == 0) {
		hgss_set_error(out_error, out_error_size, "source slot is empty");
		goto cleanup;
	}
	if (expected_source_species != 0 && source_species != expected_source_species) {
		hgss_set_error(out_error, out_error_size, "source slot species does not match expected species");
		goto cleanup;
	}

	report.source_species_before = source_species;
	report.source_exp_before = hgss_read_u32_le(source_dec, PK4_OFFSET_EXP);
	report.source_friendship_before = source_dec[PK4_OFFSET_FRIENDSHIP];

	/* Store the sent Pokemon in both active and backup walker pair areas. */
	memcpy(payload + pair_offset, source_raw, HGSS_WALKER_PAIR_SIZE);
	memcpy(payload + pair_backup_offset, source_raw, HGSS_WALKER_PAIR_SIZE);
	report.walker_pair_written = true;

	if (clear_source_slot) {
		memset(payload + source_offset, 0, HGSS_BOX_SLOT_SIZE);
		memset(payload + source_backup_offset, 0, HGSS_BOX_SLOT_SIZE);
		report.source_slot_cleared = true;
	}

	hgss_patch_general_checksum(payload, general_offset);
	hgss_patch_general_checksum(payload, general_backup_offset);
	hgss_patch_storage_checksum(payload, storage_offset);
	hgss_patch_storage_checksum(payload, storage_backup_offset);

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

bool hgss_apply_stroll_return(
		const char *save_path,
		u8 box_number,
		u8 source_slot_number,
		u8 target_box_number,
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
		size_t out_error_size)
{
	FILE *f = NULL;
	u8 *payload = NULL;
	u32 general_offset;
	u32 general_backup_offset;
	u32 storage_offset;
	u32 storage_backup_offset;
	u32 source_offset;
	u32 source_backup_offset;
	u32 pair_offset;
	u32 pair_backup_offset;
	u8 source_raw[HGSS_BOX_SLOT_SIZE];
	u8 source_dec[HGSS_BOX_SLOT_SIZE];
	u8 source_enc[HGSS_BOX_SLOT_SIZE];
	u8 pair_raw[HGSS_BOX_SLOT_SIZE];
	u8 pair_dec[HGSS_BOX_SLOT_SIZE];
	hgss_stroll_return_report report;
	u16 source_species;
	u32 source_exp_before;
	u32 source_exp_after;
	u8 source_friend_before;
	u8 source_friend_after;
	u32 trip_offset;
	size_t read_size;
	bool ok = false;
	bool write_capture = false;
	bool source_restored_from_pair = false;
	u8 target_slot_index = 0;

	if (!save_path || !save_path[0]) {
		hgss_set_error(out_error, out_error_size, "save path is empty");
		return false;
	}
	if (box_number == 0 || box_number > HGSS_BOX_COUNT) {
		hgss_set_error(out_error, out_error_size, "box number out of range (1..18)");
		return false;
	}
	if (source_slot_number == 0 || source_slot_number > HGSS_BOX_SLOTS) {
		hgss_set_error(out_error, out_error_size, "source slot out of range (1..30)");
		return false;
	}
	if (target_box_number > HGSS_BOX_COUNT) {
		hgss_set_error(out_error, out_error_size, "target box out of range (0..18)");
		return false;
	}
	if (target_slot_number > HGSS_BOX_SLOTS) {
		hgss_set_error(out_error, out_error_size, "target slot out of range (0..30)");
		return false;
	}
	if (target_slot_number != 0 && target_box_number == 0) {
		hgss_set_error(out_error, out_error_size, "target box must be set when target slot is explicit");
		return false;
	}

	memset(&report, 0, sizeof(report));
	report.target_box = 0;
	report.target_slot = 0;
	report.trip_counter_incremented = increment_trip_counter;

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
	general_backup_offset = hgss_backup_general_offset(general_offset);
	storage_offset = hgss_active_storage_offset(payload);
	storage_backup_offset = hgss_backup_storage_offset(storage_offset);
	pair_offset = general_offset + HGSS_WALKER_PAIR_OFFSET;
	pair_backup_offset = general_backup_offset + HGSS_WALKER_PAIR_OFFSET;

	source_offset = hgss_storage_slot_offset(storage_offset, (u8)(box_number - 1), (u8)(source_slot_number - 1));
	source_backup_offset = hgss_storage_slot_offset(storage_backup_offset, (u8)(box_number - 1), (u8)(source_slot_number - 1));
	memcpy(source_raw, payload + source_offset, sizeof(source_raw));
	hgss_pk4_decrypt_stored(source_raw, source_dec);

	source_species = hgss_read_u16_le(source_dec, PK4_OFFSET_SPECIES);
	if (source_species == 0) {
		memcpy(pair_raw, payload + pair_offset, sizeof(pair_raw));
		hgss_pk4_decrypt_stored(pair_raw, pair_dec);
		source_species = hgss_read_u16_le(pair_dec, PK4_OFFSET_SPECIES);
		if (source_species == 0) {
			hgss_set_error(out_error, out_error_size, "source slot is empty and walker pair has no pending Pokemon");
			goto cleanup;
		}

		memcpy(source_raw, pair_raw, sizeof(source_raw));
		memcpy(source_dec, pair_dec, sizeof(source_dec));
		source_restored_from_pair = true;
	}
	if (expected_source_species != 0 && source_species != expected_source_species) {
		hgss_set_error(out_error, out_error_size, "source slot species does not match expected species");
		goto cleanup;
	}

	source_exp_before = hgss_read_u32_le(source_dec, PK4_OFFSET_EXP);
	source_friend_before = source_dec[PK4_OFFSET_FRIENDSHIP];
	source_exp_after = source_exp_before + exp_gain;
	if (source_exp_after < source_exp_before)
		source_exp_after = 0xFFFFFFFFu;

	if (walked_steps >= (u32)(0xFF - source_friend_before))
		source_friend_after = 0xFF;
	else
		source_friend_after = (u8)(source_friend_before + walked_steps);

	hgss_write_u32_le(source_dec, PK4_OFFSET_EXP, source_exp_after);
	source_dec[PK4_OFFSET_FRIENDSHIP] = source_friend_after;
	hgss_pk4_encrypt_stored(source_dec, source_enc);
	memcpy(payload + source_offset, source_enc, sizeof(source_enc));
	memcpy(payload + source_backup_offset, source_enc, sizeof(source_enc));
	if (source_restored_from_pair) {
		memset(payload + pair_offset, 0, HGSS_WALKER_PAIR_SIZE);
		memset(payload + pair_backup_offset, 0, HGSS_WALKER_PAIR_SIZE);
	}

	report.source_species_before = source_species;
	report.source_exp_before = source_exp_before;
	report.source_exp_after = source_exp_after;
	report.source_friendship_before = source_friend_before;
	report.source_friendship_after = source_friend_after;
	report.source_restored_from_pair = source_restored_from_pair;

	if (capture_species != 0) {
		u32 target_offset;
		u32 target_backup_offset;
		u8 target_raw[HGSS_BOX_SLOT_SIZE];
		u8 target_dec[HGSS_BOX_SLOT_SIZE];
		u8 target_enc[HGSS_BOX_SLOT_SIZE];
		u8 final_capture_level = capture_level;
		u8 final_version = source_dec[PK4_OFFSET_VERSION];
		u8 trainer_name_raw[HGSS_TRAINER_NAME_SIZE];
		u32 trainer_id32;
		u8 trainer_ot_gender;
		u8 trainer_language;
		bool skip_capture = false;
		u8 target_box_index = 0;

		if (final_capture_level == 0)
			final_capture_level = 10;
		if (final_capture_level > 100)
			final_capture_level = 100;

		if (target_slot_number == 0) {
			if (target_box_number == 0) {
				if (!hgss_find_first_empty_slot_any_box(
							payload,
							storage_offset,
							(u8)(box_number - 1),
							(u8)(source_slot_number - 1),
							&target_box_index,
							&target_slot_index)) {
					report.capture_skipped_no_space = true;
					skip_capture = true;
				}
			} else {
				u8 exclude_slot = (target_box_number == box_number) ? (u8)(source_slot_number - 1) : 0xFFu;

				target_box_index = (u8)(target_box_number - 1);
				if (!hgss_find_first_empty_slot(
							payload,
							storage_offset,
							target_box_index,
							exclude_slot,
							&target_slot_index)) {
					report.capture_skipped_no_space = true;
					skip_capture = true;
				}
			}
		} else {
			target_box_index = (u8)(target_box_number - 1);
			target_slot_index = (u8)(target_slot_number - 1);
			if (target_box_number == box_number && target_slot_index == (u8)(source_slot_number - 1)) {
				hgss_set_error(out_error, out_error_size, "target slot cannot be the same as source slot");
				goto cleanup;
			}
		}

		if (skip_capture)
			goto skip_capture_write;

		target_offset = hgss_storage_slot_offset(storage_offset, target_box_index, target_slot_index);
		target_backup_offset = hgss_storage_slot_offset(storage_backup_offset, target_box_index, target_slot_index);
		memcpy(target_raw, payload + target_offset, sizeof(target_raw));
		hgss_pk4_decrypt_stored(target_raw, target_dec);
		if (hgss_read_u16_le(target_dec, PK4_OFFSET_SPECIES) != 0) {
			hgss_set_error(out_error, out_error_size, "target slot is not empty");
			goto cleanup;
		}

		trainer_id32 = hgss_read_u32_le(payload, general_offset + HGSS_TRAINER_INFO_OFFSET + HGSS_TRAINER_ID32_REL);
		trainer_ot_gender = payload[general_offset + HGSS_TRAINER_INFO_OFFSET + HGSS_TRAINER_GENDER_REL];
		trainer_language = payload[general_offset + HGSS_TRAINER_INFO_OFFSET + HGSS_TRAINER_LANGUAGE_REL];
		memcpy(
				trainer_name_raw,
				payload + general_offset + HGSS_TRAINER_INFO_OFFSET,
				sizeof(trainer_name_raw));

		if (final_version == 0)
			final_version = 0x0B;

		hgss_build_capture_pk4(
				target_dec,
				capture_species,
				final_capture_level,
				capture_moves,
				capture_species_name,
				final_version,
				trainer_id32,
				trainer_ot_gender,
				trainer_language,
				trainer_name_raw);
		hgss_pk4_encrypt_stored(target_dec, target_enc);
		memcpy(payload + target_offset, target_enc, sizeof(target_enc));
		memcpy(payload + target_backup_offset, target_enc, sizeof(target_enc));

		report.target_box = (s32)target_box_index + 1;
		report.target_slot = (s32)target_slot_index + 1;
		report.capture_species = capture_species;
		report.capture_level = final_capture_level;
		write_capture = true;

skip_capture_write:
		;
	}

	hgss_write_u32_le(payload, general_offset + HGSS_WALKER_STEPS_OFFSET, pokewalker_steps);
	hgss_write_u32_le(payload, general_offset + HGSS_WALKER_WATTS_OFFSET, pokewalker_watts);
	hgss_write_u32_le(payload, general_offset + HGSS_WALKER_COURSE_FLAGS_OFFSET, pokewalker_course_flags);
	hgss_write_u32_le(payload, general_backup_offset + HGSS_WALKER_STEPS_OFFSET, pokewalker_steps);
	hgss_write_u32_le(payload, general_backup_offset + HGSS_WALKER_WATTS_OFFSET, pokewalker_watts);
	hgss_write_u32_le(payload, general_backup_offset + HGSS_WALKER_COURSE_FLAGS_OFFSET, pokewalker_course_flags);

	if (increment_trip_counter) {
		u16 trip_counter;
		u16 trip_counter_backup;
		trip_offset = general_offset + HGSS_WALKER_TRIP_COUNTER_OFFSET;
		trip_counter = hgss_read_u16_le(payload, trip_offset);
		hgss_write_u16_le(payload, trip_offset, (u16)(trip_counter + 1));
		trip_offset = general_backup_offset + HGSS_WALKER_TRIP_COUNTER_OFFSET;
		trip_counter_backup = hgss_read_u16_le(payload, trip_offset);
		hgss_write_u16_le(payload, trip_offset, (u16)(trip_counter_backup + 1));
	}

	hgss_patch_general_checksum(payload, general_offset);
	hgss_patch_general_checksum(payload, general_backup_offset);
	hgss_patch_storage_checksum(payload, storage_offset);
	hgss_patch_storage_checksum(payload, storage_backup_offset);

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

	report.pokewalker_steps_after = pokewalker_steps;
	report.pokewalker_watts_after = pokewalker_watts;
	report.pokewalker_course_flags_after = pokewalker_course_flags;
	if (!write_capture) {
		report.capture_species = 0;
		report.capture_level = 0;
		report.target_box = 0;
		report.target_slot = 0;
	}

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
