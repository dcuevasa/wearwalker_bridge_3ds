#!/usr/bin/env python3
"""Shared EEPROM helpers for WiFi protocol testing and local manipulation.

This module intentionally mirrors key offsets and read/write logic from the
WearWalker Kotlin code so local Python tools and the 3DS client can be tested
against the same behavior.
"""

from __future__ import annotations

import random
import re
import time
from functools import lru_cache
from pathlib import Path
from typing import Any, Dict

EEPROM_SIZE = 0x10000
SIGNATURE_OFFSET = 0x0000
SIGNATURE = b"nintendo"

IDENTITY_OFFSET = 0x00ED
IDENTITY_OWNER_NAME_OFFSET = IDENTITY_OFFSET + 72
IDENTITY_LAST_SYNC_OFFSET = IDENTITY_OFFSET + 96
IDENTITY_STEP_COUNT_OFFSET = IDENTITY_OFFSET + 100

HEALTH_OFFSET = 0x0156
HEALTH_LIFETIME_STEPS_OFFSET = HEALTH_OFFSET
HEALTH_TODAY_STEPS_OFFSET = HEALTH_OFFSET + 4
HEALTH_LAST_SYNC_OFFSET = HEALTH_OFFSET + 8
HEALTH_CUR_WATTS_OFFSET = HEALTH_OFFSET + 14

GENERAL_WATTS_OFFSET = 0x0164
GENERAL_LAST_SYNC_OFFSET = 0x015E
SESSION_WATTS_OFFSET = 0xCE8A

IDENTITY_PROTOCOL_VERSION_OFFSET = IDENTITY_OFFSET + 92
IDENTITY_PROTOCOL_SUB_VERSION_OFFSET = IDENTITY_OFFSET + 94
IDENTITY_TRAINER_TID_OFFSET = IDENTITY_OFFSET + 12
IDENTITY_TRAINER_SID_OFFSET = IDENTITY_OFFSET + 14

ROUTE_INFO_OFFSET = 0x8F00
ROUTE_INFO_SIZE = 0x00BD
ROUTE_WALKING_SPECIES_OFFSET = ROUTE_INFO_OFFSET
ROUTE_IMAGE_INDEX_OFFSET = ROUTE_INFO_OFFSET + 39

INVENTORY_CAUGHT_OFFSET = 0xCE8C
INVENTORY_CAUGHT_SLOTS = 3
INVENTORY_CAUGHT_ENTRY_SIZE = 16

INVENTORY_DOWSED_OFFSET = 0xCEBC
INVENTORY_DOWSED_SLOTS = 3
INVENTORY_ITEM_ENTRY_SIZE = 4

INVENTORY_GIFTED_OFFSET = 0xCEC8
INVENTORY_GIFTED_SLOTS = 10

STEP_HISTORY_OFFSET = 0xCEF0
STEP_HISTORY_DAYS = 7

EVENT_LOG_OFFSET = 0xCF0C
EVENT_LOG_ENTRY_SIZE = 136
EVENT_LOG_ENTRIES = 24

EVENT_LOG_WATTS_OFFSET = 78
EVENT_LOG_REMOTE_WATTS_OFFSET = 80
EVENT_LOG_STEP_COUNT_OFFSET = 82
EVENT_LOG_REMOTE_STEP_COUNT_OFFSET = 86
EVENT_LOG_TYPE_OFFSET = 90
EVENT_LOG_WALKING_SPECIES_OFFSET = 10
EVENT_LOG_CAUGHT_SPECIES_OFFSET = 12
EVENT_LOG_EXTRA_DATA_OFFSET = 14
EVENT_LOG_ROUTE_IMAGE_OFFSET = 76
EVENT_LOG_FRIENDSHIP_OFFSET = 77
EVENT_LOG_GENDER_FORM_OFFSET = 91
EVENT_LOG_CAUGHT_GENDER_FORM_OFFSET = 92

ROUTE_NICKNAME_OFFSET = ROUTE_INFO_OFFSET + 16
ROUTE_NICKNAME_CHARS = 11
ROUTE_FRIENDSHIP_OFFSET = ROUTE_INFO_OFFSET + 38
ROUTE_NAME_OFFSET = ROUTE_INFO_OFFSET + 40
ROUTE_NAME_CHARS = 21
ROUTE_POKES_OFFSET = ROUTE_INFO_OFFSET + 82
ROUTE_POKE_SLOTS = 3
ROUTE_POKE_MIN_STEPS_OFFSET = ROUTE_INFO_OFFSET + 130
ROUTE_POKE_CHANCE_OFFSET = ROUTE_INFO_OFFSET + 136

TEAM_OFFSET = 0xCC00
TEAM_POKES_OFFSET = TEAM_OFFSET + 96
TEAM_POKE_ENTRY_SIZE = 56
TEAM_POKE_COUNT = 6
TEAM_POKE_SPECIES_OFFSET = 0
TEAM_POKE_LEVEL_OFFSET = 34
TEAM_POKE_NICKNAME_OFFSET = 36
TEAM_POKE_NICKNAME_CHARS = 10

POKE_SUMMARY_SIZE = 16
POKE_SUMMARY_LEVEL_OFFSET = 12
POKE_SUMMARY_VARIANT_FLAGS_OFFSET = 13
POKE_SUMMARY_SPECIAL_FLAGS_OFFSET = 14

EVENT_TYPE_STROLL_DEPART = 1
EVENT_TYPE_STROLL_RETURN = 2
EVENT_TYPE_STROLL_CAPTURE = 3

DEFAULT_ROUTE_SLOT_MIN_STEPS = (0, 200, 500)
DEFAULT_ROUTE_SLOT_CHANCE = (50, 35, 15)

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
MONOREPO_ROOT = PROJECT_ROOT.parent
PKHEX_PERSONAL_HGSS_PATH = (
    MONOREPO_ROOT / "pkhex" / "PKHeX.Core" / "Resources" / "byte" / "personal" / "personal_hgss"
)
PKHEX_WALKER_ENCOUNTER_PATH = (
    MONOREPO_ROOT
    / "pkhex"
    / "PKHeX.Core"
    / "Resources"
    / "legality"
    / "wild"
    / "Gen4"
    / "encounter_walker4.pkl"
)
SPECIES_TS_PATH = (
    MONOREPO_ROOT
    / "pokewalker-eeprom-editor"
    / "src"
    / "pokewalker"
    / "types"
    / "species.ts"
)

COURSE_NAMES: tuple[str, ...] = (
    "Refreshing Field",
    "Noisy Forest",
    "Rugged Road",
    "Beautiful Beach",
    "Suburban Area",
    "Dim Cave",
    "Blue Lake",
    "Town Outskirts",
    "Hoenn Field",
    "Warm Beach",
    "Volcano Path",
    "Treehouse",
    "Scary Cave",
    "Sinnoh Field",
    "Icy Mountain Road",
    "Big Forest",
    "White Lake",
    "Stormy Beach",
    "Resort",
    "Quiet Cave",
    "Beyond The Sea",
    "Night Sky's Edge",
    "Yellow Forest",
    "Rally",
    "Sightseeing",
    "Winners Path",
    "Amity Meadow",
)

# Pokewalker route unlock model distilled from game behavior and public course tables.
# `watts` is the threshold to unlock by accumulated watts; `None` means special unlock.
# `requires_national_dex` follows the documented HGSS requirement for late routes.
# `special` is an informational marker for routes requiring event/trade conditions.
COURSE_UNLOCK_RULES: tuple[dict[str, Any], ...] = (
    {"watts": 0, "requires_national_dex": False, "special": None},
    {"watts": 0, "requires_national_dex": False, "special": None},
    {"watts": 50, "requires_national_dex": False, "special": None},
    {"watts": 200, "requires_national_dex": False, "special": None},
    {"watts": 500, "requires_national_dex": False, "special": None},
    {"watts": 1000, "requires_national_dex": False, "special": None},
    {"watts": 2000, "requires_national_dex": False, "special": None},
    {"watts": 3000, "requires_national_dex": False, "special": None},
    {"watts": 5000, "requires_national_dex": True, "special": None},
    {"watts": 7500, "requires_national_dex": True, "special": None},
    {"watts": 10000, "requires_national_dex": True, "special": None},
    {"watts": 15000, "requires_national_dex": True, "special": None},
    {"watts": 20000, "requires_national_dex": True, "special": None},
    {"watts": 25000, "requires_national_dex": True, "special": None},
    {"watts": 30000, "requires_national_dex": True, "special": None},
    {"watts": 40000, "requires_national_dex": True, "special": None},
    {"watts": 50000, "requires_national_dex": True, "special": None},
    {"watts": 65000, "requires_national_dex": True, "special": None},
    {"watts": 80000, "requires_national_dex": True, "special": None},
    {"watts": 100000, "requires_national_dex": True, "special": None},
    {"watts": None, "requires_national_dex": False, "special": "gts-trade"},
    {"watts": None, "requires_national_dex": False, "special": "jirachi-trade"},
    {"watts": None, "requires_national_dex": False, "special": "event"},
    {"watts": None, "requires_national_dex": False, "special": "event"},
    {"watts": None, "requires_national_dex": False, "special": "event"},
    {"watts": None, "requires_national_dex": False, "special": "event"},
    {"watts": None, "requires_national_dex": False, "special": "event"},
)
COURSE_UNLOCK_MASK = (1 << len(COURSE_NAMES)) - 1

# PKHeX source: PokewalkerRNG.CourseSpecies (6 entries per course, 3 groups of 2).
COURSE_SPECIES: tuple[int, ...] = (
    115,
    84,
    29,
    32,
    16,
    161,
    202,
    69,
    48,
    46,
    43,
    21,
    240,
    95,
    66,
    77,
    163,
    74,
    54,
    120,
    79,
    60,
    191,
    194,
    239,
    81,
    81,
    198,
    163,
    19,
    238,
    92,
    92,
    95,
    41,
    66,
    147,
    60,
    98,
    90,
    118,
    72,
    63,
    100,
    109,
    88,
    19,
    162,
    300,
    264,
    314,
    313,
    263,
    265,
    320,
    298,
    116,
    318,
    118,
    129,
    218,
    307,
    228,
    111,
    77,
    74,
    352,
    351,
    203,
    234,
    44,
    70,
    105,
    128,
    42,
    177,
    66,
    92,
    439,
    415,
    403,
    406,
    399,
    401,
    459,
    361,
    215,
    436,
    220,
    179,
    357,
    438,
    114,
    400,
    179,
    102,
    433,
    200,
    93,
    418,
    223,
    170,
    456,
    422,
    129,
    86,
    54,
    90,
    417,
    25,
    39,
    35,
    183,
    187,
    442,
    446,
    433,
    349,
    164,
    42,
    120,
    224,
    116,
    222,
    223,
    170,
    35,
    39,
    41,
    163,
    74,
    95,
    25,
    25,
    25,
    25,
    25,
    25,
    441,
    302,
    25,
    453,
    427,
    417,
    255,
    133,
    279,
    61,
    52,
    25,
    446,
    374,
    116,
    355,
    129,
    436,
    239,
    240,
    238,
    440,
    174,
    173,
)

NATURE_NAMES: tuple[str, ...] = (
    "Hardy",
    "Lonely",
    "Brave",
    "Adamant",
    "Naughty",
    "Bold",
    "Docile",
    "Relaxed",
    "Impish",
    "Lax",
    "Timid",
    "Hasty",
    "Serious",
    "Jolly",
    "Naive",
    "Modest",
    "Mild",
    "Quiet",
    "Bashful",
    "Rash",
    "Calm",
    "Gentle",
    "Sassy",
    "Careful",
    "Quirky",
)

# Indexes are stat positions within (atk, def, spa, spd, spe).
NATURE_EFFECTS: tuple[tuple[int, int], ...] = (
    (0, 0),
    (0, 1),
    (0, 4),
    (0, 2),
    (0, 3),
    (1, 0),
    (1, 1),
    (1, 4),
    (1, 2),
    (1, 3),
    (4, 0),
    (4, 1),
    (4, 4),
    (4, 2),
    (4, 3),
    (2, 0),
    (2, 1),
    (2, 4),
    (2, 2),
    (2, 3),
    (3, 0),
    (3, 1),
    (3, 4),
    (3, 2),
    (3, 3),
)

_SPECIES_NAMES_CACHE: list[str] | None = None
_PERSONAL_HGSS_CACHE: bytes | None = None
_WALKER_ENCOUNTERS_CACHE: dict[int, list[dict[str, Any]]] | None = None


MAX_TRAINER_CHARS = 8
DEFAULT_TRAINER = "WWBRIDGE"


def _read_device_text_fixed(eeprom: bytearray, offset: int, max_chars: int) -> str:
    chars: list[str] = []
    for index in range(max_chars):
        code_unit = read_u16_le(eeprom, offset + index * 2)
        decoded = decode_device_code_unit(code_unit)
        if decoded is None:
            break
        chars.append(decoded)
    return "".join(chars).strip()


def _write_device_text_fixed(eeprom: bytearray, offset: int, max_chars: int, value: str) -> None:
    clean = (value or "").strip()[:max_chars]
    for index in range(max_chars):
        code_unit = 0x0000
        if index < len(clean):
            code_unit = encode_device_char(clean[index])
        write_u16_le(eeprom, offset + index * 2, code_unit)


def _load_species_names() -> list[str]:
    global _SPECIES_NAMES_CACHE
    if _SPECIES_NAMES_CACHE is not None:
        return _SPECIES_NAMES_CACHE

    names: list[str] = []
    if SPECIES_TS_PATH.exists():
        pattern = re.compile(r'^\s*"([^"]+)"\s*,?\s*$')
        text = SPECIES_TS_PATH.read_text(encoding="utf-8", errors="ignore")
        for line in text.splitlines():
            match = pattern.match(line)
            if match:
                names.append(match.group(1))

    if not names:
        names = ["NULL"]

    _SPECIES_NAMES_CACHE = names
    return names


def _species_name(species_id: int) -> str:
    names = _load_species_names()
    if 0 <= species_id < len(names):
        return names[species_id]
    return f"SPECIES_{species_id}"


def _load_personal_hgss_table() -> bytes | None:
    global _PERSONAL_HGSS_CACHE
    if _PERSONAL_HGSS_CACHE is not None:
        return _PERSONAL_HGSS_CACHE

    if not PKHEX_PERSONAL_HGSS_PATH.exists():
        _PERSONAL_HGSS_CACHE = b""
        return None

    payload = PKHEX_PERSONAL_HGSS_PATH.read_bytes()
    if len(payload) % 0x2C != 0:
        _PERSONAL_HGSS_CACHE = b""
        return None

    _PERSONAL_HGSS_CACHE = payload
    return _PERSONAL_HGSS_CACHE


def _get_personal_entry(species_id: int) -> bytes | None:
    table = _load_personal_hgss_table()
    if not table:
        return None

    sid = int(species_id)
    start = sid * 0x2C
    end = start + 0x2C
    if sid < 0 or end > len(table):
        return None
    return table[start:end]


def _species_growth_rate(species_id: int) -> int:
    entry = _get_personal_entry(species_id)
    if entry is None:
        return 0
    return entry[0x13] & 0xFF


def _species_base_friendship(species_id: int) -> int:
    entry = _get_personal_entry(species_id)
    if entry is None:
        return 70
    return entry[0x12] & 0xFF


def _species_base_stats(species_id: int) -> dict[str, int]:
    entry = _get_personal_entry(species_id)
    if entry is None:
        return {
            "hp": 50,
            "atk": 50,
            "def": 50,
            "spa": 50,
            "spd": 50,
            "spe": 50,
        }
    return {
        "hp": entry[0],
        "atk": entry[1],
        "def": entry[2],
        "spe": entry[3],
        "spa": entry[4],
        "spd": entry[5],
    }


def _exp_for_level(level: int, growth: int) -> int:
    lvl = max(1, min(int(level), 100))
    cube = lvl * lvl * lvl

    # Growth mappings match PKHeX Experience.cs table indices 0..5.
    if growth == 0:  # Medium Fast
        return cube
    if growth == 1:  # Erratic
        if lvl <= 50:
            return (cube * (100 - lvl)) // 50
        if lvl <= 68:
            return (cube * (150 - lvl)) // 100
        if lvl <= 98:
            return (cube * ((1911 - 10 * lvl) // 3)) // 500
        return (cube * (160 - lvl)) // 100
    if growth == 2:  # Fluctuating
        if lvl <= 15:
            return (cube * (((lvl + 1) // 3) + 24)) // 50
        if lvl <= 36:
            return (cube * (lvl + 14)) // 50
        return (cube * ((lvl // 2) + 32)) // 50
    if growth == 3:  # Medium Slow
        value = (6 * cube) // 5 - 15 * lvl * lvl + 100 * lvl - 140
        return max(0, value)
    if growth == 4:  # Fast
        return (4 * cube) // 5
    if growth == 5:  # Slow
        return (5 * cube) // 4
    return cube


@lru_cache(maxsize=8)
def _exp_table_for_growth(growth: int) -> tuple[int, ...]:
    return tuple(_exp_for_level(level, growth) for level in range(1, 101))


def _exp_threshold(level: int, growth: int) -> int:
    lvl = max(1, min(int(level), 100))
    return _exp_table_for_growth(growth)[lvl - 1]


def _level_from_exp(exp: int, growth: int) -> int:
    amount = max(0, int(exp))
    table = _exp_table_for_growth(growth)
    if amount >= table[-1]:
        return 100

    level = 1
    while level < 100 and amount >= table[level]:
        level += 1
    return level


def _nature_modifier_percent(nature_id: int, stat_index: int) -> int:
    inc, dec = NATURE_EFFECTS[nature_id % len(NATURE_EFFECTS)]
    if inc == dec:
        return 100
    if stat_index == inc:
        return 110
    if stat_index == dec:
        return 90
    return 100


def _generate_pokemon_stats(species_id: int, level: int, rng: random.Random) -> dict[str, Any]:
    lvl = max(1, min(int(level), 100))
    base = _species_base_stats(species_id)
    nature_id = rng.randint(0, len(NATURE_NAMES) - 1)

    iv_hp = rng.randint(0, 31)
    iv_atk = rng.randint(0, 31)
    iv_def = rng.randint(0, 31)
    iv_spa = rng.randint(0, 31)
    iv_spd = rng.randint(0, 31)
    iv_spe = rng.randint(0, 31)

    hp = ((2 * base["hp"] + iv_hp) * lvl) // 100 + lvl + 10

    def _calc_non_hp(base_stat: int, iv: int, stat_index: int) -> int:
        raw = ((2 * base_stat + iv) * lvl) // 100 + 5
        return (raw * _nature_modifier_percent(nature_id, stat_index)) // 100

    atk = _calc_non_hp(base["atk"], iv_atk, 0)
    defense = _calc_non_hp(base["def"], iv_def, 1)
    spa = _calc_non_hp(base["spa"], iv_spa, 2)
    spd = _calc_non_hp(base["spd"], iv_spd, 3)
    spe = _calc_non_hp(base["spe"], iv_spe, 4)

    growth = _species_growth_rate(species_id)
    min_exp = _exp_threshold(lvl, growth)
    max_exp = _exp_threshold(min(100, lvl + 1), growth) - 1 if lvl < 100 else _exp_threshold(100, growth)

    return {
        "natureId": nature_id,
        "nature": NATURE_NAMES[nature_id],
        "growthRate": growth,
        "expRange": {
            "min": min_exp,
            "max": max_exp,
        },
        "ivs": {
            "hp": iv_hp,
            "atk": iv_atk,
            "def": iv_def,
            "spa": iv_spa,
            "spd": iv_spd,
            "spe": iv_spe,
        },
        "stats": {
            "hp": hp,
            "atk": atk,
            "def": defense,
            "spa": spa,
            "spd": spd,
            "spe": spe,
        },
    }


def _read_pokemon_summary(eeprom: bytearray, offset: int) -> dict[str, Any]:
    species_id = read_u16_le(eeprom, offset)
    held_item = read_u16_le(eeprom, offset + 2)
    moves = [read_u16_le(eeprom, offset + 4 + index * 2) for index in range(4)]
    level = eeprom[offset + POKE_SUMMARY_LEVEL_OFFSET]
    variant_flags = eeprom[offset + POKE_SUMMARY_VARIANT_FLAGS_OFFSET]
    special_flags = eeprom[offset + POKE_SUMMARY_SPECIAL_FLAGS_OFFSET]
    return {
        "speciesId": species_id,
        "speciesName": _species_name(species_id),
        "heldItem": held_item,
        "moves": moves,
        "level": level,
        "variantFlags": variant_flags,
        "specialFlags": special_flags,
        "isFemale": bool(variant_flags & 0x20),
        "isShiny": bool(special_flags & 0x02),
        "hasAltForm": bool(special_flags & 0x01),
    }


def _pokemon_summary_from_values(
    species_id: int,
    *,
    level: int,
    held_item: int = 0,
    moves: list[int] | None = None,
    variant_flags: int = 0,
    special_flags: int = 0,
) -> dict[str, Any]:
    move_values = [0, 0, 0, 0]
    if moves is not None:
        for index, value in enumerate(moves[:4]):
            move_values[index] = _validate_u16(f"moves[{index}]", value)

    return {
        "speciesId": _validate_u16("speciesId", species_id),
        "heldItem": _validate_u16("heldItem", held_item),
        "moves": move_values,
        "level": max(1, min(int(level), 100)),
        "variantFlags": _validate_u8("variantFlags", variant_flags),
        "specialFlags": _validate_u8("specialFlags", special_flags),
    }


def _write_pokemon_summary(eeprom: bytearray, offset: int, summary: dict[str, Any]) -> None:
    species_id = _validate_u16("speciesId", int(summary.get("speciesId", 0)))
    held_item = _validate_u16("heldItem", int(summary.get("heldItem", 0)))
    raw_moves = summary.get("moves", [])
    if not isinstance(raw_moves, list):
        raise ValueError("moves must be a list")

    moves = [0, 0, 0, 0]
    for index, value in enumerate(raw_moves[:4]):
        moves[index] = _validate_u16(f"moves[{index}]", int(value))

    level = max(0, min(int(summary.get("level", 0)), 100))
    variant_flags = _validate_u8("variantFlags", int(summary.get("variantFlags", 0)))
    special_flags = _validate_u8("specialFlags", int(summary.get("specialFlags", 0)))

    write_u16_le(eeprom, offset, species_id)
    write_u16_le(eeprom, offset + 2, held_item)
    for index, move in enumerate(moves):
        write_u16_le(eeprom, offset + 4 + index * 2, move)
    eeprom[offset + POKE_SUMMARY_LEVEL_OFFSET] = level
    eeprom[offset + POKE_SUMMARY_VARIANT_FLAGS_OFFSET] = variant_flags
    eeprom[offset + POKE_SUMMARY_SPECIAL_FLAGS_OFFSET] = special_flags
    eeprom[offset + 15] = 0


def _load_walker_encounters() -> dict[int, list[dict[str, Any]]]:
    global _WALKER_ENCOUNTERS_CACHE
    if _WALKER_ENCOUNTERS_CACHE is not None:
        return _WALKER_ENCOUNTERS_CACHE

    courses: dict[int, list[dict[str, Any]]] = {}
    if PKHEX_WALKER_ENCOUNTER_PATH.exists():
        payload = PKHEX_WALKER_ENCOUNTER_PATH.read_bytes()
        slot_size = 0x0C
        slots_per_course = 6
        if len(payload) % slot_size == 0:
            total_slots = len(payload) // slot_size
            total_courses = total_slots // slots_per_course
            for course_id in range(total_courses):
                entries: list[dict[str, Any]] = []
                for slot in range(slots_per_course):
                    offset = (course_id * slots_per_course + slot) * slot_size
                    species_id = payload[offset] | (payload[offset + 1] << 8)
                    level = payload[offset + 2]
                    gender = payload[offset + 3]
                    moves = [
                        payload[offset + 4] | (payload[offset + 5] << 8),
                        payload[offset + 6] | (payload[offset + 7] << 8),
                        payload[offset + 8] | (payload[offset + 9] << 8),
                        payload[offset + 10] | (payload[offset + 11] << 8),
                    ]
                    entries.append(
                        {
                            "speciesId": species_id,
                            "level": level,
                            "gender": gender,
                            "moves": moves,
                        }
                    )
                courses[course_id] = entries

    if not courses:
        slots_per_course = 6
        total_courses = len(COURSE_SPECIES) // slots_per_course
        for course_id in range(total_courses):
            entries: list[dict[str, Any]] = []
            for slot in range(slots_per_course):
                species_id = COURSE_SPECIES[course_id * slots_per_course + slot]
                entries.append(
                    {
                        "speciesId": species_id,
                        "level": 10,
                        "gender": 0,
                        "moves": [0, 0, 0, 0],
                    }
                )
            courses[course_id] = entries

    _WALKER_ENCOUNTERS_CACHE = courses
    return courses


def _find_walker_encounter_by_species(
    species_id: int,
    *,
    preferred_course: int | None,
) -> dict[str, Any] | None:
    sid = _validate_u16("speciesId", int(species_id))
    courses = _load_walker_encounters()

    if preferred_course is not None:
        entries = courses.get(int(preferred_course), [])
        for entry in entries:
            if int(entry.get("speciesId", 0)) == sid:
                return dict(entry)

    for course_id in sorted(courses.keys()):
        for entry in courses[course_id]:
            if int(entry.get("speciesId", 0)) == sid:
                return dict(entry)

    return None


def _weighted_choice(weighted: list[tuple[Any, int]], rng: random.Random) -> Any:
    total = sum(weight for _, weight in weighted)
    if total <= 0:
        return weighted[0][0]
    roll = rng.randrange(total)
    for value, weight in weighted:
        roll -= weight
        if roll < 0:
            return value
    return weighted[-1][0]


def _course_unlock_rule(course_id: int) -> dict[str, Any]:
    if course_id < 0 or course_id >= len(COURSE_NAMES):
        raise ValueError(f"courseId out of range (0..{len(COURSE_NAMES) - 1}): {course_id}")
    return dict(COURSE_UNLOCK_RULES[course_id])


def _unlocked_course_ids_from_flags(flags: int) -> list[int]:
    mask = int(flags) & COURSE_UNLOCK_MASK
    return [course_id for course_id in range(len(COURSE_NAMES)) if ((mask >> course_id) & 0x1) == 1]


def _course_unlock_available(
    *,
    watts: int,
    rule: dict[str, Any],
    assume_national_dex: bool,
    unlock_special_courses: bool,
    unlock_event_courses: bool,
) -> bool:
    threshold = rule.get("watts")
    requires_national_dex = bool(rule.get("requires_national_dex", False))
    special = rule.get("special")

    if threshold is not None:
        if int(watts) < int(threshold):
            return False
        if requires_national_dex and not assume_national_dex:
            return False
        return True

    if special in ("gts-trade", "jirachi-trade"):
        return unlock_special_courses
    if special == "event":
        return unlock_event_courses
    return False


def compute_course_unlock_flags(
    *,
    watts: int,
    existing_flags: int = 0,
    assume_national_dex: bool = True,
    unlock_special_courses: bool = False,
    unlock_event_courses: bool = False,
) -> int:
    current_watts = max(0, int(watts))
    flags = int(existing_flags) & COURSE_UNLOCK_MASK

    for course_id in range(len(COURSE_NAMES)):
        rule = _course_unlock_rule(course_id)
        if _course_unlock_available(
            watts=current_watts,
            rule=rule,
            assume_national_dex=assume_national_dex,
            unlock_special_courses=unlock_special_courses,
            unlock_event_courses=unlock_event_courses,
        ):
            flags |= 1 << course_id

    return flags & COURSE_UNLOCK_MASK


def build_course_unlock_state(
    eeprom: bytearray,
    *,
    existing_flags: int = 0,
    assume_national_dex: bool = True,
    unlock_special_courses: bool = False,
    unlock_event_courses: bool = False,
) -> dict[str, Any]:
    _check_size(eeprom)
    watts = read_current_watts(eeprom)
    flags = compute_course_unlock_flags(
        watts=watts,
        existing_flags=existing_flags,
        assume_national_dex=assume_national_dex,
        unlock_special_courses=unlock_special_courses,
        unlock_event_courses=unlock_event_courses,
    )
    unlocked = _unlocked_course_ids_from_flags(flags)

    courses: list[dict[str, Any]] = []
    next_unlock: dict[str, Any] | None = None

    for course_id, course_name in enumerate(COURSE_NAMES):
        rule = _course_unlock_rule(course_id)
        threshold = rule.get("watts")
        requires_national_dex = bool(rule.get("requires_national_dex", False))
        special = rule.get("special")
        is_unlocked = ((flags >> course_id) & 0x1) == 1

        remaining_watts: int | None
        if threshold is None:
            remaining_watts = None
        else:
            remaining_watts = max(0, int(threshold) - int(watts))

        entry = {
            "courseId": course_id,
            "courseName": course_name,
            "unlocked": is_unlocked,
            "requiredWatts": threshold,
            "remainingWatts": remaining_watts,
            "requiresNationalDex": requires_national_dex,
            "specialRequirement": special,
        }
        courses.append(entry)

        if is_unlocked or threshold is None:
            continue
        if requires_national_dex and not assume_national_dex:
            continue
        if next_unlock is None or int(remaining_watts) < int(next_unlock["remainingWatts"]):
            next_unlock = {
                "courseId": course_id,
                "courseName": course_name,
                "requiredWatts": int(threshold),
                "remainingWatts": int(remaining_watts),
            }

    return {
        "watts": int(watts),
        "unlockFlags": int(flags),
        "unlockFlagsHex": f"0x{int(flags):08X}",
        "assumeNationalDex": bool(assume_national_dex),
        "unlockSpecialCourses": bool(unlock_special_courses),
        "unlockEventCourses": bool(unlock_event_courses),
        "unlockedCourses": unlocked,
        "unlockedCourseNames": [COURSE_NAMES[course_id] for course_id in unlocked],
        "nextWattsUnlock": next_unlock,
        "courses": courses,
    }



def _check_size(eeprom: bytearray) -> None:
    if len(eeprom) != EEPROM_SIZE:
        raise ValueError(f"Expected EEPROM size {EEPROM_SIZE}, got {len(eeprom)}")


def _validate_u32(name: str, value: int) -> int:
    ivalue = int(value)
    if ivalue < 0 or ivalue > 0xFFFFFFFF:
        raise ValueError(f"{name} out of range (0..4294967295): {value}")
    return ivalue


def _validate_u16(name: str, value: int) -> int:
    ivalue = int(value)
    if ivalue < 0 or ivalue > 0xFFFF:
        raise ValueError(f"{name} out of range (0..65535): {value}")
    return ivalue


def _validate_u8(name: str, value: int) -> int:
    ivalue = int(value)
    if ivalue < 0 or ivalue > 0xFF:
        raise ValueError(f"{name} out of range (0..255): {value}")
    return ivalue


def _slot_offset(base: int, slot: int, slots: int, entry_size: int) -> int:
    islot = int(slot)
    if islot < 0 or islot >= slots:
        raise ValueError(f"slot out of range (0..{slots - 1}): {slot}")
    return base + islot * entry_size


def read_u16_le(data: bytearray, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def write_u16_le(data: bytearray, offset: int, value: int) -> None:
    data[offset] = value & 0xFF
    data[offset + 1] = (value >> 8) & 0xFF


def read_u16_be(data: bytearray, offset: int) -> int:
    return (data[offset] << 8) | data[offset + 1]


def write_u16_be(data: bytearray, offset: int, value: int) -> None:
    data[offset] = (value >> 8) & 0xFF
    data[offset + 1] = value & 0xFF


def read_u32_be(data: bytearray, offset: int) -> int:
    return (
        (data[offset] << 24)
        | (data[offset + 1] << 16)
        | (data[offset + 2] << 8)
        | data[offset + 3]
    )


def write_u32_be(data: bytearray, offset: int, value: int) -> None:
    data[offset] = (value >> 24) & 0xFF
    data[offset + 1] = (value >> 16) & 0xFF
    data[offset + 2] = (value >> 8) & 0xFF
    data[offset + 3] = value & 0xFF


def decode_device_code_unit(code_unit: int) -> str | None:
    if code_unit in (0x0000, 0xFFFF):
        return None
    if 0x0121 <= code_unit <= 0x012A:
        return chr(ord("0") + (code_unit - 0x0121))
    if 0x012B <= code_unit <= 0x0144:
        return chr(ord("A") + (code_unit - 0x012B))
    if 0x0145 <= code_unit <= 0x015E:
        return chr(ord("a") + (code_unit - 0x0145))

    punctuation = {
        0x00E1: "!",
        0x00E2: "?",
        0x00E6: "*",
        0x00E7: "/",
        0x00F0: "+",
        0x00F1: "-",
        0x00F4: "=",
        0x00F8: ".",
        0x00F9: ",",
    }
    if code_unit in punctuation:
        return punctuation[code_unit]
    if 0x20 <= code_unit <= 0x7E:
        return chr(code_unit)
    return "?"


def encode_device_char(ch: str) -> int:
    if "0" <= ch <= "9":
        return 0x0121 + (ord(ch) - ord("0"))
    if "A" <= ch <= "Z":
        return 0x012B + (ord(ch) - ord("A"))
    if "a" <= ch <= "z":
        return 0x0145 + (ord(ch) - ord("a"))

    punctuation = {
        "!": 0x00E1,
        "?": 0x00E2,
        "*": 0x00E6,
        "/": 0x00E7,
        "+": 0x00F0,
        "-": 0x00F1,
        "=": 0x00F4,
        ".": 0x00F8,
        ",": 0x00F9,
    }
    if ch in punctuation:
        return punctuation[ch]
    if 0x20 <= ord(ch) <= 0x7E:
        return ord(ch)
    return 0x00E2


def read_trainer_name(eeprom: bytearray) -> str:
    _check_size(eeprom)
    chars = []
    for index in range(MAX_TRAINER_CHARS):
        code_unit = read_u16_le(eeprom, IDENTITY_OWNER_NAME_OFFSET + index * 2)
        decoded = decode_device_code_unit(code_unit)
        if decoded is None:
            break
        chars.append(decoded)

    name = "".join(chars).strip()
    return name if name else "UNKNOWN"


def set_trainer_name(eeprom: bytearray, trainer_name: str) -> None:
    _check_size(eeprom)
    clean_name = (trainer_name or "").strip()[:MAX_TRAINER_CHARS]

    for index in range(MAX_TRAINER_CHARS):
        code_unit = 0x0000
        if index < len(clean_name):
            code_unit = encode_device_char(clean_name[index])
        write_u16_le(eeprom, IDENTITY_OWNER_NAME_OFFSET + index * 2, code_unit)


def read_current_watts(eeprom: bytearray) -> int:
    _check_size(eeprom)
    health_watts = read_u16_be(eeprom, HEALTH_CUR_WATTS_OFFSET)
    general_watts = read_u16_be(eeprom, GENERAL_WATTS_OFFSET)
    session_watts = read_u16_be(eeprom, SESSION_WATTS_OFFSET)

    values = [
        candidate
        for candidate in (session_watts, health_watts, general_watts)
        if candidate > 0 and candidate <= 0xFFFF and candidate not in (0x07FF, 0xFFFF)
    ]
    if not values:
        return 0

    repeated = next((value for value in set(values) if values.count(value) >= 2), None)
    if repeated is not None:
        return repeated

    if session_watts in values and session_watts <= 9999:
        return session_watts

    return min(values)


def set_watts(eeprom: bytearray, watts: int) -> None:
    _check_size(eeprom)
    clamped = max(0, min(int(watts), 0xFFFF))
    write_u16_be(eeprom, HEALTH_CUR_WATTS_OFFSET, clamped)
    write_u16_be(eeprom, GENERAL_WATTS_OFFSET, clamped)
    write_u16_be(eeprom, SESSION_WATTS_OFFSET, clamped)


def read_steps(eeprom: bytearray) -> int:
    _check_size(eeprom)
    identity_steps = read_u32_be(eeprom, IDENTITY_STEP_COUNT_OFFSET)
    if identity_steps != 0:
        return identity_steps

    today_steps = read_u32_be(eeprom, HEALTH_TODAY_STEPS_OFFSET)
    lifetime_steps = read_u32_be(eeprom, HEALTH_LIFETIME_STEPS_OFFSET)
    return max(today_steps, lifetime_steps)


def set_steps(eeprom: bytearray, steps: int) -> None:
    _check_size(eeprom)
    clamped = max(0, min(int(steps), 0xFFFFFFFF))
    write_u32_be(eeprom, IDENTITY_STEP_COUNT_OFFSET, clamped)
    write_u32_be(eeprom, HEALTH_LIFETIME_STEPS_OFFSET, clamped)
    write_u32_be(eeprom, HEALTH_TODAY_STEPS_OFFSET, clamped)


def set_last_sync_seconds(eeprom: bytearray, epoch_seconds: int) -> None:
    _check_size(eeprom)
    clamped = max(0, min(int(epoch_seconds), 0xFFFFFFFF))
    write_u32_be(eeprom, IDENTITY_LAST_SYNC_OFFSET, clamped)
    write_u32_be(eeprom, HEALTH_LAST_SYNC_OFFSET, clamped)
    write_u32_be(eeprom, GENERAL_LAST_SYNC_OFFSET, clamped)


def read_snapshot(eeprom: bytearray) -> Dict[str, int | str]:
    _check_size(eeprom)
    return {
        "trainerName": read_trainer_name(eeprom),
        "trainer": read_trainer_name(eeprom),
        "steps": read_steps(eeprom),
        "watts": read_current_watts(eeprom),
        "protocolVersion": eeprom[IDENTITY_PROTOCOL_VERSION_OFFSET],
        "protocolSubVersion": eeprom[IDENTITY_PROTOCOL_SUB_VERSION_OFFSET],
        "lastSyncEpochSeconds": read_u32_be(eeprom, IDENTITY_LAST_SYNC_OFFSET),
    }


def _read_step_history(eeprom: bytearray) -> list[int]:
    return [
        read_u32_be(eeprom, STEP_HISTORY_OFFSET + index * 4)
        for index in range(STEP_HISTORY_DAYS)
    ]


def _write_step_history(eeprom: bytearray, values: list[int]) -> None:
    if len(values) != STEP_HISTORY_DAYS:
        raise ValueError(f"stepHistory must contain exactly {STEP_HISTORY_DAYS} values")
    for index, value in enumerate(values):
        write_u32_be(
            eeprom,
            STEP_HISTORY_OFFSET + index * 4,
            _validate_u32(f"stepHistory[{index}]", value),
        )


def read_identity_section(eeprom: bytearray) -> Dict[str, int | str]:
    _check_size(eeprom)
    return {
        "trainerName": read_trainer_name(eeprom),
        "trainerTidBE": read_u16_be(eeprom, IDENTITY_TRAINER_TID_OFFSET),
        "trainerTidLE": read_u16_le(eeprom, IDENTITY_TRAINER_TID_OFFSET),
        "trainerSidBE": read_u16_be(eeprom, IDENTITY_TRAINER_SID_OFFSET),
        "trainerSidLE": read_u16_le(eeprom, IDENTITY_TRAINER_SID_OFFSET),
        "protocolVersion": eeprom[IDENTITY_PROTOCOL_VERSION_OFFSET],
        "protocolSubVersion": eeprom[IDENTITY_PROTOCOL_SUB_VERSION_OFFSET],
        "lastSyncEpochSeconds": read_u32_be(eeprom, IDENTITY_LAST_SYNC_OFFSET),
        "identityStepCount": read_u32_be(eeprom, IDENTITY_STEP_COUNT_OFFSET),
    }


def set_identity_section(
    eeprom: bytearray,
    *,
    trainer_name: str | None = None,
    protocol_version: int | None = None,
    protocol_sub_version: int | None = None,
    last_sync_epoch_seconds: int | None = None,
    step_count: int | None = None,
) -> None:
    _check_size(eeprom)
    if trainer_name is not None:
        set_trainer_name(eeprom, trainer_name)
    if protocol_version is not None:
        eeprom[IDENTITY_PROTOCOL_VERSION_OFFSET] = _validate_u8("protocolVersion", protocol_version)
    if protocol_sub_version is not None:
        eeprom[IDENTITY_PROTOCOL_SUB_VERSION_OFFSET] = _validate_u8(
            "protocolSubVersion", protocol_sub_version
        )
    if last_sync_epoch_seconds is not None:
        set_last_sync_seconds(eeprom, last_sync_epoch_seconds)
    if step_count is not None:
        set_steps(eeprom, step_count)


def read_stats_section(eeprom: bytearray) -> Dict[str, Any]:
    _check_size(eeprom)
    return {
        "steps": read_steps(eeprom),
        "identityStepCount": read_u32_be(eeprom, IDENTITY_STEP_COUNT_OFFSET),
        "lifetimeSteps": read_u32_be(eeprom, HEALTH_LIFETIME_STEPS_OFFSET),
        "todaySteps": read_u32_be(eeprom, HEALTH_TODAY_STEPS_OFFSET),
        "watts": read_current_watts(eeprom),
        "healthWatts": read_u16_be(eeprom, HEALTH_CUR_WATTS_OFFSET),
        "generalWatts": read_u16_be(eeprom, GENERAL_WATTS_OFFSET),
        "sessionWatts": read_u16_be(eeprom, SESSION_WATTS_OFFSET),
        "lastSyncIdentity": read_u32_be(eeprom, IDENTITY_LAST_SYNC_OFFSET),
        "lastSyncHealth": read_u32_be(eeprom, HEALTH_LAST_SYNC_OFFSET),
        "lastSyncGeneral": read_u32_be(eeprom, GENERAL_LAST_SYNC_OFFSET),
        "stepHistory": _read_step_history(eeprom),
    }


def set_stats_section(
    eeprom: bytearray,
    *,
    steps: int | None = None,
    lifetime_steps: int | None = None,
    today_steps: int | None = None,
    watts: int | None = None,
    last_sync_epoch_seconds: int | None = None,
    step_history: list[int] | None = None,
) -> None:
    _check_size(eeprom)
    if steps is not None:
        set_steps(eeprom, steps)

    if lifetime_steps is not None:
        write_u32_be(
            eeprom,
            HEALTH_LIFETIME_STEPS_OFFSET,
            _validate_u32("lifetimeSteps", lifetime_steps),
        )
    if today_steps is not None:
        write_u32_be(
            eeprom,
            HEALTH_TODAY_STEPS_OFFSET,
            _validate_u32("todaySteps", today_steps),
        )

    if read_u32_be(eeprom, HEALTH_TODAY_STEPS_OFFSET) > read_u32_be(
        eeprom, HEALTH_LIFETIME_STEPS_OFFSET
    ):
        raise ValueError("todaySteps cannot be greater than lifetimeSteps")

    if watts is not None:
        set_watts(eeprom, watts)
    if last_sync_epoch_seconds is not None:
        set_last_sync_seconds(eeprom, last_sync_epoch_seconds)
    if step_history is not None:
        _write_step_history(eeprom, step_history)


def read_stroll_section(eeprom: bytearray) -> Dict[str, Any]:
    _check_size(eeprom)
    walking = _read_pokemon_summary(eeprom, ROUTE_INFO_OFFSET)
    route_index = eeprom[ROUTE_IMAGE_INDEX_OFFSET]
    route_name = _read_device_text_fixed(eeprom, ROUTE_NAME_OFFSET, ROUTE_NAME_CHARS)
    route_course_name = COURSE_NAMES[route_index] if 0 <= route_index < len(COURSE_NAMES) else None
    return {
        "sessionWatts": read_u16_be(eeprom, SESSION_WATTS_OFFSET),
        "walkingSpecies": walking["speciesId"],
        "walkingSpeciesName": walking["speciesName"],
        "walkingCompanion": walking,
        "walkingNickname": _read_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS),
        "walkingFriendship": eeprom[ROUTE_FRIENDSHIP_OFFSET],
        "routeImageIndex": route_index,
        "routeCourseName": route_course_name,
        "routeName": route_name,
        "routeInfoOffset": ROUTE_INFO_OFFSET,
        "routeInfoSize": ROUTE_INFO_SIZE,
        "routeInfoPreviewHex": eeprom[
            ROUTE_INFO_OFFSET : ROUTE_INFO_OFFSET + min(32, ROUTE_INFO_SIZE)
        ].hex(),
    }


def set_stroll_section(
    eeprom: bytearray,
    *,
    session_watts: int | None = None,
    route_image_index: int | None = None,
) -> None:
    _check_size(eeprom)
    if session_watts is not None:
        set_watts(eeprom, session_watts)
    if route_image_index is not None:
        eeprom[ROUTE_IMAGE_INDEX_OFFSET] = _validate_u8("routeImageIndex", route_image_index)


def _read_route_slot(eeprom: bytearray, slot: int) -> dict[str, Any]:
    if slot < 0 or slot >= ROUTE_POKE_SLOTS:
        raise ValueError(f"route slot out of range (0..{ROUTE_POKE_SLOTS - 1}): {slot}")

    base = ROUTE_POKES_OFFSET + slot * POKE_SUMMARY_SIZE
    summary = _read_pokemon_summary(eeprom, base)
    min_steps = read_u16_le(eeprom, ROUTE_POKE_MIN_STEPS_OFFSET + slot * 2)
    chance = eeprom[ROUTE_POKE_CHANCE_OFFSET + slot]
    return {
        "slot": slot,
        "minSteps": min_steps,
        "chance": chance,
        "pokemon": summary,
    }


def _write_route_slot(
    eeprom: bytearray,
    slot: int,
    summary: dict[str, Any],
    *,
    min_steps: int | None = None,
    chance: int | None = None,
) -> None:
    if slot < 0 or slot >= ROUTE_POKE_SLOTS:
        raise ValueError(f"route slot out of range (0..{ROUTE_POKE_SLOTS - 1}): {slot}")

    base = ROUTE_POKES_OFFSET + slot * POKE_SUMMARY_SIZE
    _write_pokemon_summary(eeprom, base, summary)
    if min_steps is not None:
        write_u16_le(eeprom, ROUTE_POKE_MIN_STEPS_OFFSET + slot * 2, _validate_u16("minSteps", min_steps))
    if chance is not None:
        eeprom[ROUTE_POKE_CHANCE_OFFSET + slot] = _validate_u8("chance", chance)


def _read_route_slots(eeprom: bytearray) -> list[dict[str, Any]]:
    return [_read_route_slot(eeprom, slot) for slot in range(ROUTE_POKE_SLOTS)]


def _configure_route_from_course(
    eeprom: bytearray,
    course_id: int,
    *,
    rng: random.Random,
) -> list[dict[str, Any]]:
    if course_id < 0 or course_id >= len(COURSE_NAMES):
        raise ValueError(f"courseId out of range (0..{len(COURSE_NAMES) - 1}): {course_id}")

    courses = _load_walker_encounters()
    entries = courses.get(course_id)
    if not entries or len(entries) < 6:
        raise ValueError(f"No encounter data available for course {course_id}")

    configured: list[dict[str, Any]] = []
    for group in range(ROUTE_POKE_SLOTS):
        left = entries[group * 2]
        right = entries[group * 2 + 1]
        pick = left if rng.randint(0, 1) == 0 else right
        summary = _pokemon_summary_from_values(
            int(pick["speciesId"]),
            level=int(pick.get("level", 10)),
            moves=list(pick.get("moves", [0, 0, 0, 0])),
        )
        _write_route_slot(
            eeprom,
            group,
            summary,
            min_steps=DEFAULT_ROUTE_SLOT_MIN_STEPS[group],
            chance=DEFAULT_ROUTE_SLOT_CHANCE[group],
        )
        configured.append(
            {
                "slot": group,
                "speciesId": summary["speciesId"],
                "speciesName": _species_name(summary["speciesId"]),
                "level": summary["level"],
                "chance": DEFAULT_ROUTE_SLOT_CHANCE[group],
                "minSteps": DEFAULT_ROUTE_SLOT_MIN_STEPS[group],
            }
        )

    eeprom[ROUTE_IMAGE_INDEX_OFFSET] = course_id & 0xFF
    _write_device_text_fixed(eeprom, ROUTE_NAME_OFFSET, ROUTE_NAME_CHARS, COURSE_NAMES[course_id])
    return configured


def read_inventory_section(eeprom: bytearray) -> Dict[str, Any]:
    _check_size(eeprom)

    caught = []
    for slot in range(INVENTORY_CAUGHT_SLOTS):
        base = _slot_offset(
            INVENTORY_CAUGHT_OFFSET,
            slot,
            INVENTORY_CAUGHT_SLOTS,
            INVENTORY_CAUGHT_ENTRY_SIZE,
        )
        summary = _read_pokemon_summary(eeprom, base)
        caught.append(
            {
                "slot": slot,
                "species": summary["speciesId"],
                "speciesId": summary["speciesId"],
                "speciesName": summary["speciesName"],
                "heldItem": summary["heldItem"],
                "moves": summary["moves"],
                "level": summary["level"],
                "variantFlags": summary["variantFlags"],
                "specialFlags": summary["specialFlags"],
                "rawHex": eeprom[base : base + INVENTORY_CAUGHT_ENTRY_SIZE].hex(),
            }
        )

    dowsed = []
    for slot in range(INVENTORY_DOWSED_SLOTS):
        base = _slot_offset(
            INVENTORY_DOWSED_OFFSET,
            slot,
            INVENTORY_DOWSED_SLOTS,
            INVENTORY_ITEM_ENTRY_SIZE,
        )
        dowsed.append(
            {
                "slot": slot,
                "itemId": read_u16_le(eeprom, base),
                "unused": read_u16_le(eeprom, base + 2),
            }
        )

    gifted = []
    for slot in range(INVENTORY_GIFTED_SLOTS):
        base = _slot_offset(
            INVENTORY_GIFTED_OFFSET,
            slot,
            INVENTORY_GIFTED_SLOTS,
            INVENTORY_ITEM_ENTRY_SIZE,
        )
        gifted.append(
            {
                "slot": slot,
                "itemId": read_u16_le(eeprom, base),
                "unused": read_u16_le(eeprom, base + 2),
            }
        )

    return {
        "caught": caught,
        "dowsedItems": dowsed,
        "giftedItems": gifted,
    }


def set_inventory_dowsed_item(eeprom: bytearray, slot: int, item_id: int) -> None:
    _check_size(eeprom)
    base = _slot_offset(
        INVENTORY_DOWSED_OFFSET,
        slot,
        INVENTORY_DOWSED_SLOTS,
        INVENTORY_ITEM_ENTRY_SIZE,
    )
    write_u16_le(eeprom, base, _validate_u16("itemId", item_id))
    write_u16_le(eeprom, base + 2, 0)


def set_inventory_gifted_item(eeprom: bytearray, slot: int, item_id: int) -> None:
    _check_size(eeprom)
    base = _slot_offset(
        INVENTORY_GIFTED_OFFSET,
        slot,
        INVENTORY_GIFTED_SLOTS,
        INVENTORY_ITEM_ENTRY_SIZE,
    )
    write_u16_le(eeprom, base, _validate_u16("itemId", item_id))
    write_u16_le(eeprom, base + 2, 0)


def set_inventory_caught_species(eeprom: bytearray, slot: int, species_id: int) -> None:
    _check_size(eeprom)
    base = _slot_offset(
        INVENTORY_CAUGHT_OFFSET,
        slot,
        INVENTORY_CAUGHT_SLOTS,
        INVENTORY_CAUGHT_ENTRY_SIZE,
    )
    write_u16_le(eeprom, base, _validate_u16("speciesId", species_id))


def set_inventory_caught_summary(
    eeprom: bytearray,
    slot: int,
    summary: dict[str, Any],
) -> None:
    _check_size(eeprom)
    base = _slot_offset(
        INVENTORY_CAUGHT_OFFSET,
        slot,
        INVENTORY_CAUGHT_SLOTS,
        INVENTORY_CAUGHT_ENTRY_SIZE,
    )
    _write_pokemon_summary(eeprom, base, summary)


def add_inventory_caught_summary(
    eeprom: bytearray,
    summary: dict[str, Any],
    replace_slot: int | None = None,
) -> Dict[str, Any]:
    _check_size(eeprom)
    target_slot = replace_slot if replace_slot is not None else _find_empty_species_slot(eeprom)
    if target_slot is None:
        raise ValueError("No empty caught slot available; provide replaceSlot")

    set_inventory_caught_summary(eeprom, target_slot, summary)
    species_id = _validate_u16("speciesId", int(summary.get("speciesId", 0)))
    level = max(0, min(int(summary.get("level", 0)), 100))
    return {
        "slot": target_slot,
        "speciesId": species_id,
        "speciesName": _species_name(species_id),
        "level": level,
    }


def _find_empty_species_slot(eeprom: bytearray) -> int | None:
    for slot in range(INVENTORY_CAUGHT_SLOTS):
        base = INVENTORY_CAUGHT_OFFSET + slot * INVENTORY_CAUGHT_ENTRY_SIZE
        if read_u16_le(eeprom, base) == 0:
            return slot
    return None


def _find_empty_item_slot(base: int, slots: int, eeprom: bytearray) -> int | None:
    for slot in range(slots):
        item_offset = base + slot * INVENTORY_ITEM_ENTRY_SIZE
        if read_u16_le(eeprom, item_offset) == 0:
            return slot
    return None


def add_inventory_caught_species(
    eeprom: bytearray, species_id: int, replace_slot: int | None = None
) -> Dict[str, int]:
    _check_size(eeprom)
    preferred_course = int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])
    legal_entry = _find_walker_encounter_by_species(species_id, preferred_course=preferred_course)

    if legal_entry is None:
        summary = _pokemon_summary_from_values(species_id, level=10)
    else:
        level = max(1, min(int(legal_entry.get("level", 10)), 100))
        raw_moves = legal_entry.get("moves", [0, 0, 0, 0])
        moves = [0, 0, 0, 0]
        if isinstance(raw_moves, list):
            for index, value in enumerate(raw_moves[:4]):
                moves[index] = _validate_u16(f"moves[{index}]", int(value))
        gender = int(legal_entry.get("gender", 0))
        variant_flags = 0x20 if gender == 1 else 0
        summary = _pokemon_summary_from_values(
            species_id,
            level=level,
            moves=moves,
            variant_flags=variant_flags,
        )

    placement = add_inventory_caught_summary(eeprom, summary, replace_slot)
    return {
        "slot": int(placement["slot"]),
        "speciesId": int(placement["speciesId"]),
    }


def add_inventory_dowsed_item(
    eeprom: bytearray, item_id: int, replace_slot: int | None = None
) -> Dict[str, int]:
    _check_size(eeprom)
    target_slot = (
        replace_slot
        if replace_slot is not None
        else _find_empty_item_slot(INVENTORY_DOWSED_OFFSET, INVENTORY_DOWSED_SLOTS, eeprom)
    )
    if target_slot is None:
        raise ValueError("No empty dowsed item slot available; provide replaceSlot")

    set_inventory_dowsed_item(eeprom, target_slot, item_id)
    return {"slot": target_slot, "itemId": _validate_u16("itemId", item_id)}


def add_inventory_gifted_item(
    eeprom: bytearray, item_id: int, replace_slot: int | None = None
) -> Dict[str, int]:
    _check_size(eeprom)
    target_slot = (
        replace_slot
        if replace_slot is not None
        else _find_empty_item_slot(INVENTORY_GIFTED_OFFSET, INVENTORY_GIFTED_SLOTS, eeprom)
    )
    if target_slot is None:
        raise ValueError("No empty gifted item slot available; provide replaceSlot")

    set_inventory_gifted_item(eeprom, target_slot, item_id)
    return {"slot": target_slot, "itemId": _validate_u16("itemId", item_id)}


def add_walked_steps(eeprom: bytearray, walked_steps: int) -> Dict[str, int]:
    _check_size(eeprom)
    added_steps = _validate_u32("walkedSteps", walked_steps)

    identity_steps = read_u32_be(eeprom, IDENTITY_STEP_COUNT_OFFSET)
    lifetime_steps = read_u32_be(eeprom, HEALTH_LIFETIME_STEPS_OFFSET)
    today_steps = read_u32_be(eeprom, HEALTH_TODAY_STEPS_OFFSET)
    current_watts = read_current_watts(eeprom)

    next_identity_steps = min(0xFFFFFFFF, identity_steps + added_steps)
    next_lifetime_steps = min(0xFFFFFFFF, lifetime_steps + added_steps)
    next_today_steps = min(0xFFFFFFFF, today_steps + added_steps)

    gained_watts = added_steps // 20
    next_watts = min(0xFFFF, current_watts + gained_watts)

    write_u32_be(eeprom, IDENTITY_STEP_COUNT_OFFSET, next_identity_steps)
    write_u32_be(eeprom, HEALTH_LIFETIME_STEPS_OFFSET, next_lifetime_steps)
    write_u32_be(eeprom, HEALTH_TODAY_STEPS_OFFSET, next_today_steps)
    set_watts(eeprom, next_watts)

    return {
        "addedSteps": added_steps,
        "gainedWatts": gained_watts,
        "steps": read_steps(eeprom),
        "watts": read_current_watts(eeprom),
        "todaySteps": next_today_steps,
        "lifetimeSteps": next_lifetime_steps,
    }


def _roll_route_slot_for_capture(
    eeprom: bytearray,
    *,
    walked_steps: int,
    rng: random.Random,
) -> dict[str, Any] | None:
    weighted: list[tuple[dict[str, Any], int]] = []
    fallback: list[dict[str, Any]] = []

    for route_slot in _read_route_slots(eeprom):
        summary = route_slot["pokemon"]
        if int(summary.get("speciesId", 0)) == 0:
            continue

        fallback.append(route_slot)
        if walked_steps < int(route_slot["minSteps"]):
            continue

        chance = max(0, min(int(route_slot["chance"]), 100))
        if chance <= 0:
            continue
        weighted.append((route_slot, chance))

    if weighted:
        return _weighted_choice(weighted, rng)
    if fallback:
        return fallback[0]
    return None


def append_journal_event(
    eeprom: bytearray,
    *,
    event_type: int,
    walking_species: int,
    caught_species: int = 0,
    step_count: int = 0,
    watts: int = 0,
    remote_watts: int = 0,
    remote_step_count: int = 0,
    route_image_idx: int | None = None,
    friendship: int | None = None,
    extra_data: int = 0,
    companion_name: str | None = None,
    trainer_name: str | None = None,
    event_time: int | None = None,
) -> Dict[str, int]:
    _check_size(eeprom)

    now = _validate_u32("eventTime", int(time.time()) if event_time is None else int(event_time))
    event_type_u16 = _validate_u16("eventType", event_type)
    walking_species_u16 = _validate_u16("walkingSpecies", walking_species)
    caught_species_u16 = _validate_u16("caughtSpecies", caught_species)
    step_count_u32 = _validate_u32("stepCount", step_count)
    watts_u16 = _validate_u16("watts", watts)
    remote_watts_u16 = _validate_u16("remoteWatts", remote_watts)
    remote_step_count_u32 = _validate_u32("remoteStepCount", remote_step_count)
    extra_data_u16 = _validate_u16("extraData", extra_data)

    route_idx = (
        int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])
        if route_image_idx is None
        else _validate_u8("routeImageIndex", route_image_idx)
    )
    friendship_u8 = (
        int(eeprom[ROUTE_FRIENDSHIP_OFFSET])
        if friendship is None
        else _validate_u8("friendship", friendship)
    )

    local_trainer = read_trainer_name(eeprom) if trainer_name is None else trainer_name
    local_companion = (
        _read_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS)
        if companion_name is None
        else companion_name
    )

    start = EVENT_LOG_OFFSET
    stride = EVENT_LOG_ENTRY_SIZE
    tail = bytes(eeprom[start : start + stride * (EVENT_LOG_ENTRIES - 1)])
    eeprom[start + stride : start + stride * EVENT_LOG_ENTRIES] = tail
    eeprom[start : start + stride] = b"\x00" * stride

    base = EVENT_LOG_OFFSET
    write_u32_be(eeprom, base, now)
    write_u16_le(eeprom, base + EVENT_LOG_WALKING_SPECIES_OFFSET, walking_species_u16)
    write_u16_le(eeprom, base + EVENT_LOG_CAUGHT_SPECIES_OFFSET, caught_species_u16)
    write_u16_le(eeprom, base + EVENT_LOG_EXTRA_DATA_OFFSET, extra_data_u16)

    _write_device_text_fixed(eeprom, base + 16, 8, local_trainer)
    _write_device_text_fixed(eeprom, base + 32, 11, local_companion)
    _write_device_text_fixed(eeprom, base + 54, 11, "")

    eeprom[base + EVENT_LOG_ROUTE_IMAGE_OFFSET] = route_idx
    eeprom[base + EVENT_LOG_FRIENDSHIP_OFFSET] = friendship_u8
    write_u16_be(eeprom, base + EVENT_LOG_WATTS_OFFSET, watts_u16)
    write_u16_be(eeprom, base + EVENT_LOG_REMOTE_WATTS_OFFSET, remote_watts_u16)
    write_u32_be(eeprom, base + EVENT_LOG_STEP_COUNT_OFFSET, step_count_u32)
    write_u32_be(eeprom, base + EVENT_LOG_REMOTE_STEP_COUNT_OFFSET, remote_step_count_u32)
    write_u16_le(eeprom, base + EVENT_LOG_TYPE_OFFSET, event_type_u16)
    eeprom[base + EVENT_LOG_GENDER_FORM_OFFSET] = 0
    eeprom[base + EVENT_LOG_CAUGHT_GENDER_FORM_OFFSET] = 0

    return {
        "index": 0,
        "eventType": event_type_u16,
        "eventTime": now,
        "walkingSpecies": walking_species_u16,
        "caughtSpecies": caught_species_u16,
    }


def send_pokemon_to_stroll(
    eeprom: bytearray,
    *,
    species_id: int,
    level: int = 10,
    route_image_index: int | None = None,
    course_id: int | None = None,
    nickname: str | None = None,
    friendship: int | None = None,
    held_item: int = 0,
    moves: list[int] | None = None,
    variant_flags: int = 0,
    special_flags: int = 0,
    seed: int | None = None,
    clear_buffers: bool = False,
    allow_locked_course: bool = False,
    assume_national_dex: bool = True,
    unlock_special_courses: bool = False,
    unlock_event_courses: bool = False,
    existing_course_flags: int = 0,
) -> Dict[str, Any]:
    _check_size(eeprom)

    rng_seed = int(seed) if seed is not None else int(time.time_ns() & 0xFFFFFFFF)
    rng = random.Random(rng_seed)

    sid = _validate_u16("speciesId", species_id)
    lvl = max(1, min(int(level), 100))

    unlock_state_before = build_course_unlock_state(
        eeprom,
        existing_flags=existing_course_flags,
        assume_national_dex=assume_national_dex,
        unlock_special_courses=unlock_special_courses,
        unlock_event_courses=unlock_event_courses,
    )
    unlocked_courses = [int(value) for value in unlock_state_before.get("unlockedCourses", [])]

    requested_course: int | None = None
    if course_id is not None:
        requested_course = int(course_id)
    elif route_image_index is not None:
        requested_course = int(route_image_index)

    selected_course: int
    course_note: str | None = None

    if requested_course is not None:
        if requested_course < 0 or requested_course >= len(COURSE_NAMES):
            raise ValueError(f"courseId out of range (0..{len(COURSE_NAMES) - 1}): {requested_course}")

        if requested_course not in unlocked_courses and not allow_locked_course:
            rule = _course_unlock_rule(requested_course)
            required_watts = rule.get("watts")
            special_requirement = rule.get("special")
            if required_watts is not None:
                raise ValueError(
                    (
                        f"Requested course {requested_course} ({COURSE_NAMES[requested_course]}) "
                        f"is locked at {unlock_state_before['watts']} watts; requires {required_watts} watts"
                    )
                )
            raise ValueError(
                (
                    f"Requested course {requested_course} ({COURSE_NAMES[requested_course]}) "
                    f"requires special unlock condition: {special_requirement}"
                )
            )

        selected_course = requested_course
        if course_id is not None and route_image_index is not None and int(course_id) != int(route_image_index):
            course_note = (
                "Both courseId and routeImageIndex were provided; courseId was used for route configuration"
            )
    else:
        current_course = int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])
        if current_course in unlocked_courses:
            selected_course = current_course
        elif unlocked_courses:
            selected_course = unlocked_courses[-1]
            course_note = (
                f"Current route {current_course} is locked at {unlock_state_before['watts']} watts; "
                f"auto-selected highest unlocked route {selected_course}"
            )
        else:
            selected_course = 0
            course_note = "No unlocked courses detected; defaulted to route 0"

    route_cfg: list[dict[str, Any]] = _configure_route_from_course(eeprom, selected_course, rng=rng)
    route_image_index = selected_course
    eeprom[ROUTE_IMAGE_INDEX_OFFSET] = _validate_u8("routeImageIndex", route_image_index)

    if friendship is None:
        friendship = _species_base_friendship(sid)
    friendship_u8 = _validate_u8("friendship", friendship)

    summary = _pokemon_summary_from_values(
        sid,
        level=lvl,
        held_item=held_item,
        moves=moves,
        variant_flags=variant_flags,
        special_flags=special_flags,
    )
    _write_pokemon_summary(eeprom, ROUTE_INFO_OFFSET, summary)

    display_name = (nickname or _species_name(sid)).strip() or _species_name(sid)
    _write_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS, display_name)
    eeprom[ROUTE_FRIENDSHIP_OFFSET] = friendship_u8

    team_base = TEAM_POKES_OFFSET
    write_u16_le(eeprom, team_base + TEAM_POKE_SPECIES_OFFSET, sid)
    eeprom[team_base + TEAM_POKE_LEVEL_OFFSET] = lvl
    _write_device_text_fixed(eeprom, team_base + TEAM_POKE_NICKNAME_OFFSET, TEAM_POKE_NICKNAME_CHARS, display_name)

    if clear_buffers:
        clear_stroll_buffers(
            eeprom,
            clear_caught=True,
            clear_dowsed=True,
            clear_gifted=False,
            clear_journal_entries=False,
        )

    journal_entry = append_journal_event(
        eeprom,
        event_type=EVENT_TYPE_STROLL_DEPART,
        walking_species=sid,
        route_image_idx=route_image_index,
        friendship=friendship_u8,
        companion_name=display_name,
    )

    unlock_state_after = build_course_unlock_state(
        eeprom,
        existing_flags=existing_course_flags,
        assume_national_dex=assume_national_dex,
        unlock_special_courses=unlock_special_courses,
        unlock_event_courses=unlock_event_courses,
    )

    return {
        "seed": rng_seed,
        "walkingPokemon": {
            "speciesId": sid,
            "speciesName": _species_name(sid),
            "level": lvl,
            "friendship": friendship_u8,
            "nickname": display_name,
            "routeImageIndex": int(eeprom[ROUTE_IMAGE_INDEX_OFFSET]),
            "routeCourseName": COURSE_NAMES[int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])]
            if int(eeprom[ROUTE_IMAGE_INDEX_OFFSET]) < len(COURSE_NAMES)
            else None,
        },
        "courseSelection": {
            "requestedCourseId": requested_course,
            "selectedCourseId": int(eeprom[ROUTE_IMAGE_INDEX_OFFSET]),
            "selectedCourseName": COURSE_NAMES[int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])],
            "allowLockedCourse": bool(allow_locked_course),
            "autoSelected": requested_course is None or int(eeprom[ROUTE_IMAGE_INDEX_OFFSET]) != requested_course,
            "note": course_note,
        },
        "courseUnlocks": {
            "before": unlock_state_before,
            "after": unlock_state_after,
        },
        "configuredRouteSlots": route_cfg,
        "stroll": read_stroll_section(eeprom),
        "routes": read_routes_section(eeprom),
        "journalEntry": journal_entry,
        "consoleLog": [
            f"[DEPART] {_species_name(sid)} Lv{lvl} sent to route {int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])}",
        ],
    }


def return_pokemon_from_stroll(
    eeprom: bytearray,
    *,
    walked_steps: int,
    gained_exp: int | None = None,
    bonus_watts: int = 0,
    capture_species_ids: list[int] | None = None,
    auto_captures: int = 0,
    seed: int | None = None,
    replace_when_full: bool = False,
    clear_caught_after_return: bool = False,
    assume_national_dex: bool = True,
    unlock_special_courses: bool = False,
    unlock_event_courses: bool = False,
    existing_course_flags: int = 0,
) -> Dict[str, Any]:
    _check_size(eeprom)

    rng_seed = int(seed) if seed is not None else int(time.time_ns() & 0xFFFFFFFF)
    rng = random.Random(rng_seed)

    added_steps = _validate_u32("walkedSteps", walked_steps)
    bonus_watts_u16 = _validate_u16("bonusWatts", bonus_watts)

    walking = _read_pokemon_summary(eeprom, ROUTE_INFO_OFFSET)
    walking_species = int(walking["speciesId"])
    if walking_species == 0:
        raise ValueError("No walking Pokemon in EEPROM route info. Send one first.")

    start_level = max(1, int(walking["level"] or 1))
    route_index = int(eeprom[ROUTE_IMAGE_INDEX_OFFSET])
    friendship_before = int(eeprom[ROUTE_FRIENDSHIP_OFFSET])
    companion_name = _read_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS)

    unlock_state_before = build_course_unlock_state(
        eeprom,
        existing_flags=existing_course_flags,
        assume_national_dex=assume_national_dex,
        unlock_special_courses=unlock_special_courses,
        unlock_event_courses=unlock_event_courses,
    )

    step_result = add_walked_steps(eeprom, added_steps)
    if bonus_watts_u16 > 0:
        set_watts(eeprom, min(0xFFFF, read_current_watts(eeprom) + bonus_watts_u16))

    unlock_state_after = build_course_unlock_state(
        eeprom,
        existing_flags=existing_course_flags,
        assume_national_dex=assume_national_dex,
        unlock_special_courses=unlock_special_courses,
        unlock_event_courses=unlock_event_courses,
    )
    unlocked_before = {
        int(course_id)
        for course_id in unlock_state_before.get("unlockedCourses", [])
        if isinstance(course_id, int)
    }
    unlocked_after = {
        int(course_id)
        for course_id in unlock_state_after.get("unlockedCourses", [])
        if isinstance(course_id, int)
    }
    newly_unlocked = sorted(unlocked_after - unlocked_before)

    capture_requests: list[dict[str, Any]] = []
    if capture_species_ids is not None:
        for raw_species in capture_species_ids:
            sid = _validate_u16("captureSpeciesId", int(raw_species))
            level_hint = start_level
            for route_slot in _read_route_slots(eeprom):
                route_species = int(route_slot["pokemon"]["speciesId"])
                if route_species == sid:
                    level_hint = max(1, int(route_slot["pokemon"]["level"] or start_level))
                    break
            capture_requests.append(
                _pokemon_summary_from_values(
                    sid,
                    level=level_hint,
                )
            )

    auto_count = max(0, int(auto_captures))
    for _ in range(auto_count):
        rolled = _roll_route_slot_for_capture(eeprom, walked_steps=added_steps, rng=rng)
        if rolled is None:
            break
        capture_summary = dict(rolled["pokemon"])
        if int(capture_summary.get("level", 0)) <= 0:
            capture_summary["level"] = start_level
        capture_requests.append(capture_summary)

    captures_applied: list[dict[str, Any]] = []
    captures_dropped: list[dict[str, Any]] = []

    for index, capture in enumerate(capture_requests):
        species_id = _validate_u16("captureSpeciesId", int(capture.get("speciesId", 0)))
        if species_id == 0:
            continue

        if int(capture.get("level", 0)) <= 0:
            capture["level"] = start_level

        replace_slot: int | None = None
        if replace_when_full and _find_empty_species_slot(eeprom) is None:
            replace_slot = index % INVENTORY_CAUGHT_SLOTS

        try:
            placement = add_inventory_caught_summary(eeprom, capture, replace_slot=replace_slot)
            stats = _generate_pokemon_stats(species_id, int(capture["level"]), rng)
            applied_entry = {
                **placement,
                "stats": stats,
            }
            captures_applied.append(applied_entry)
            append_journal_event(
                eeprom,
                event_type=EVENT_TYPE_STROLL_CAPTURE,
                walking_species=walking_species,
                caught_species=species_id,
                route_image_idx=route_index,
                friendship=friendship_before,
                step_count=added_steps,
                watts=0,
                companion_name=companion_name,
            )
        except ValueError:
            captures_dropped.append(
                {
                    "speciesId": species_id,
                    "speciesName": _species_name(species_id),
                    "level": int(capture.get("level", start_level)),
                    "reason": "no-empty-slot",
                }
            )

    if gained_exp is None:
        exp_gain_requested = added_steps
    else:
        exp_gain_requested = _validate_u32("gainedExp", gained_exp)

    growth = _species_growth_rate(walking_species)
    start_exp = _exp_threshold(start_level, growth)
    if start_level >= 100:
        exp_cap = start_exp
    else:
        exp_cap = _exp_threshold(start_level + 1, growth)
    exp_gain_applied = min(exp_gain_requested, max(0, exp_cap - start_exp))
    end_exp = start_exp + exp_gain_applied
    end_level = _level_from_exp(end_exp, growth)

    friendship_after = min(0xFF, friendship_before + added_steps)

    for team_slot in range(TEAM_POKE_COUNT):
        team_base = TEAM_POKES_OFFSET + team_slot * TEAM_POKE_ENTRY_SIZE
        if read_u16_le(eeprom, team_base + TEAM_POKE_SPECIES_OFFSET) == walking_species:
            eeprom[team_base + TEAM_POKE_LEVEL_OFFSET] = end_level
            break

    total_watts_gain = int(step_result["gainedWatts"]) + bonus_watts_u16
    return_event = append_journal_event(
        eeprom,
        event_type=EVENT_TYPE_STROLL_RETURN,
        walking_species=walking_species,
        caught_species=0,
        route_image_idx=route_index,
        friendship=friendship_after,
        step_count=added_steps,
        watts=total_watts_gain,
        extra_data=len(captures_applied),
        companion_name=companion_name,
    )

    # Companion leaves the device after returning to HGSS.
    eeprom[ROUTE_INFO_OFFSET : ROUTE_INFO_OFFSET + POKE_SUMMARY_SIZE] = b"\x00" * POKE_SUMMARY_SIZE
    _write_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS, "")
    eeprom[ROUTE_FRIENDSHIP_OFFSET] = 0

    for team_slot in range(TEAM_POKE_COUNT):
        team_base = TEAM_POKES_OFFSET + team_slot * TEAM_POKE_ENTRY_SIZE
        if read_u16_le(eeprom, team_base + TEAM_POKE_SPECIES_OFFSET) == walking_species:
            write_u16_le(eeprom, team_base + TEAM_POKE_SPECIES_OFFSET, 0)
            eeprom[team_base + TEAM_POKE_LEVEL_OFFSET] = 0
            _write_device_text_fixed(
                eeprom,
                team_base + TEAM_POKE_NICKNAME_OFFSET,
                TEAM_POKE_NICKNAME_CHARS,
                "",
            )
            break

    if clear_caught_after_return:
        start = INVENTORY_CAUGHT_OFFSET
        end = start + INVENTORY_CAUGHT_SLOTS * INVENTORY_CAUGHT_ENTRY_SIZE
        eeprom[start:end] = b"\x00" * (end - start)

    console_log = [
        (
            f"[RETURN] {_species_name(walking_species)} Lv{start_level} -> Lv{end_level} "
            f"(+{exp_gain_applied} EXP, +{added_steps} steps, +{total_watts_gain} watts)"
        )
    ]
    for capture in captures_applied:
        stats = capture["stats"]["stats"]
        console_log.append(
            (
                f"[CAPTURE] slot {capture['slot']}: {capture['speciesName']} Lv{capture['level']} "
                f"(HP {stats['hp']}, ATK {stats['atk']}, DEF {stats['def']}, "
                f"SPA {stats['spa']}, SPD {stats['spd']}, SPE {stats['spe']})"
            )
        )
    for dropped in captures_dropped:
        console_log.append(
            f"[CAPTURE-DROPPED] {dropped['speciesName']} Lv{dropped['level']} ({dropped['reason']})"
        )

    return {
        "seed": rng_seed,
        "returnedPokemon": {
            "speciesId": walking_species,
            "speciesName": _species_name(walking_species),
            "startLevel": start_level,
            "endLevel": end_level,
            "growthRate": growth,
            "expGain": exp_gain_applied,
            "expGainRequested": exp_gain_requested,
            "expGainApplied": exp_gain_applied,
            "expCap": exp_cap,
            "startExp": start_exp,
            "endExp": end_exp,
            "friendshipBefore": friendship_before,
            "friendshipAfter": friendship_after,
        },
        "steps": {
            **step_result,
            "inputWalkedSteps": added_steps,
        },
        "watts": {
            "fromSteps": int(step_result["gainedWatts"]),
            "bonus": bonus_watts_u16,
            "totalGained": total_watts_gain,
            "current": read_current_watts(eeprom),
        },
        "courseUnlocks": {
            "before": unlock_state_before,
            "after": unlock_state_after,
            "newlyUnlockedCourses": newly_unlocked,
            "newlyUnlockedCourseNames": [COURSE_NAMES[course_id] for course_id in newly_unlocked],
        },
        "captures": {
            "requested": len(capture_requests),
            "applied": captures_applied,
            "dropped": captures_dropped,
            "clearCaughtAfterReturn": bool(clear_caught_after_return),
        },
        "journal": {
            "returnEntry": return_event,
        },
        "snapshot": read_snapshot(eeprom),
        "stroll": read_stroll_section(eeprom),
        "inventory": read_inventory_section(eeprom),
        "consoleLog": console_log,
    }


def read_journal_section(eeprom: bytearray, preview_entries: int = 6) -> Dict[str, Any]:
    _check_size(eeprom)
    limit = max(0, min(int(preview_entries), EVENT_LOG_ENTRIES))

    entries = []
    non_empty = 0
    for index in range(EVENT_LOG_ENTRIES):
        base = EVENT_LOG_OFFSET + index * EVENT_LOG_ENTRY_SIZE
        event_time = read_u32_be(eeprom, base)
        event_type = read_u16_le(eeprom, base + EVENT_LOG_TYPE_OFFSET)
        step_count = read_u32_be(eeprom, base + EVENT_LOG_STEP_COUNT_OFFSET)
        watts = read_u16_be(eeprom, base + EVENT_LOG_WATTS_OFFSET)
        remote_watts = read_u16_be(eeprom, base + EVENT_LOG_REMOTE_WATTS_OFFSET)

        if event_time or event_type or step_count or watts or remote_watts:
            non_empty += 1

        if index < limit:
            entries.append(
                {
                    "index": index,
                    "eventTime": event_time,
                    "eventType": event_type,
                    "walkingSpecies": read_u16_le(eeprom, base + EVENT_LOG_WALKING_SPECIES_OFFSET),
                    "caughtSpecies": read_u16_le(eeprom, base + EVENT_LOG_CAUGHT_SPECIES_OFFSET),
                    "stepCount": step_count,
                    "watts": watts,
                    "remoteWatts": remote_watts,
                }
            )

    return {
        "entryCount": EVENT_LOG_ENTRIES,
        "entrySize": EVENT_LOG_ENTRY_SIZE,
        "nonEmptyEntries": non_empty,
        "preview": entries,
    }


def _read_event_log_entry(eeprom: bytearray, index: int) -> dict[str, Any]:
    if index < 0 or index >= EVENT_LOG_ENTRIES:
        raise ValueError(f"event log index out of range (0..{EVENT_LOG_ENTRIES - 1}): {index}")

    base = EVENT_LOG_OFFSET + index * EVENT_LOG_ENTRY_SIZE
    event_time = read_u32_be(eeprom, base)
    return {
        "index": index,
        "eventTime": event_time,
        "unk0": read_u32_be(eeprom, base + 4),
        "unk2": read_u16_be(eeprom, base + 8),
        "walkingSpecies": read_u16_le(eeprom, base + EVENT_LOG_WALKING_SPECIES_OFFSET),
        "caughtSpecies": read_u16_le(eeprom, base + EVENT_LOG_CAUGHT_SPECIES_OFFSET),
        "extraData": read_u16_le(eeprom, base + EVENT_LOG_EXTRA_DATA_OFFSET),
        "remoteTrainerName": _read_device_text_fixed(eeprom, base + 16, 8),
        "pokeNick": _read_device_text_fixed(eeprom, base + 32, 11),
        "remotePokeNick": _read_device_text_fixed(eeprom, base + 54, 11),
        "routeImageIndex": eeprom[base + EVENT_LOG_ROUTE_IMAGE_OFFSET],
        "friendship": eeprom[base + EVENT_LOG_FRIENDSHIP_OFFSET],
        "watts": read_u16_be(eeprom, base + EVENT_LOG_WATTS_OFFSET),
        "remoteWatts": read_u16_be(eeprom, base + EVENT_LOG_REMOTE_WATTS_OFFSET),
        "stepCount": read_u32_be(eeprom, base + EVENT_LOG_STEP_COUNT_OFFSET),
        "remoteStepCount": read_u32_be(eeprom, base + EVENT_LOG_REMOTE_STEP_COUNT_OFFSET),
        "eventType": read_u16_le(eeprom, base + EVENT_LOG_TYPE_OFFSET),
        "genderAndForm": eeprom[base + EVENT_LOG_GENDER_FORM_OFFSET],
        "caughtGenderAndForm": eeprom[base + EVENT_LOG_CAUGHT_GENDER_FORM_OFFSET],
    }


def _write_event_log_entry(eeprom: bytearray, index: int, entry: dict[str, Any]) -> None:
    if index < 0 or index >= EVENT_LOG_ENTRIES:
        raise ValueError(f"event log index out of range (0..{EVENT_LOG_ENTRIES - 1}): {index}")

    base = EVENT_LOG_OFFSET + index * EVENT_LOG_ENTRY_SIZE
    eeprom[base : base + EVENT_LOG_ENTRY_SIZE] = b"\x00" * EVENT_LOG_ENTRY_SIZE

    write_u32_be(eeprom, base, _validate_u32("eventTime", int(entry.get("eventTime", 0))))
    write_u32_be(eeprom, base + 4, _validate_u32("unk0", int(entry.get("unk0", 0))))
    write_u16_be(eeprom, base + 8, _validate_u16("unk2", int(entry.get("unk2", 0))))
    write_u16_le(
        eeprom,
        base + EVENT_LOG_WALKING_SPECIES_OFFSET,
        _validate_u16("walkingSpecies", int(entry.get("walkingSpecies", 0))),
    )
    write_u16_le(
        eeprom,
        base + EVENT_LOG_CAUGHT_SPECIES_OFFSET,
        _validate_u16("caughtSpecies", int(entry.get("caughtSpecies", 0))),
    )
    write_u16_le(
        eeprom,
        base + EVENT_LOG_EXTRA_DATA_OFFSET,
        _validate_u16("extraData", int(entry.get("extraData", 0))),
    )

    _write_device_text_fixed(eeprom, base + 16, 8, str(entry.get("remoteTrainerName", "")))
    _write_device_text_fixed(eeprom, base + 32, 11, str(entry.get("pokeNick", "")))
    _write_device_text_fixed(eeprom, base + 54, 11, str(entry.get("remotePokeNick", "")))

    eeprom[base + EVENT_LOG_ROUTE_IMAGE_OFFSET] = _validate_u8(
        "routeImageIndex", int(entry.get("routeImageIndex", 0))
    )
    eeprom[base + EVENT_LOG_FRIENDSHIP_OFFSET] = _validate_u8(
        "friendship", int(entry.get("friendship", 0))
    )

    write_u16_be(eeprom, base + EVENT_LOG_WATTS_OFFSET, _validate_u16("watts", int(entry.get("watts", 0))))
    write_u16_be(
        eeprom,
        base + EVENT_LOG_REMOTE_WATTS_OFFSET,
        _validate_u16("remoteWatts", int(entry.get("remoteWatts", 0))),
    )
    write_u32_be(
        eeprom,
        base + EVENT_LOG_STEP_COUNT_OFFSET,
        _validate_u32("stepCount", int(entry.get("stepCount", 0))),
    )
    write_u32_be(
        eeprom,
        base + EVENT_LOG_REMOTE_STEP_COUNT_OFFSET,
        _validate_u32("remoteStepCount", int(entry.get("remoteStepCount", 0))),
    )
    write_u16_le(
        eeprom,
        base + EVENT_LOG_TYPE_OFFSET,
        _validate_u16("eventType", int(entry.get("eventType", 0))),
    )
    eeprom[base + EVENT_LOG_GENDER_FORM_OFFSET] = _validate_u8(
        "genderAndForm", int(entry.get("genderAndForm", 0))
    )
    eeprom[base + EVENT_LOG_CAUGHT_GENDER_FORM_OFFSET] = _validate_u8(
        "caughtGenderAndForm", int(entry.get("caughtGenderAndForm", 0))
    )


def _append_event_log_entry(eeprom: bytearray, entry: dict[str, Any]) -> dict[str, Any]:
    records = [_read_event_log_entry(eeprom, idx) for idx in range(EVENT_LOG_ENTRIES)]
    non_empty = [record for record in records if record["eventTime"] != 0]

    updated = [dict(entry)]
    updated.extend(non_empty[: EVENT_LOG_ENTRIES - 1])

    for idx in range(EVENT_LOG_ENTRIES):
        if idx < len(updated):
            _write_event_log_entry(eeprom, idx, updated[idx])
        else:
            base = EVENT_LOG_OFFSET + idx * EVENT_LOG_ENTRY_SIZE
            eeprom[base : base + EVENT_LOG_ENTRY_SIZE] = b"\x00" * EVENT_LOG_ENTRY_SIZE

    return _read_event_log_entry(eeprom, 0)


def clear_journal(eeprom: bytearray) -> None:
    _check_size(eeprom)
    end = EVENT_LOG_OFFSET + EVENT_LOG_ENTRY_SIZE * EVENT_LOG_ENTRIES
    eeprom[EVENT_LOG_OFFSET:end] = b"\x00" * (end - EVENT_LOG_OFFSET)


def clear_stroll_buffers(
    eeprom: bytearray,
    *,
    clear_caught: bool = True,
    clear_dowsed: bool = True,
    clear_gifted: bool = False,
    clear_journal_entries: bool = False,
) -> Dict[str, bool]:
    _check_size(eeprom)

    if clear_caught:
        start = INVENTORY_CAUGHT_OFFSET
        end = start + INVENTORY_CAUGHT_SLOTS * INVENTORY_CAUGHT_ENTRY_SIZE
        eeprom[start:end] = b"\x00" * (end - start)

    if clear_dowsed:
        start = INVENTORY_DOWSED_OFFSET
        end = start + INVENTORY_DOWSED_SLOTS * INVENTORY_ITEM_ENTRY_SIZE
        eeprom[start:end] = b"\x00" * (end - start)

    if clear_gifted:
        start = INVENTORY_GIFTED_OFFSET
        end = start + INVENTORY_GIFTED_SLOTS * INVENTORY_ITEM_ENTRY_SIZE
        eeprom[start:end] = b"\x00" * (end - start)

    if clear_journal_entries:
        clear_journal(eeprom)

    return {
        "clearedCaught": clear_caught,
        "clearedDowsed": clear_dowsed,
        "clearedGifted": clear_gifted,
        "clearedJournal": clear_journal_entries,
    }


def _stroll_status(eeprom: bytearray) -> dict[str, Any]:
    walking = _read_pokemon_summary(eeprom, ROUTE_INFO_OFFSET)
    route_index = eeprom[ROUTE_IMAGE_INDEX_OFFSET]
    team_levels = []
    for slot in range(TEAM_POKE_COUNT):
        offset = TEAM_POKES_OFFSET + slot * TEAM_POKE_ENTRY_SIZE
        species_id = read_u16_le(eeprom, offset + TEAM_POKE_SPECIES_OFFSET)
        if species_id == 0:
            continue
        team_levels.append(eeprom[offset + TEAM_POKE_LEVEL_OFFSET])

    return {
        "hasWalkingCompanion": walking["speciesId"] != 0,
        "walkingCompanion": walking,
        "walkingNickname": _read_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS),
        "walkingFriendship": eeprom[ROUTE_FRIENDSHIP_OFFSET],
        "routeImageIndex": route_index,
        "routeCourseName": COURSE_NAMES[route_index] if 0 <= route_index < len(COURSE_NAMES) else None,
        "routeName": _read_device_text_fixed(eeprom, ROUTE_NAME_OFFSET, ROUTE_NAME_CHARS),
        "routeSlots": _read_route_slots(eeprom),
        "teamLevels": team_levels,
        "inventory": read_inventory_section(eeprom),
    }


def start_stroll(
    eeprom: bytearray,
    *,
    team_slot: int = 0,
    course_id: int,
    nickname: str | None = None,
    clear_buffers: bool = True,
    seed: int | None = None,
) -> dict[str, Any]:
    _check_size(eeprom)
    if team_slot < 0 or team_slot >= TEAM_POKE_COUNT:
        raise ValueError(f"teamSlot out of range (0..{TEAM_POKE_COUNT - 1}): {team_slot}")
    if course_id < 0 or course_id >= len(COURSE_NAMES):
        raise ValueError(f"courseId out of range (0..{len(COURSE_NAMES) - 1}): {course_id}")

    if _read_pokemon_summary(eeprom, ROUTE_INFO_OFFSET)["speciesId"] != 0:
        raise ValueError("A walking Pokemon is already active in ROUTE_INFO")

    team_offset = TEAM_POKES_OFFSET + team_slot * TEAM_POKE_ENTRY_SIZE
    species_id = read_u16_le(eeprom, team_offset + TEAM_POKE_SPECIES_OFFSET)
    if species_id == 0:
        raise ValueError(f"Team slot {team_slot} is empty in EEPROM team data")

    level = max(1, min(eeprom[team_offset + TEAM_POKE_LEVEL_OFFSET], 100))
    moves = [
        read_u16_le(eeprom, team_offset + 4 + move_idx * 2)
        for move_idx in range(4)
    ]
    base_friendship = _species_base_friendship(species_id)

    walker_nickname = (
        nickname
        if nickname is not None
        else _read_device_text_fixed(eeprom, team_offset + TEAM_POKE_NICKNAME_OFFSET, TEAM_POKE_NICKNAME_CHARS)
    )

    rng_seed = int(seed) if seed is not None else int(time.time())
    rng = random.Random(rng_seed)
    configured_slots = _configure_route_from_course(eeprom, course_id, rng=rng)

    walking_summary = _pokemon_summary_from_values(
        species_id,
        level=level,
        moves=moves,
    )
    _write_pokemon_summary(eeprom, ROUTE_INFO_OFFSET, walking_summary)
    _write_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS, walker_nickname or "")
    eeprom[ROUTE_FRIENDSHIP_OFFSET] = _validate_u8("friendship", base_friendship)

    clear_result: dict[str, bool] | None = None
    if clear_buffers:
        clear_result = clear_stroll_buffers(
            eeprom,
            clear_caught=True,
            clear_dowsed=True,
            clear_gifted=False,
            clear_journal_entries=False,
        )

    now = int(time.time())
    log_entry = _append_event_log_entry(
        eeprom,
        {
            "eventTime": now,
            "eventType": EVENT_TYPE_STROLL_DEPART,
            "walkingSpecies": species_id,
            "caughtSpecies": 0,
            "routeImageIndex": course_id,
            "friendship": base_friendship,
            "stepCount": read_steps(eeprom),
            "watts": read_current_watts(eeprom),
            "pokeNick": walker_nickname or "",
        },
    )

    return {
        "status": "ok",
        "action": "stroll-start",
        "teamSlot": team_slot,
        "courseId": course_id,
        "courseName": COURSE_NAMES[course_id],
        "seed": rng_seed,
        "walkingCompanion": _read_pokemon_summary(eeprom, ROUTE_INFO_OFFSET),
        "walkingNickname": _read_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS),
        "walkingFriendship": eeprom[ROUTE_FRIENDSHIP_OFFSET],
        "routeSlots": configured_slots,
        "buffers": clear_result,
        "journalEntry": log_entry,
    }


def _roll_capture_for_slot(
    eeprom: bytearray,
    slot: int,
    *,
    steps_delta: int,
    rng: random.Random,
) -> dict[str, Any] | None:
    route_slot = _read_route_slot(eeprom, slot)
    summary = route_slot["pokemon"]
    species_id = int(summary["speciesId"])
    if species_id == 0:
        return None

    min_steps = int(route_slot["minSteps"])
    chance = max(0, min(int(route_slot["chance"]), 100))
    if steps_delta < min_steps:
        return None
    if chance <= 0:
        return None
    if rng.randrange(100) >= chance:
        return None

    placement = add_inventory_caught_summary(eeprom, summary)
    slot_stats = _generate_pokemon_stats(species_id, int(summary["level"]), rng)
    return {
        "routeSlot": slot,
        "minSteps": min_steps,
        "chance": chance,
        "placement": placement,
        "pokemon": {
            "speciesId": species_id,
            "speciesName": _species_name(species_id),
            "level": int(summary["level"]),
            "heldItem": int(summary["heldItem"]),
            "moves": list(summary["moves"]),
            "variantFlags": int(summary["variantFlags"]),
            "specialFlags": int(summary["specialFlags"]),
        },
        "stats": slot_stats,
    }


def _roll_dowsed_items(
    eeprom: bytearray,
    *,
    steps_delta: int,
    rng: random.Random,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for item_index in range(10):
        item_id = read_u16_le(eeprom, ROUTE_INFO_OFFSET + 140 + item_index * 2)
        if item_id == 0:
            continue
        min_steps = read_u16_le(eeprom, ROUTE_INFO_OFFSET + 160 + item_index * 2)
        chance = eeprom[ROUTE_INFO_OFFSET + 180 + item_index]
        if steps_delta < min_steps:
            continue
        if chance <= 0:
            continue
        if rng.randrange(100) >= chance:
            continue
        try:
            placement = add_inventory_dowsed_item(eeprom, item_id)
        except ValueError:
            break
        results.append(
            {
                "routeItemIndex": item_index,
                "itemId": item_id,
                "minSteps": min_steps,
                "chance": chance,
                "placement": placement,
            }
        )
        if len(results) >= INVENTORY_DOWSED_SLOTS:
            break
    return results


def return_from_stroll(
    eeprom: bytearray,
    *,
    walked_steps: int,
    exp_steps: int | None = None,
    force_capture_slots: list[int] | None = None,
    force_dowsed_item_ids: list[int] | None = None,
    clear_walking_companion: bool = True,
    seed: int | None = None,
) -> dict[str, Any]:
    _check_size(eeprom)
    walking = _read_pokemon_summary(eeprom, ROUTE_INFO_OFFSET)
    species_id = int(walking["speciesId"])
    if species_id == 0:
        raise ValueError("No active walking Pokemon in ROUTE_INFO")

    added_steps = _validate_u32("walkedSteps", walked_steps)
    exp_source_steps = added_steps if exp_steps is None else _validate_u32("expSteps", exp_steps)
    route_index = eeprom[ROUTE_IMAGE_INDEX_OFFSET]
    friendship_before = int(eeprom[ROUTE_FRIENDSHIP_OFFSET])
    walker_nickname = _read_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS)

    step_result = add_walked_steps(eeprom, added_steps)

    growth = _species_growth_rate(species_id)
    start_level = max(1, min(int(walking["level"]), 100))
    start_exp = _exp_threshold(start_level, growth)
    if start_level >= 100:
        exp_cap = start_exp
    else:
        exp_cap = _exp_threshold(start_level + 1, growth)
    gained_exp_requested = int(exp_source_steps)
    gained_exp = min(gained_exp_requested, max(0, exp_cap - start_exp))
    final_exp = start_exp + gained_exp
    final_level = _level_from_exp(final_exp, growth)
    friendship_after = min(0xFF, friendship_before + added_steps)
    eeprom[ROUTE_FRIENDSHIP_OFFSET] = friendship_after

    rng_seed = int(seed) if seed is not None else int(time.time())
    rng = random.Random(rng_seed)

    captures: list[dict[str, Any]] = []
    if force_capture_slots:
        for slot in force_capture_slots:
            slot_value = int(slot)
            if slot_value < 0 or slot_value >= ROUTE_POKE_SLOTS:
                raise ValueError(f"force capture slot out of range (0..{ROUTE_POKE_SLOTS - 1}): {slot}")
            capture = _roll_capture_for_slot(
                eeprom,
                slot_value,
                steps_delta=10 ** 9,
                rng=rng,
            )
            if capture is not None:
                captures.append(capture)
    else:
        for slot in range(ROUTE_POKE_SLOTS):
            capture = _roll_capture_for_slot(
                eeprom,
                slot,
                steps_delta=added_steps,
                rng=rng,
            )
            if capture is not None:
                captures.append(capture)

    dowsed_items: list[dict[str, Any]] = []
    if force_dowsed_item_ids:
        for item_id in force_dowsed_item_ids:
            placement = add_inventory_dowsed_item(eeprom, int(item_id))
            dowsed_items.append(
                {
                    "routeItemIndex": None,
                    "itemId": int(item_id),
                    "minSteps": 0,
                    "chance": 100,
                    "placement": placement,
                }
            )
            if len(dowsed_items) >= INVENTORY_DOWSED_SLOTS:
                break
    else:
        dowsed_items = _roll_dowsed_items(eeprom, steps_delta=added_steps, rng=rng)

    now = int(time.time())
    log_entries: list[dict[str, Any]] = []
    for capture in captures:
        log_entries.append(
            _append_event_log_entry(
                eeprom,
                {
                    "eventTime": now,
                    "eventType": EVENT_TYPE_STROLL_CAPTURE,
                    "walkingSpecies": species_id,
                    "caughtSpecies": int(capture["pokemon"]["speciesId"]),
                    "routeImageIndex": route_index,
                    "friendship": friendship_after,
                    "stepCount": read_steps(eeprom),
                    "watts": read_current_watts(eeprom),
                    "pokeNick": walker_nickname,
                    "caughtGenderAndForm": int(capture["pokemon"]["variantFlags"]),
                },
            )
        )

    log_entries.append(
        _append_event_log_entry(
            eeprom,
            {
                "eventTime": now,
                "eventType": EVENT_TYPE_STROLL_RETURN,
                "walkingSpecies": species_id,
                "caughtSpecies": 0,
                "routeImageIndex": route_index,
                "friendship": friendship_after,
                "stepCount": read_steps(eeprom),
                "watts": read_current_watts(eeprom),
                "pokeNick": walker_nickname,
                "extraData": max(0, min(final_level - start_level, 0xFFFF)),
            },
        )
    )

    if clear_walking_companion:
        _write_pokemon_summary(eeprom, ROUTE_INFO_OFFSET, _pokemon_summary_from_values(0, level=0))
        _write_device_text_fixed(eeprom, ROUTE_NICKNAME_OFFSET, ROUTE_NICKNAME_CHARS, "")
        eeprom[ROUTE_FRIENDSHIP_OFFSET] = 0

    returned_stats = _generate_pokemon_stats(species_id, final_level, rng)
    return {
        "status": "ok",
        "action": "stroll-return",
        "seed": rng_seed,
        "walkingPokemon": {
            "speciesId": species_id,
            "speciesName": _species_name(species_id),
            "nickname": walker_nickname,
            "friendshipBefore": friendship_before,
            "friendshipAfter": friendship_after,
            "startLevel": start_level,
            "finalLevel": final_level,
            "expGained": gained_exp,
            "expGainedRequested": gained_exp_requested,
            "expGainedApplied": gained_exp,
            "expCap": exp_cap,
            "startExp": start_exp,
            "finalExp": final_exp,
            "growthRate": growth,
            "stats": returned_stats,
        },
        "steps": step_result,
        "captures": captures,
        "dowsedItems": dowsed_items,
        "journalEntries": log_entries,
        "inventory": read_inventory_section(eeprom),
        "stroll": read_stroll_section(eeprom),
        "statsDomain": read_stats_section(eeprom),
    }


def stroll_report(eeprom: bytearray) -> dict[str, Any]:
    _check_size(eeprom)
    inventory = read_inventory_section(eeprom)
    catches: list[dict[str, Any]] = []
    for caught in inventory["caught"]:
        species_id = int(caught["speciesId"])
        if species_id == 0:
            continue
        stats_seed = (species_id << 16) ^ (int(caught["level"]) << 8) ^ int(caught["slot"])
        rng = random.Random(stats_seed)
        catches.append(
            {
                "slot": caught["slot"],
                "speciesId": species_id,
                "speciesName": caught["speciesName"],
                "level": caught["level"],
                "heldItem": caught["heldItem"],
                "moves": caught["moves"],
                "stats": _generate_pokemon_stats(species_id, int(caught["level"]), rng),
            }
        )

    journal = read_journal_section(eeprom, preview_entries=EVENT_LOG_ENTRIES)
    capture_logs = [
        entry
        for entry in journal["preview"]
        if int(entry.get("eventType", 0)) == EVENT_TYPE_STROLL_CAPTURE
    ]

    return {
        "status": "ok",
        "stroll": _stroll_status(eeprom),
        "captures": catches,
        "captureLogEntries": capture_logs,
        "dowsedItems": inventory["dowsedItems"],
        "giftedItems": inventory["giftedItems"],
        "stats": read_stats_section(eeprom),
    }


def read_routes_section(eeprom: bytearray) -> Dict[str, Any]:
    _check_size(eeprom)
    route_index = eeprom[ROUTE_IMAGE_INDEX_OFFSET]
    unlocks = build_course_unlock_state(eeprom)
    return {
        "routeInfoOffset": ROUTE_INFO_OFFSET,
        "routeInfoSize": ROUTE_INFO_SIZE,
        "walkingSpecies": read_u16_le(eeprom, ROUTE_WALKING_SPECIES_OFFSET),
        "walkingSpeciesName": _species_name(read_u16_le(eeprom, ROUTE_WALKING_SPECIES_OFFSET)),
        "routeImageIndex": route_index,
        "routeCourseName": COURSE_NAMES[route_index] if 0 <= route_index < len(COURSE_NAMES) else None,
        "routeName": _read_device_text_fixed(eeprom, ROUTE_NAME_OFFSET, ROUTE_NAME_CHARS),
        "routeCourseUnlocked": route_index in [int(value) for value in unlocks.get("unlockedCourses", [])],
        "courseUnlocks": unlocks,
        "routeSlots": _read_route_slots(eeprom),
        "routeInfoPreviewHex": eeprom[
            ROUTE_INFO_OFFSET : ROUTE_INFO_OFFSET + min(64, ROUTE_INFO_SIZE)
        ].hex(),
    }


def read_all_domains(eeprom: bytearray) -> Dict[str, Any]:
    _check_size(eeprom)
    return {
        "identity": read_identity_section(eeprom),
        "stats": read_stats_section(eeprom),
        "stroll": read_stroll_section(eeprom),
        "inventory": read_inventory_section(eeprom),
        "journal": read_journal_section(eeprom),
        "routes": read_routes_section(eeprom),
    }


def build_sync_package(eeprom: bytearray) -> Dict[str, Any]:
    _check_size(eeprom)
    return {
        "schema": "wearwalker-sync-v1",
        "generatedAtEpochSeconds": int(time.time()),
        "domains": read_all_domains(eeprom),
    }


def apply_sync_package(eeprom: bytearray, package: Dict[str, Any]) -> Dict[str, Any]:
    _check_size(eeprom)
    if not isinstance(package, dict):
        raise ValueError("sync package must be an object")

    raw_domains = package.get("domains", package)
    if not isinstance(raw_domains, dict):
        raise ValueError("sync package domains must be an object")

    identity = raw_domains.get("identity")
    if isinstance(identity, dict):
        set_identity_section(
            eeprom,
            trainer_name=identity.get("trainerName"),
            protocol_version=identity.get("protocolVersion"),
            protocol_sub_version=identity.get("protocolSubVersion"),
            last_sync_epoch_seconds=identity.get("lastSyncEpochSeconds"),
            step_count=identity.get("identityStepCount", identity.get("stepCount")),
        )

    stats = raw_domains.get("stats")
    if isinstance(stats, dict):
        set_stats_section(
            eeprom,
            steps=stats.get("steps"),
            lifetime_steps=stats.get("lifetimeSteps"),
            today_steps=stats.get("todaySteps"),
            watts=stats.get("watts"),
            last_sync_epoch_seconds=stats.get("lastSyncEpochSeconds", stats.get("lastSyncIdentity")),
            step_history=stats.get("stepHistory"),
        )

    stroll = raw_domains.get("stroll")
    if isinstance(stroll, dict):
        set_stroll_section(
            eeprom,
            session_watts=stroll.get("sessionWatts"),
            route_image_index=stroll.get("routeImageIndex"),
        )

    inventory = raw_domains.get("inventory")
    if isinstance(inventory, dict):
        caught = inventory.get("caught", [])
        if isinstance(caught, list):
            for entry in caught:
                if isinstance(entry, dict) and "slot" in entry and "species" in entry:
                    set_inventory_caught_species(eeprom, int(entry["slot"]), int(entry["species"]))

        dowsed_items = inventory.get("dowsedItems", [])
        if isinstance(dowsed_items, list):
            for entry in dowsed_items:
                if isinstance(entry, dict) and "slot" in entry and "itemId" in entry:
                    set_inventory_dowsed_item(eeprom, int(entry["slot"]), int(entry["itemId"]))

        gifted_items = inventory.get("giftedItems", [])
        if isinstance(gifted_items, list):
            for entry in gifted_items:
                if isinstance(entry, dict) and "slot" in entry and "itemId" in entry:
                    set_inventory_gifted_item(eeprom, int(entry["slot"]), int(entry["itemId"]))

    journal = raw_domains.get("journal")
    if isinstance(journal, dict) and bool(journal.get("clear", False)):
        clear_journal(eeprom)
    if bool(package.get("clearJournal", False)):
        clear_journal(eeprom)

    return read_all_domains(eeprom)


def apply_semantic_operations(eeprom: bytearray, operations: list[Dict[str, Any]]) -> list[Dict[str, Any]]:
    _check_size(eeprom)
    if not isinstance(operations, list) or not operations:
        raise ValueError("operations must be a non-empty list")

    results: list[Dict[str, Any]] = []
    for index, op in enumerate(operations):
        if not isinstance(op, dict):
            raise ValueError(f"operation at index {index} must be an object")

        op_name = op.get("op")
        if not isinstance(op_name, str) or not op_name:
            raise ValueError(f"operation at index {index} missing op name")

        if op_name == "set-steps":
            set_steps(eeprom, int(op.get("steps", 0)))
            op_result: Dict[str, Any] = read_snapshot(eeprom)
        elif op_name == "set-watts":
            set_watts(eeprom, int(op.get("watts", 0)))
            op_result = read_snapshot(eeprom)
        elif op_name == "set-trainer":
            set_trainer_name(eeprom, str(op.get("name", "")))
            op_result = read_snapshot(eeprom)
        elif op_name == "set-sync":
            set_last_sync_seconds(eeprom, int(op.get("epoch", 0)))
            op_result = read_snapshot(eeprom)
        elif op_name == "patch-identity":
            set_identity_section(
                eeprom,
                trainer_name=op.get("trainerName"),
                protocol_version=op.get("protocolVersion"),
                protocol_sub_version=op.get("protocolSubVersion"),
                last_sync_epoch_seconds=op.get("lastSyncEpochSeconds"),
                step_count=op.get("stepCount"),
            )
            op_result = read_identity_section(eeprom)
        elif op_name == "patch-stats":
            set_stats_section(
                eeprom,
                steps=op.get("steps"),
                lifetime_steps=op.get("lifetimeSteps"),
                today_steps=op.get("todaySteps"),
                watts=op.get("watts"),
                last_sync_epoch_seconds=op.get("lastSyncEpochSeconds"),
                step_history=op.get("stepHistory"),
            )
            op_result = read_stats_section(eeprom)
        elif op_name == "patch-stroll":
            set_stroll_section(
                eeprom,
                session_watts=op.get("sessionWatts"),
                route_image_index=op.get("routeImageIndex"),
            )
            op_result = read_stroll_section(eeprom)
        elif op_name == "set-dowsed-item":
            set_inventory_dowsed_item(
                eeprom,
                int(op.get("slot", 0)),
                int(op.get("itemId", 0)),
            )
            op_result = read_inventory_section(eeprom)
        elif op_name == "set-gifted-item":
            set_inventory_gifted_item(
                eeprom,
                int(op.get("slot", 0)),
                int(op.get("itemId", 0)),
            )
            op_result = read_inventory_section(eeprom)
        elif op_name == "set-caught-species":
            set_inventory_caught_species(
                eeprom,
                int(op.get("slot", 0)),
                int(op.get("speciesId", 0)),
            )
            op_result = read_inventory_section(eeprom)
        elif op_name == "add-caught-species":
            op_result = add_inventory_caught_species(
                eeprom,
                int(op.get("speciesId", 0)),
                int(op["replaceSlot"]) if "replaceSlot" in op and op["replaceSlot"] is not None else None,
            )
        elif op_name == "add-dowsed-item":
            op_result = add_inventory_dowsed_item(
                eeprom,
                int(op.get("itemId", 0)),
                int(op["replaceSlot"]) if "replaceSlot" in op and op["replaceSlot"] is not None else None,
            )
        elif op_name == "add-gifted-item":
            op_result = add_inventory_gifted_item(
                eeprom,
                int(op.get("itemId", 0)),
                int(op["replaceSlot"]) if "replaceSlot" in op and op["replaceSlot"] is not None else None,
            )
        elif op_name == "stroll-send":
            if "speciesId" not in op:
                raise ValueError("stroll-send requires speciesId")
            raw_moves = op.get("moves")
            moves = None
            if raw_moves is not None:
                if not isinstance(raw_moves, list):
                    raise ValueError("stroll-send moves must be a list")
                moves = [int(value) for value in raw_moves]
            op_result = send_pokemon_to_stroll(
                eeprom,
                species_id=int(op.get("speciesId", 0)),
                level=int(op.get("level", 10)),
                route_image_index=(
                    int(op["routeImageIndex"])
                    if "routeImageIndex" in op and op["routeImageIndex"] is not None
                    else None
                ),
                course_id=(
                    int(op["courseId"])
                    if "courseId" in op and op["courseId"] is not None
                    else None
                ),
                nickname=str(op["nickname"]) if "nickname" in op and op["nickname"] is not None else None,
                friendship=(
                    int(op["friendship"])
                    if "friendship" in op and op["friendship"] is not None
                    else None
                ),
                held_item=int(op.get("heldItem", 0)),
                moves=moves,
                variant_flags=int(op.get("variantFlags", 0)),
                special_flags=int(op.get("specialFlags", 0)),
                seed=int(op["seed"]) if "seed" in op and op["seed"] is not None else None,
                clear_buffers=bool(op.get("clearBuffers", False)),
            )
        elif op_name == "stroll-start":
            if "courseId" not in op:
                raise ValueError("stroll-start requires courseId")
            op_result = start_stroll(
                eeprom,
                team_slot=int(op.get("teamSlot", 0)),
                course_id=int(op.get("courseId", 0)),
                nickname=str(op["nickname"]) if "nickname" in op and op["nickname"] is not None else None,
                clear_buffers=bool(op.get("clearBuffers", True)),
                seed=int(op["seed"]) if "seed" in op and op["seed"] is not None else None,
            )
        elif op_name == "stroll-return":
            if "walkedSteps" not in op:
                raise ValueError("stroll-return requires walkedSteps")
            raw_capture_ids = op.get("captureSpeciesIds")
            capture_ids = None
            if raw_capture_ids is not None:
                if not isinstance(raw_capture_ids, list):
                    raise ValueError("captureSpeciesIds must be a list")
                capture_ids = [int(value) for value in raw_capture_ids]
            op_result = return_pokemon_from_stroll(
                eeprom,
                walked_steps=int(op.get("walkedSteps", 0)),
                gained_exp=(
                    int(op["gainedExp"])
                    if "gainedExp" in op and op["gainedExp"] is not None
                    else None
                ),
                bonus_watts=int(op.get("bonusWatts", 0)),
                capture_species_ids=capture_ids,
                auto_captures=int(op.get("autoCaptures", 0)),
                seed=int(op["seed"]) if "seed" in op and op["seed"] is not None else None,
                replace_when_full=bool(op.get("replaceWhenFull", False)),
                clear_caught_after_return=bool(op.get("clearCaughtAfterReturn", False)),
            )
        elif op_name == "stroll-report":
            op_result = stroll_report(eeprom)
        elif op_name == "stroll-tick":
            op_result = add_walked_steps(eeprom, int(op.get("steps", 0)))
        elif op_name == "clear-journal":
            clear_journal(eeprom)
            op_result = read_journal_section(eeprom)
        elif op_name == "clear-stroll-buffers":
            op_result = clear_stroll_buffers(
                eeprom,
                clear_caught=bool(op.get("clearCaught", True)),
                clear_dowsed=bool(op.get("clearDowsed", True)),
                clear_gifted=bool(op.get("clearGifted", False)),
                clear_journal_entries=bool(op.get("clearJournal", False)),
            )
        elif op_name == "apply-sync-package":
            package = op.get("package")
            if not isinstance(package, dict):
                raise ValueError("apply-sync-package requires package object")
            op_result = apply_sync_package(eeprom, package)
        else:
            raise ValueError(f"unknown operation: {op_name}")

        results.append({"index": index, "op": op_name, "result": op_result})

    return results


def create_blank_eeprom(trainer_name: str = DEFAULT_TRAINER) -> bytearray:
    eeprom = bytearray(EEPROM_SIZE)
    eeprom[SIGNATURE_OFFSET : SIGNATURE_OFFSET + len(SIGNATURE)] = SIGNATURE
    eeprom[IDENTITY_PROTOCOL_VERSION_OFFSET] = 1
    eeprom[IDENTITY_PROTOCOL_SUB_VERSION_OFFSET] = 0

    set_trainer_name(eeprom, trainer_name)
    set_steps(eeprom, 0)
    set_watts(eeprom, 0)
    set_last_sync_seconds(eeprom, 0)
    return eeprom


def load_eeprom(path: str | Path) -> bytearray:
    eeprom_path = Path(path)
    if not eeprom_path.exists():
        eeprom = create_blank_eeprom()
        save_eeprom(eeprom_path, eeprom)
        return eeprom

    data = bytearray(eeprom_path.read_bytes())
    if len(data) != EEPROM_SIZE:
        raise ValueError(
            f"Invalid EEPROM file size at {eeprom_path}: expected {EEPROM_SIZE}, got {len(data)}"
        )
    return data


def save_eeprom(path: str | Path, eeprom: bytearray) -> None:
    _check_size(eeprom)
    eeprom_path = Path(path)
    eeprom_path.parent.mkdir(parents=True, exist_ok=True)
    eeprom_path.write_bytes(bytes(eeprom))
