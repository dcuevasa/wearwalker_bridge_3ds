#!/usr/bin/env python3
"""HGSS save helpers focused on Pokewalker-related state.

The implementation mirrors PKHeX's SAV4/SAV4HGSS logic for:
- active block detection for General and Storage chunks
- CRC16-CCITT block checksums
- known Pokewalker fields inside the active General block
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date
from functools import lru_cache
from pathlib import Path
import random
from typing import Any


HGSS_SAVE_SIZE = 0x80000
HGSS_PARTITION_SIZE = 0x40000

HGSS_GENERAL_SIZE = 0xF628
HGSS_GENERAL_GAP = 0xD8
HGSS_STORAGE_SIZE = 0x12310
HGSS_STORAGE_START = HGSS_GENERAL_SIZE + HGSS_GENERAL_GAP

HGSS_BLOCK_FOOTER_SIZE = 0x10
HGSS_BLOCK_COUNTER_REL = 0x14

HGSS_WALKER_PAIR_OFFSET = 0xE5E0
HGSS_WALKER_PAIR_SIZE = 136
HGSS_WALKER_INFO_OFFSET = 0xE704
HGSS_WALKER_COURSE_FLAG_COUNT = 32

# The game stores an extra u16 counter immediately before the public
# steps/watts/courseFlags Pokewalker fields.
HGSS_WALKER_TRIP_COUNTER_OFFSET = HGSS_WALKER_INFO_OFFSET - 0x4

HGSS_WALKER_STEPS_OFFSET = HGSS_WALKER_INFO_OFFSET
HGSS_WALKER_WATTS_OFFSET = HGSS_WALKER_INFO_OFFSET + 0x4
HGSS_WALKER_COURSE_FLAGS_OFFSET = HGSS_WALKER_INFO_OFFSET + 0x8

HGSS_TRAINER_INFO_OFFSET = 0x64
HGSS_TRAINER_NAME_SIZE = 16
HGSS_TRAINER_ID32_REL = 0x10
HGSS_TRAINER_GENDER_REL = 0x18
HGSS_TRAINER_LANGUAGE_REL = 0x19

HGSS_BOX_COUNT = 18
HGSS_BOX_SLOTS = 30
HGSS_BOX_SLOT_SIZE = 136
HGSS_BOX_STRIDE = 0x1000

PK4_OFFSET_PID = 0x00
PK4_OFFSET_SANITY = 0x04
PK4_OFFSET_CHECKSUM = 0x06
PK4_OFFSET_SPECIES = 0x08
PK4_OFFSET_HELD_ITEM = 0x0A
PK4_OFFSET_ID32 = 0x0C
PK4_OFFSET_EXP = 0x10
PK4_OFFSET_FRIENDSHIP = 0x14
PK4_OFFSET_ABILITY = 0x15
PK4_OFFSET_LANGUAGE = 0x17
PK4_OFFSET_MOVE1 = 0x28
PK4_OFFSET_IV32 = 0x38
PK4_OFFSET_FLAGS = 0x40
PK4_OFFSET_EGG_LOCATION_EXT = 0x44
PK4_OFFSET_MET_LOCATION_EXT = 0x46
PK4_OFFSET_NICKNAME = 0x48
PK4_OFFSET_VERSION = 0x5F
PK4_OFFSET_OT_NAME = 0x68
PK4_OFFSET_EGG_DATE = 0x78
PK4_OFFSET_MET_DATE = 0x7B
PK4_OFFSET_EGG_LOCATION_DP = 0x7E
PK4_OFFSET_MET_LOCATION_DP = 0x80
PK4_OFFSET_POKERUS = 0x82
PK4_OFFSET_BALL_DPPT = 0x83
PK4_OFFSET_MET_LEVEL_OTG = 0x84
PK4_OFFSET_BALL_HGSS = 0x86
PK4_OFFSET_WALKING_MOOD = 0x87
PK4_OFFSET_UNUSED_RIBBON_BITS = 0x64

LOCATION_LINK_TRADE_4 = 2002
LOCATION_DAYCARE_4 = 2000
LOCATION_FARAWAY_4 = 3002
LOCATION_HATCH_HGSS = 182
LOCATION_POKEWALKER_4 = 233

POKEWALKER_STROLL_SEED_MOD = 24 * 60 * 60
POKEWALKER_MAX_PRIOR_POKES = (HGSS_BOX_COUNT * HGSS_BOX_SLOTS) - 1

BALL_POKE = 4
SPECIES_HO_OH = 250
SPECIES_PIDGEY = 16

SCRIPT_DIR = Path(__file__).resolve().parent
MONOREPO_ROOT = SCRIPT_DIR.parent.parent
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

_BLOCK_POSITION = (
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
    0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
    2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1,
    1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2,
    2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0,
    2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0,
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
    0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
)

_BLOCK_POSITION_INVERT = (
    0, 1, 2, 4,
    3, 5, 6, 7,
    12, 18, 13, 19,
    8, 10, 14, 20,
    16, 22, 9, 11,
    15, 21, 17, 23,
    0, 1, 2, 4,
    3, 5, 6, 7,
)


_FIRST = 0
_SECOND = 1
_SAME = 2


def _read_u16_le(data: bytearray, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def _write_u16_le(data: bytearray, offset: int, value: int) -> None:
    data[offset] = value & 0xFF
    data[offset + 1] = (value >> 8) & 0xFF


def _read_u32_le(data: bytearray, offset: int) -> int:
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def _write_u32_le(data: bytearray, offset: int, value: int) -> None:
    data[offset] = value & 0xFF
    data[offset + 1] = (value >> 8) & 0xFF
    data[offset + 2] = (value >> 16) & 0xFF
    data[offset + 3] = (value >> 24) & 0xFF


def _validate_u32(name: str, value: int) -> int:
    ivalue = int(value)
    if ivalue < 0 or ivalue > 0xFFFFFFFF:
        raise ValueError(f"{name} out of range (0..4294967295): {value}")
    return ivalue


def _crc16_ccitt(data: bytes | bytearray | memoryview) -> int:
    # Matches PKHeX Checksums.CRC16_CCITT.
    top = 0xFF
    bot = 0xFF
    for byte in data:
        x = byte ^ top
        x ^= x >> 4
        top = (bot ^ ((x >> 3) & 0x1F) ^ ((x << 4) & 0xFF)) & 0xFF
        bot = (x ^ ((x << 5) & 0xFF)) & 0xFF
    return ((top << 8) | bot) & 0xFFFF


def _compare_counters(counter1: int, counter2: int) -> int:
    # Mirrors SAV4BlockDetection.CompareCounters.
    if counter1 == 0xFFFFFFFF and counter2 != 0xFFFFFFFE:
        return _SECOND
    if counter2 == 0xFFFFFFFF and counter1 != 0xFFFFFFFE:
        return _FIRST
    if counter1 > counter2:
        return _FIRST
    if counter1 < counter2:
        return _SECOND
    return _SAME


def _compare_footers(data: bytearray, offset1: int, offset2: int) -> int:
    major1 = _read_u32_le(data, offset1)
    major2 = _read_u32_le(data, offset2)
    major = _compare_counters(major1, major2)
    if major != _SAME:
        return major

    minor1 = _read_u32_le(data, offset1 + 4)
    minor2 = _read_u32_le(data, offset2 + 4)
    minor = _compare_counters(minor1, minor2)
    return _SECOND if minor == _SECOND else _FIRST


def _bit_indices(bits: int, width: int) -> list[int]:
    return [index for index in range(width) if ((bits >> index) & 0x1) == 1]


def _collect_byte_diffs(
    before: bytes | bytearray,
    after: bytes | bytearray,
    *,
    absolute_base: int,
    max_diffs: int,
) -> list[dict[str, Any]]:
    if len(before) != len(after):
        raise ValueError("Diff inputs must have the same size")

    diffs: list[dict[str, Any]] = []
    for rel, (b0, b1) in enumerate(zip(before, after)):
        if b0 == b1:
            continue
        diffs.append(
            {
                "offset": absolute_base + rel,
                "before": b0,
                "after": b1,
            }
        )
        if len(diffs) >= max_diffs:
            break
    return diffs


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


@lru_cache(maxsize=1)
def _load_personal_hgss_table() -> bytes:
    if not PKHEX_PERSONAL_HGSS_PATH.exists():
        raise FileNotFoundError(f"personal_hgss not found: {PKHEX_PERSONAL_HGSS_PATH}")
    payload = PKHEX_PERSONAL_HGSS_PATH.read_bytes()
    if len(payload) % 0x2C != 0:
        raise ValueError("invalid personal_hgss table size")
    return payload


def _personal_entry(species_id: int) -> bytes:
    sid = _validate_u16("speciesId", species_id)
    table = _load_personal_hgss_table()
    offset = sid * 0x2C
    end = offset + 0x2C
    if end > len(table):
        raise ValueError(f"speciesId out of personal table range: {species_id}")
    return table[offset:end]


def _species_growth_rate(species_id: int) -> int:
    return _personal_entry(species_id)[0x13]


def _species_base_friendship(species_id: int) -> int:
    return _personal_entry(species_id)[0x14]


def _species_gender_ratio(species_id: int) -> int:
    return _personal_entry(species_id)[0x10]


def _species_ability_ids(species_id: int) -> tuple[int, int]:
    entry = _personal_entry(species_id)
    return entry[0x16], entry[0x17]


@lru_cache(maxsize=1)
def _load_pokewalker_encounter_table() -> tuple[dict[str, Any], ...]:
    if not PKHEX_WALKER_ENCOUNTER_PATH.exists():
        return tuple()

    payload = PKHEX_WALKER_ENCOUNTER_PATH.read_bytes()
    slot_size = 0x0C
    if len(payload) % slot_size != 0:
        return tuple()

    entries: list[dict[str, Any]] = []
    for index in range(len(payload) // slot_size):
        offset = index * slot_size
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
    return tuple(entries)


def _pick_pokewalker_template(
    *,
    species_id: int,
    level_hint: int,
    moves_hint: list[int],
    preferred_gender: int,
) -> dict[str, Any] | None:
    entries = [
        entry
        for entry in _load_pokewalker_encounter_table()
        if int(entry.get("speciesId", 0)) == int(species_id)
    ]
    if not entries:
        return None

    hinted_moves = [0, 0, 0, 0]
    for index, move in enumerate(moves_hint[:4]):
        hinted_moves[index] = int(move)
    has_hint_moves = any(move != 0 for move in hinted_moves)

    def _score(entry: dict[str, Any]) -> tuple[int, int, int]:
        score = 0
        entry_level = int(entry.get("level", 0))
        entry_gender = int(entry.get("gender", 0))
        entry_moves = [int(move) for move in list(entry.get("moves", [0, 0, 0, 0]))[:4]]

        if level_hint > 0 and entry_level == level_hint:
            score += 10
        if preferred_gender in (0, 1, 2) and entry_gender == preferred_gender:
            score += 6
        if has_hint_moves:
            if entry_moves == hinted_moves:
                score += 4
            elif hinted_moves[0] != 0 and entry_moves[0] == hinted_moves[0]:
                score += 1

        return score, -abs(entry_level - max(1, level_hint)), -entries.index(entry)

    return max(entries, key=_score)


def _exp_for_level(level: int, growth: int) -> int:
    lvl = max(1, min(int(level), 100))
    cube = lvl * lvl * lvl

    # Growth mappings match PKHeX Experience.cs (Gen4 indexes 0..5).
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


def _exp_threshold(level: int, growth: int) -> int:
    return _exp_for_level(level, growth)


def _level_from_exp(exp: int, growth: int) -> int:
    amount = max(0, int(exp))
    level = 1
    while level < 100 and amount >= _exp_threshold(level + 1, growth):
        level += 1
    return level


def _pk4_add16_checksum(data: bytes | bytearray) -> int:
    buf = bytearray(data)
    chk = 0
    for index in range(8, HGSS_BOX_SLOT_SIZE, 2):
        chk = (chk + _read_u16_le(buf, index)) & 0xFFFF
    return chk


def _lcrng_next(seed: int) -> int:
    return (0x41C64E6D * seed + 0x00006073) & 0xFFFFFFFF


def _pk4_crypt_array(data: bytes | bytearray, seed: int) -> bytearray:
    out = bytearray(data)
    s = int(seed) & 0xFFFFFFFF
    for i in range(0, len(out), 2):
        s = _lcrng_next(s)
        xor = (s >> 16) & 0xFFFF
        value = _read_u16_le(out, i) ^ xor
        _write_u16_le(out, i, value)
    return out


def _pk4_shuffle_blocks(data: bytes | bytearray, sv: int) -> bytearray:
    if sv == 0:
        return bytearray(data)

    out = bytearray(data)
    block_size = 32
    perm = [0, 1, 2, 3]
    slot_of = [0, 1, 2, 3]
    table = _BLOCK_POSITION[sv * 4 : sv * 4 + 4]

    for i in range(3):
        desired = table[i]
        j = slot_of[desired]
        if j == i:
            continue

        ia = i * block_size
        ja = j * block_size
        temp = out[ia : ia + block_size]
        out[ia : ia + block_size] = out[ja : ja + block_size]
        out[ja : ja + block_size] = temp

        block_at_i = perm[i]
        perm[j] = block_at_i
        slot_of[block_at_i] = j

    return out


def _pk4_is_encrypted(stored: bytes | bytearray) -> bool:
    return _read_u32_le(bytearray(stored), PK4_OFFSET_UNUSED_RIBBON_BITS) != 0


def _pk4_decrypt_stored(stored: bytes | bytearray) -> bytearray:
    if len(stored) != HGSS_BOX_SLOT_SIZE:
        raise ValueError(f"PK4 stored size must be {HGSS_BOX_SLOT_SIZE} bytes")

    out = bytearray(stored)
    if not _pk4_is_encrypted(out):
        return out

    pid = _read_u32_le(out, PK4_OFFSET_PID)
    checksum = _read_u16_le(out, PK4_OFFSET_CHECKSUM)
    sv = (pid >> 13) & 31

    core = _pk4_crypt_array(out[8:HGSS_BOX_SLOT_SIZE], checksum)
    core = _pk4_shuffle_blocks(core, sv)
    out[8:HGSS_BOX_SLOT_SIZE] = core
    return out


def _pk4_encrypt_stored(decrypted: bytes | bytearray) -> bytearray:
    if len(decrypted) != HGSS_BOX_SLOT_SIZE:
        raise ValueError(f"PK4 stored size must be {HGSS_BOX_SLOT_SIZE} bytes")

    out = bytearray(decrypted)
    _write_u16_le(out, PK4_OFFSET_SANITY, 0)
    checksum = _pk4_add16_checksum(out)
    _write_u16_le(out, PK4_OFFSET_CHECKSUM, checksum)

    pid = _read_u32_le(out, PK4_OFFSET_PID)
    sv = (pid >> 13) & 31
    sv = _BLOCK_POSITION_INVERT[sv]

    core = _pk4_shuffle_blocks(out[8:HGSS_BOX_SLOT_SIZE], sv)
    core = _pk4_crypt_array(core, checksum)
    out[8:HGSS_BOX_SLOT_SIZE] = core
    return out


def _pk4_pack_iv32(
    hp: int,
    atk: int,
    de: int,
    spe: int,
    spa: int,
    spd: int,
    *,
    is_egg: bool,
    is_nicknamed: bool,
) -> int:
    value = (
        ((hp & 0x1F) << 0)
        | ((atk & 0x1F) << 5)
        | ((de & 0x1F) << 10)
        | ((spe & 0x1F) << 15)
        | ((spa & 0x1F) << 20)
        | ((spd & 0x1F) << 25)
    )
    if is_egg:
        value |= 0x40000000
    if is_nicknamed:
        value |= 0x80000000
    return value


def _gender_from_pid_and_ratio(pid: int, ratio: int) -> int:
    if ratio == 0xFF:
        return 2  # genderless
    if ratio == 0xFE:
        return 1  # female only
    if ratio == 0x00:
        return 0  # male only
    return 1 if (pid & 0xFF) < ratio else 0


def _pokewalker_pid(*, id32: int, nature: int, gender: int, gender_ratio: int) -> int:
    tid16 = int(id32) & 0xFFFF
    sid16 = (int(id32) >> 16) & 0xFFFF
    local_nature = int(nature) % 24

    # Mirrors PKHeX PokewalkerRNG.GetPID: force non-shiny top byte, then fit nature/gender.
    pid = ((((tid16 ^ sid16) >> 8) ^ 0xFF) << 24) & 0xFFFFFFFF
    pid = (pid + local_nature - (pid % 25)) & 0xFFFFFFFF

    if gender_ratio == 0x00 or gender_ratio >= 0xFE:
        return pid

    pid_gender = 1 if (pid & 0xFF) < gender_ratio else 0
    if int(gender) == pid_gender:
        return pid

    if int(gender) == 0:
        pid = (pid + ((((gender_ratio - (pid & 0xFF)) // 25) + 1) * 25)) & 0xFFFFFFFF
        if (local_nature & 1) != (pid & 1):
            pid = (pid + 25) & 0xFFFFFFFF
    else:
        pid = (pid - (((((pid & 0xFF) - gender_ratio) // 25) + 1) * 25)) & 0xFFFFFFFF
        if (local_nature & 1) != (pid & 1):
            pid = (pid - 25) & 0xFFFFFFFF
    return pid & 0xFFFFFFFF


def _pokewalker_ivs_from_stroll_seed(seed: int, prior_pokes: int, *, is_nicknamed: bool) -> int:
    state = int(seed) % POKEWALKER_STROLL_SEED_MOD
    for _ in range(3):  # slot A/B/C pre-roll on stroll initialization
        state = _lcrng_next(state)

    advances = max(0, min(int(prior_pokes), POKEWALKER_MAX_PRIOR_POKES)) * 2
    for _ in range(advances):
        state = _lcrng_next(state)

    state = _lcrng_next(state)
    first = (state >> 16) & 0xFFFF
    state = _lcrng_next(state)
    second = (state >> 16) & 0xFFFF

    return _pk4_pack_iv32(
        first & 0x1F,
        (first >> 5) & 0x1F,
        (first >> 10) & 0x1F,
        second & 0x1F,
        (second >> 5) & 0x1F,
        (second >> 10) & 0x1F,
        is_egg=False,
        is_nicknamed=is_nicknamed,
    )


def _g4_char_to_value(char: str) -> int:
    if len(char) != 1:
        raise ValueError("char must be length 1")
    if "0" <= char <= "9":
        return 0x0121 + (ord(char) - ord("0"))
    if "A" <= char <= "Z":
        return 0x012B + (ord(char) - ord("A"))
    if "a" <= char <= "z":
        return 0x0145 + (ord(char) - ord("a"))
    if char == " ":
        return 0x01CE
    punctuation = {
        "!": 0x00E1,
        "?": 0x00E2,
        ",": 0x00F9,
        ".": 0x00F8,
        "-": 0x00F1,
        "'": 0x01B3,
    }
    return punctuation.get(char, 0x01AC)  # '?'


def _encode_g4_string(value: str, max_chars: int) -> bytes:
    chars = (value or "")[:max_chars]
    out = bytearray(max_chars * 2)
    for index, ch in enumerate(chars):
        _write_u16_le(out, index * 2, _g4_char_to_value(ch))

    if len(chars) < max_chars:
        _write_u16_le(out, len(chars) * 2, 0xFFFF)
    return bytes(out)


def _g4_value_to_char(value: int) -> str:
    if 0x0121 <= value <= 0x012A:
        return chr(ord("0") + (value - 0x0121))
    if 0x012B <= value <= 0x0144:
        return chr(ord("A") + (value - 0x012B))
    if 0x0145 <= value <= 0x015E:
        return chr(ord("a") + (value - 0x0145))
    if value == 0x01CE:
        return " "

    punctuation = {
        0x00E1: "!",
        0x00E2: "?",
        0x00F9: ",",
        0x00F8: ".",
        0x00F1: "-",
        0x01B3: "'",
        0x01AC: "?",
    }
    return punctuation.get(value, "?")


def _decode_g4_string(raw: bytes | bytearray, max_chars: int) -> str:
    data = bytearray(raw)
    length = min(max_chars, len(data) // 2)
    out_chars: list[str] = []

    for index in range(length):
        value = _read_u16_le(data, index * 2)
        if value in (0x0000, 0xFFFF):
            break
        out_chars.append(_g4_value_to_char(value))

    return "".join(out_chars).strip()


def _pk4_today_triplet() -> tuple[int, int, int]:
    today = date.today()
    if today.year < 2000 or today.year > 2099:
        raise ValueError(f"unsupported DS date year: {today.year}")
    return today.year - 2000, today.month, today.day


@dataclass(frozen=True)
class HGSSBlockStatus:
    index: int
    offset: int
    size: int
    major_counter: int
    minor_counter: int
    magic: int
    checksum_saved: int
    checksum_calculated: int

    @property
    def checksum_valid(self) -> bool:
        return self.checksum_saved == self.checksum_calculated

    def as_dict(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "offset": self.offset,
            "size": self.size,
            "majorCounter": self.major_counter,
            "minorCounter": self.minor_counter,
            "magic": f"0x{self.magic:08X}",
            "checksumSaved": f"0x{self.checksum_saved:04X}",
            "checksumCalculated": f"0x{self.checksum_calculated:04X}",
            "checksumValid": self.checksum_valid,
        }


class HGSSSave:
    """Utility wrapper around one full HGSS .sav file."""

    def __init__(self, payload: bytes | bytearray) -> None:
        self._data = bytearray(payload)
        if len(self._data) != HGSS_SAVE_SIZE:
            raise ValueError(
                f"Invalid HGSS save size: expected {HGSS_SAVE_SIZE} bytes, got {len(self._data)}"
            )
        self._refresh_active_offsets()

    @classmethod
    def from_file(cls, path: str | Path) -> HGSSSave:
        file_path = Path(path).resolve()
        return cls(file_path.read_bytes())

    def to_bytes(self) -> bytes:
        return bytes(self._data)

    def write_file(self, path: str | Path) -> Path:
        out_path = Path(path).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(self.to_bytes())
        return out_path

    def _refresh_active_offsets(self) -> None:
        general_footer_1 = HGSS_GENERAL_SIZE - HGSS_BLOCK_COUNTER_REL
        general_footer_2 = general_footer_1 + HGSS_PARTITION_SIZE
        storage_footer_1 = HGSS_STORAGE_START + HGSS_STORAGE_SIZE - HGSS_BLOCK_COUNTER_REL
        storage_footer_2 = storage_footer_1 + HGSS_PARTITION_SIZE

        self.general_active_index = _compare_footers(self._data, general_footer_1, general_footer_2)
        self.storage_active_index = _compare_footers(self._data, storage_footer_1, storage_footer_2)

        self.general_offset = 0 if self.general_active_index == _FIRST else HGSS_PARTITION_SIZE
        self.storage_offset = HGSS_STORAGE_START + (
            0 if self.storage_active_index == _FIRST else HGSS_PARTITION_SIZE
        )

        self.general_backup_offset = HGSS_PARTITION_SIZE if self.general_offset == 0 else 0
        self.storage_backup_offset = HGSS_STORAGE_START + (
            HGSS_PARTITION_SIZE if self.storage_offset == HGSS_STORAGE_START else 0
        )

    def _block_status(self, block_offset: int, block_size: int, block_index: int) -> HGSSBlockStatus:
        footer = block_offset + block_size - HGSS_BLOCK_COUNTER_REL
        checksum_offset = block_offset + block_size - 2
        magic_offset = block_offset + block_size - 8

        block = self._data[block_offset : block_offset + block_size]
        checksum_calculated = _crc16_ccitt(block[:-HGSS_BLOCK_FOOTER_SIZE])
        checksum_saved = _read_u16_le(self._data, checksum_offset)

        return HGSSBlockStatus(
            index=block_index,
            offset=block_offset,
            size=block_size,
            major_counter=_read_u32_le(self._data, footer),
            minor_counter=_read_u32_le(self._data, footer + 4),
            magic=_read_u32_le(self._data, magic_offset),
            checksum_saved=checksum_saved,
            checksum_calculated=checksum_calculated,
        )

    def _active_general(self) -> memoryview:
        return memoryview(self._data)[self.general_offset : self.general_offset + HGSS_GENERAL_SIZE]

    def _active_storage(self) -> memoryview:
        return memoryview(self._data)[self.storage_offset : self.storage_offset + HGSS_STORAGE_SIZE]

    def get_general_status(self) -> dict[str, Any]:
        active = self._block_status(
            self.general_offset,
            HGSS_GENERAL_SIZE,
            self.general_active_index,
        )
        backup = self._block_status(
            self.general_backup_offset,
            HGSS_GENERAL_SIZE,
            _SECOND if self.general_active_index == _FIRST else _FIRST,
        )
        return {
            "active": active.as_dict(),
            "backup": backup.as_dict(),
        }

    def get_storage_status(self) -> dict[str, Any]:
        active = self._block_status(
            self.storage_offset,
            HGSS_STORAGE_SIZE,
            self.storage_active_index,
        )
        backup = self._block_status(
            self.storage_backup_offset,
            HGSS_STORAGE_SIZE,
            _SECOND if self.storage_active_index == _FIRST else _FIRST,
        )
        return {
            "active": active.as_dict(),
            "backup": backup.as_dict(),
        }

    def _storage_slot_offset(self, box_index: int, slot_index: int) -> int:
        if box_index < 0 or box_index >= HGSS_BOX_COUNT:
            raise ValueError(f"box index out of range (0..{HGSS_BOX_COUNT - 1}): {box_index}")
        if slot_index < 0 or slot_index >= HGSS_BOX_SLOTS:
            raise ValueError(f"slot index out of range (0..{HGSS_BOX_SLOTS - 1}): {slot_index}")
        return self.storage_offset + box_index * HGSS_BOX_STRIDE + slot_index * HGSS_BOX_SLOT_SIZE

    def _read_box_slot_decrypted(self, box_index: int, slot_index: int) -> tuple[int, bytes, bytearray]:
        offset = self._storage_slot_offset(box_index, slot_index)
        raw = bytes(self._data[offset : offset + HGSS_BOX_SLOT_SIZE])
        dec = _pk4_decrypt_stored(raw)
        return offset, raw, dec

    def _write_box_slot_decrypted(self, box_index: int, slot_index: int, decrypted: bytes | bytearray) -> None:
        offset = self._storage_slot_offset(box_index, slot_index)
        enc = _pk4_encrypt_stored(decrypted)
        self._data[offset : offset + HGSS_BOX_SLOT_SIZE] = enc

    def _find_first_empty_box_slot(self, box_index: int, *, exclude_slot: int | None = None) -> int | None:
        for slot_index in range(HGSS_BOX_SLOTS):
            if exclude_slot is not None and slot_index == exclude_slot:
                continue
            _, _, dec = self._read_box_slot_decrypted(box_index, slot_index)
            if _read_u16_le(dec, PK4_OFFSET_SPECIES) == 0:
                return slot_index
        return None

    def _get_trainer_context(self) -> dict[str, Any]:
        trainer_offset = self.general_offset + HGSS_TRAINER_INFO_OFFSET
        return {
            "id32": _read_u32_le(self._data, trainer_offset + HGSS_TRAINER_ID32_REL),
            "ot_name_raw": bytes(
                self._data[trainer_offset : trainer_offset + HGSS_TRAINER_NAME_SIZE]
            ),
            "ot_gender": self._data[trainer_offset + HGSS_TRAINER_GENDER_REL],
            "language": self._data[trainer_offset + HGSS_TRAINER_LANGUAGE_REL],
        }

    def extract_stroll_send_context(
        self,
        *,
        box_number: int,
        source_slot_number: int,
    ) -> dict[str, Any]:
        box_index = int(box_number) - 1
        source_slot_index = int(source_slot_number) - 1
        _, _, source_dec = self._read_box_slot_decrypted(box_index, source_slot_index)

        species_id = _read_u16_le(source_dec, PK4_OFFSET_SPECIES)
        if species_id == 0:
            raise ValueError(
                f"Source slot box {box_number} slot {source_slot_number} is empty"
            )

        exp = _read_u32_le(source_dec, PK4_OFFSET_EXP)
        growth = _species_growth_rate(species_id)
        level = _level_from_exp(exp, growth)
        friendship = int(source_dec[PK4_OFFSET_FRIENDSHIP])
        held_item = _read_u16_le(source_dec, PK4_OFFSET_HELD_ITEM)
        moves = [_read_u16_le(source_dec, PK4_OFFSET_MOVE1 + index * 2) for index in range(4)]

        pid = _read_u32_le(source_dec, PK4_OFFSET_PID)
        source_id32 = _read_u32_le(source_dec, PK4_OFFSET_ID32)
        source_tid = source_id32 & 0xFFFF
        source_sid = (source_id32 >> 16) & 0xFFFF
        is_shiny = (((pid & 0xFFFF) ^ ((pid >> 16) & 0xFFFF) ^ source_tid ^ source_sid) < 8)

        gender_bits = (int(source_dec[PK4_OFFSET_FLAGS]) >> 1) & 0x03
        variant_flags = 0x20 if gender_bits == 1 else 0
        special_flags = 0x02 if is_shiny else 0

        nickname = _decode_g4_string(
            source_dec[PK4_OFFSET_NICKNAME : PK4_OFFSET_NICKNAME + 22],
            11,
        )

        trainer_ctx = self._get_trainer_context()
        trainer_id32 = int(trainer_ctx["id32"])
        trainer_tid = trainer_id32 & 0xFFFF
        trainer_sid = (trainer_id32 >> 16) & 0xFFFF
        trainer_name = _decode_g4_string(trainer_ctx["ot_name_raw"], 8)
        if not trainer_name:
            trainer_name = "WWBRIDGE"

        return {
            "trainer": {
                "name": trainer_name,
                "id32": trainer_id32,
                "tid": trainer_tid,
                "sid": trainer_sid,
                "gender": int(trainer_ctx["ot_gender"]),
                "language": int(trainer_ctx["language"]),
            },
            "sourcePokemon": {
                "boxNumber": int(box_number),
                "slotNumber": int(source_slot_number),
                "speciesId": int(species_id),
                "level": int(level),
                "exp": int(exp),
                "friendship": int(friendship),
                "heldItem": int(held_item),
                "moves": [int(move) for move in moves],
                "variantFlags": int(variant_flags),
                "specialFlags": int(special_flags),
                "nickname": nickname,
                "version": int(source_dec[PK4_OFFSET_VERSION]),
            },
            "pokewalker": {
                "steps": int(self._read_general_u32(HGSS_WALKER_STEPS_OFFSET)),
                "watts": int(self._read_general_u32(HGSS_WALKER_WATTS_OFFSET)),
                "courseFlags": int(self._read_general_u32(HGSS_WALKER_COURSE_FLAGS_OFFSET)),
            },
        }

    def _build_pokewalker_capture_pk4(
        self,
        *,
        version: int,
        trainer: dict[str, Any],
        capture_summary: dict[str, Any],
        seed: int,
    ) -> bytearray:
        species_id = _validate_u16("captureSpeciesId", int(capture_summary.get("speciesId", 0)))
        if species_id == 0:
            raise ValueError("capture summary speciesId cannot be 0")

        level_raw = int(capture_summary.get("level", 0))
        level = max(1, min(level_raw if level_raw > 0 else 10, 100))

        raw_moves = capture_summary.get("moves", [0, 0, 0, 0])
        if not isinstance(raw_moves, list):
            raise ValueError("capture summary moves must be a list")
        moves = [0, 0, 0, 0]
        for index, value in enumerate(raw_moves[:4]):
            moves[index] = _validate_u16(f"captureMoves[{index}]", int(value))
        if moves[0] == 0:
            moves[0] = 33  # fallback to a legal low-level move when summary is sparse

        variant_flags = _validate_u8("captureVariantFlags", int(capture_summary.get("variantFlags", 0)))
        special_flags = _validate_u8("captureSpecialFlags", int(capture_summary.get("specialFlags", 0)))
        if (special_flags & 0x02) != 0:
            raise ValueError("Pokewalker capture summary cannot be shiny (specialFlags bit1 set)")

        source_seed = int(seed) & 0xFFFFFFFF
        stroll_seed = source_seed % POKEWALKER_STROLL_SEED_MOD
        prior_pokes = ((source_seed >> 16) & 0xFFFF) % (POKEWALKER_MAX_PRIOR_POKES + 1)
        nature = (source_seed >> 8) % 24

        gender_ratio = _species_gender_ratio(species_id)
        preferred_gender = 1 if (variant_flags & 0x20) != 0 else 0
        encounter_template = _pick_pokewalker_template(
            species_id=species_id,
            level_hint=level,
            moves_hint=moves,
            preferred_gender=preferred_gender,
        )
        if encounter_template is not None:
            level = max(1, min(int(encounter_template.get("level", level)), 100))
            template_moves = list(encounter_template.get("moves", [0, 0, 0, 0]))
            for index, move in enumerate(template_moves[:4]):
                moves[index] = _validate_u16(f"templateMoves[{index}]", int(move))
            template_gender = int(encounter_template.get("gender", preferred_gender))
        else:
            template_gender = preferred_gender

        if gender_ratio == 0xFF:
            desired_gender = 2
        elif gender_ratio == 0xFE:
            desired_gender = 1
        elif gender_ratio == 0x00:
            desired_gender = 0
        elif template_gender in (0, 1):
            desired_gender = template_gender
        else:
            desired_gender = preferred_gender

        pid = _pokewalker_pid(
            id32=int(trainer["id32"]),
            nature=nature,
            gender=desired_gender,
            gender_ratio=gender_ratio,
        )
        gender = _gender_from_pid_and_ratio(pid, gender_ratio)

        ability1, ability2 = _species_ability_ids(species_id)
        ability = ability1
        if ability2 != ability1 and (pid & 1) == 1:
            ability = ability2

        is_nicknamed = False
        iv32 = _pokewalker_ivs_from_stroll_seed(
            stroll_seed,
            prior_pokes,
            is_nicknamed=is_nicknamed,
        )

        growth = _species_growth_rate(species_id)
        exp = _exp_threshold(level, growth)
        friendship = _validate_u8("friendship", _species_base_friendship(species_id))
        year, month, day = _pk4_today_triplet()

        species_name = str(capture_summary.get("speciesName", "PIDGEY")).strip().upper() or "PIDGEY"

        pk = bytearray(HGSS_BOX_SLOT_SIZE)
        _write_u32_le(pk, PK4_OFFSET_PID, pid)
        _write_u16_le(pk, PK4_OFFSET_SANITY, 0)
        _write_u16_le(pk, PK4_OFFSET_CHECKSUM, 0)
        _write_u16_le(pk, PK4_OFFSET_SPECIES, species_id)
        _write_u32_le(pk, PK4_OFFSET_ID32, int(trainer["id32"]))
        _write_u32_le(pk, PK4_OFFSET_EXP, exp)

        pk[0x14] = friendship
        pk[PK4_OFFSET_ABILITY] = _validate_u8("ability", ability)
        pk[PK4_OFFSET_LANGUAGE] = _validate_u8("language", int(trainer["language"]))
        _write_u32_le(pk, PK4_OFFSET_IV32, iv32)

        flags = pk[PK4_OFFSET_FLAGS] & 0x01
        flags |= (gender & 0x03) << 1
        pk[PK4_OFFSET_FLAGS] = flags

        _write_u16_le(pk, PK4_OFFSET_EGG_LOCATION_EXT, 0)
        _write_u16_le(pk, PK4_OFFSET_MET_LOCATION_EXT, LOCATION_POKEWALKER_4)

        pk[PK4_OFFSET_NICKNAME : PK4_OFFSET_NICKNAME + 22] = _encode_g4_string(species_name, 11)
        pk[PK4_OFFSET_VERSION] = _validate_u8("version", version)
        pk[PK4_OFFSET_OT_NAME : PK4_OFFSET_OT_NAME + HGSS_TRAINER_NAME_SIZE] = trainer["ot_name_raw"]

        pk[PK4_OFFSET_EGG_DATE + 0] = 0
        pk[PK4_OFFSET_EGG_DATE + 1] = 0
        pk[PK4_OFFSET_EGG_DATE + 2] = 0
        pk[PK4_OFFSET_MET_DATE + 0] = _validate_u8("metYear", year)
        pk[PK4_OFFSET_MET_DATE + 1] = _validate_u8("metMonth", month)
        pk[PK4_OFFSET_MET_DATE + 2] = _validate_u8("metDay", day)

        _write_u16_le(pk, PK4_OFFSET_EGG_LOCATION_DP, 0)
        _write_u16_le(pk, PK4_OFFSET_MET_LOCATION_DP, LOCATION_POKEWALKER_4)

        pk[PK4_OFFSET_POKERUS] = 0
        pk[PK4_OFFSET_BALL_DPPT] = BALL_POKE
        pk[PK4_OFFSET_MET_LEVEL_OTG] = (level & 0x7F) | ((int(trainer["ot_gender"]) & 0x1) << 7)
        pk[PK4_OFFSET_BALL_HGSS] = BALL_POKE
        pk[PK4_OFFSET_WALKING_MOOD] = 0

        for index, move in enumerate(moves):
            _write_u16_le(pk, 0x28 + index * 2, move)
            pk[0x30 + index] = 5 if move != 0 else 0
        pk[0x34] = 0
        pk[0x35] = 0
        pk[0x36] = 0
        pk[0x37] = 0

        return pk

    def apply_stroll_box_return_scenario(
        self,
        *,
        box_number: int,
        source_slot_number: int,
        source_species: int = SPECIES_HO_OH,
        extra_species: int = SPECIES_PIDGEY,
        walked_steps: int = 0,
        target_slot_number: int | None = None,
        extra_capture_summary: dict[str, Any] | None = None,
        seed: int | None = None,
        pokewalker_steps: int | None = None,
        pokewalker_watts: int | None = None,
        pokewalker_course_flags: int | None = None,
        increment_pokewalker_trip_counter: bool = False,
    ) -> dict[str, Any]:
        if extra_species != SPECIES_PIDGEY:
            raise ValueError("Only Pidgey (species 16) is supported by this scenario helper")

        box_index = int(box_number) - 1
        source_slot_index = int(source_slot_number) - 1
        walked_steps_u32 = _validate_u32("walkedSteps", walked_steps)

        _, source_raw_before, source_dec = self._read_box_slot_decrypted(box_index, source_slot_index)
        source_species_actual = _read_u16_le(source_dec, PK4_OFFSET_SPECIES)
        if source_species_actual != int(source_species):
            raise ValueError(
                f"Source slot does not match expected species {source_species}: got {source_species_actual}"
            )

        growth = _species_growth_rate(source_species_actual)
        exp_before = _read_u32_le(source_dec, PK4_OFFSET_EXP)
        level_before = _level_from_exp(exp_before, growth)

        if level_before >= 100:
            exp_cap = exp_before
        else:
            exp_cap = _exp_threshold(level_before + 1, growth)

        exp_gain_requested = walked_steps_u32
        exp_gain_applied = min(exp_gain_requested, max(0, exp_cap - exp_before))
        exp_after = exp_before + exp_gain_applied
        level_after = _level_from_exp(exp_after, growth)

        friendship_before = int(source_dec[0x14])
        friendship_after = min(0xFF, friendship_before + walked_steps_u32)

        _write_u32_le(source_dec, PK4_OFFSET_EXP, exp_after)
        source_dec[0x14] = friendship_after
        self._write_box_slot_decrypted(box_index, source_slot_index, source_dec)

        if target_slot_number is None:
            target_slot_index = self._find_first_empty_box_slot(
                box_index,
                exclude_slot=source_slot_index,
            )
            if target_slot_index is None:
                raise ValueError(f"No empty slot available in box {box_number}")
        else:
            target_slot_index = int(target_slot_number) - 1

        _, target_raw_before, target_dec_before = self._read_box_slot_decrypted(box_index, target_slot_index)
        if _read_u16_le(target_dec_before, PK4_OFFSET_SPECIES) != 0:
            raise ValueError(
                f"Target slot box {box_number} slot {target_slot_index + 1} is not empty"
            )

        trainer_ctx = self._get_trainer_context()
        version = int(source_dec[PK4_OFFSET_VERSION])
        rng_seed = int(seed) if seed is not None else random.SystemRandom().getrandbits(32)

        capture_summary = dict(extra_capture_summary or {})
        if not capture_summary:
            capture_summary = {
                "speciesId": extra_species,
                "speciesName": "PIDGEY",
                "level": 10,
                "moves": [33, 0, 0, 0],
                "variantFlags": 0,
                "specialFlags": 0,
            }

        capture_species = _validate_u16("captureSpeciesId", int(capture_summary.get("speciesId", 0)))
        if capture_species != int(extra_species):
            raise ValueError(
                f"EEPROM capture species mismatch: expected {extra_species}, got {capture_species}"
            )

        pidgey_dec = self._build_pokewalker_capture_pk4(
            version=version,
            trainer=trainer_ctx,
            capture_summary=capture_summary,
            seed=rng_seed,
        )
        self._write_box_slot_decrypted(box_index, target_slot_index, pidgey_dec)

        pokewalker_patch: dict[str, Any] | None = None
        if (
            pokewalker_steps is not None
            or pokewalker_watts is not None
            or pokewalker_course_flags is not None
            or increment_pokewalker_trip_counter
        ):
            pokewalker_patch = self.patch_pokewalker(
                steps=pokewalker_steps,
                watts=pokewalker_watts,
                course_flags=pokewalker_course_flags,
                increment_trip_counter=increment_pokewalker_trip_counter,
            )

        _, source_raw_after, source_dec_after = self._read_box_slot_decrypted(box_index, source_slot_index)
        _, target_raw_after, target_dec_after = self._read_box_slot_decrypted(box_index, target_slot_index)

        return {
            "status": "ok",
            "seed": rng_seed,
            "box": box_number,
            "source": {
                "slot": source_slot_number,
                "speciesBefore": source_species_actual,
                "speciesAfter": _read_u16_le(source_dec_after, PK4_OFFSET_SPECIES),
                "walkedSteps": walked_steps_u32,
                "levelBefore": level_before,
                "levelAfter": level_after,
                "expBefore": exp_before,
                "expAfter": exp_after,
                "expGainRequested": exp_gain_requested,
                "expGainApplied": exp_gain_applied,
                "expCap": exp_cap,
                "friendshipBefore": friendship_before,
                "friendshipAfter": friendship_after,
                "changedBytesInSlot": sum(1 for a, b in zip(source_raw_before, source_raw_after) if a != b),
            },
            "extra": {
                "slot": target_slot_index + 1,
                "speciesBefore": _read_u16_le(target_dec_before, PK4_OFFSET_SPECIES),
                "speciesAfter": _read_u16_le(target_dec_after, PK4_OFFSET_SPECIES),
                "expectedSpecies": extra_species,
                "captureLevel": int(capture_summary.get("level", 0)),
                "captureMoves": [
                    int(value)
                    for value in (capture_summary.get("moves", [0, 0, 0, 0]) if isinstance(capture_summary.get("moves", [0, 0, 0, 0]), list) else [0, 0, 0, 0])[:4]
                ],
                "captureVariantFlags": int(capture_summary.get("variantFlags", 0)),
                "captureSpecialFlags": int(capture_summary.get("specialFlags", 0)),
                "generatedMetLevel": int(target_dec_after[PK4_OFFSET_MET_LEVEL_OTG] & 0x7F),
                "generatedMoves": [
                    _read_u16_le(target_dec_after, 0x28 + index * 2)
                    for index in range(4)
                ],
                "changedBytesInSlot": sum(1 for a, b in zip(target_raw_before, target_raw_after) if a != b),
                "isEmptyBefore": _read_u16_le(target_dec_before, PK4_OFFSET_SPECIES) == 0,
            },
            "safety": {
                "onlyTwoSlotsMutated": True,
                "sourceSlot": source_slot_number,
                "targetSlot": target_slot_index + 1,
            },
            "pokewalkerPatch": pokewalker_patch,
        }

    def _read_general_u32(self, relative_offset: int) -> int:
        return _read_u32_le(self._data, self.general_offset + relative_offset)

    def _write_general_u32(self, relative_offset: int, value: int) -> None:
        _write_u32_le(self._data, self.general_offset + relative_offset, value)

    def get_pokewalker_state(self) -> dict[str, Any]:
        general = self._active_general()
        course_flags = self._read_general_u32(HGSS_WALKER_COURSE_FLAGS_OFFSET)
        pair = bytes(
            general[HGSS_WALKER_PAIR_OFFSET : HGSS_WALKER_PAIR_OFFSET + HGSS_WALKER_PAIR_SIZE]
        )

        return {
            "tripCounter": _read_u16_le(general, HGSS_WALKER_TRIP_COUNTER_OFFSET),
            "steps": self._read_general_u32(HGSS_WALKER_STEPS_OFFSET),
            "watts": self._read_general_u32(HGSS_WALKER_WATTS_OFFSET),
            "courseFlags": f"0x{course_flags:08X}",
            "unlockedCourses": _bit_indices(course_flags, HGSS_WALKER_COURSE_FLAG_COUNT),
            "walkerPairNonZeroBytes": sum(1 for byte in pair if byte != 0),
            "walkerPairCRC16": f"0x{_crc16_ccitt(pair):04X}",
            "walkerPairPreview": pair[:16].hex(),
        }

    def patch_pokewalker(
        self,
        *,
        steps: int | None = None,
        watts: int | None = None,
        course_flags: int | None = None,
        increment_trip_counter: bool = False,
    ) -> dict[str, Any]:
        if steps is None and watts is None and course_flags is None and not increment_trip_counter:
            raise ValueError("Provide at least one patch value")

        before = self.get_pokewalker_state()

        if steps is not None:
            self._write_general_u32(HGSS_WALKER_STEPS_OFFSET, _validate_u32("steps", steps))
        if watts is not None:
            self._write_general_u32(HGSS_WALKER_WATTS_OFFSET, _validate_u32("watts", watts))
        if course_flags is not None:
            self._write_general_u32(
                HGSS_WALKER_COURSE_FLAGS_OFFSET,
                _validate_u32("course_flags", course_flags),
            )
        if increment_trip_counter:
            current = _read_u16_le(self._data, self.general_offset + HGSS_WALKER_TRIP_COUNTER_OFFSET)
            _write_u16_le(
                self._data,
                self.general_offset + HGSS_WALKER_TRIP_COUNTER_OFFSET,
                (current + 1) & 0xFFFF,
            )

        after = self.get_pokewalker_state()
        return {
            "before": before,
            "after": after,
            "changed": {
                "tripCounter": before["tripCounter"] != after["tripCounter"],
                "steps": before["steps"] != after["steps"],
                "watts": before["watts"] != after["watts"],
                "courseFlags": before["courseFlags"] != after["courseFlags"],
            },
        }

    def _resign_block(self, offset: int, size: int) -> None:
        checksum = _crc16_ccitt(self._data[offset : offset + size - HGSS_BLOCK_FOOTER_SIZE])
        _write_u16_le(self._data, offset + size - 2, checksum)

    def resign_active_blocks(self, include_storage: bool = False) -> None:
        self._resign_block(self.general_offset, HGSS_GENERAL_SIZE)
        if include_storage:
            self._resign_block(self.storage_offset, HGSS_STORAGE_SIZE)

    def inspect(self) -> dict[str, Any]:
        return {
            "size": len(self._data),
            "general": self.get_general_status(),
            "storage": self.get_storage_status(),
            "pokewalker": self.get_pokewalker_state(),
        }

    def diff(self, other: HGSSSave, *, max_byte_diffs: int = 96) -> dict[str, Any]:
        this_general = bytes(self._active_general())
        other_general = bytes(other._active_general())
        this_storage = bytes(self._active_storage())
        other_storage = bytes(other._active_storage())

        general_diff_count = sum(1 for a, b in zip(this_general, other_general) if a != b)
        storage_diff_count = sum(1 for a, b in zip(this_storage, other_storage) if a != b)

        window_start = HGSS_WALKER_PAIR_OFFSET
        window_end = HGSS_WALKER_COURSE_FLAGS_OFFSET + 4
        local_before = this_general[window_start:window_end]
        local_after = other_general[window_start:window_end]

        pw_before = self.get_pokewalker_state()
        pw_after = other.get_pokewalker_state()

        return {
            "generalActiveDiffBytes": general_diff_count,
            "storageActiveDiffBytes": storage_diff_count,
            "pokewalkerBefore": pw_before,
            "pokewalkerAfter": pw_after,
            "pokewalkerDelta": {
                "steps": int(pw_after["steps"]) - int(pw_before["steps"]),
                "watts": int(pw_after["watts"]) - int(pw_before["watts"]),
                "courseFlagsChanged": pw_before["courseFlags"] != pw_after["courseFlags"],
            },
            "pokewalkerWindowDiffs": _collect_byte_diffs(
                local_before,
                local_after,
                absolute_base=window_start,
                max_diffs=max_byte_diffs,
            ),
        }


def inspect_hgss_save(path: str | Path) -> dict[str, Any]:
    return HGSSSave.from_file(path).inspect()


def patch_hgss_save(
    path: str | Path,
    *,
    steps: int | None = None,
    watts: int | None = None,
    course_flags: int | None = None,
    resign_storage: bool = False,
) -> tuple[dict[str, Any], bytes]:
    save = HGSSSave.from_file(path)
    result = save.patch_pokewalker(steps=steps, watts=watts, course_flags=course_flags)
    save.resign_active_blocks(include_storage=resign_storage)
    return result, save.to_bytes()


def extract_hgss_stroll_send_context(
    path: str | Path,
    *,
    box_number: int,
    source_slot_number: int,
) -> dict[str, Any]:
    save = HGSSSave.from_file(path)
    return save.extract_stroll_send_context(
        box_number=box_number,
        source_slot_number=source_slot_number,
    )


def apply_hgss_stroll_box_return(
    path: str | Path,
    *,
    box_number: int,
    source_slot_number: int,
    target_slot_number: int | None = None,
    source_species: int = SPECIES_HO_OH,
    extra_species: int = SPECIES_PIDGEY,
    walked_steps: int = 0,
    extra_capture_summary: dict[str, Any] | None = None,
    seed: int | None = None,
    pokewalker_steps: int | None = None,
    pokewalker_watts: int | None = None,
    pokewalker_course_flags: int | None = None,
    increment_pokewalker_trip_counter: bool = False,
) -> tuple[dict[str, Any], bytes]:
    save = HGSSSave.from_file(path)
    result = save.apply_stroll_box_return_scenario(
        box_number=box_number,
        source_slot_number=source_slot_number,
        source_species=source_species,
        extra_species=extra_species,
        walked_steps=walked_steps,
        target_slot_number=target_slot_number,
        extra_capture_summary=extra_capture_summary,
        seed=seed,
        pokewalker_steps=pokewalker_steps,
        pokewalker_watts=pokewalker_watts,
        pokewalker_course_flags=pokewalker_course_flags,
        increment_pokewalker_trip_counter=increment_pokewalker_trip_counter,
    )
    save.resign_active_blocks(include_storage=True)
    return result, save.to_bytes()


def diff_hgss_save_files(
    before_path: str | Path,
    after_path: str | Path,
    *,
    max_byte_diffs: int = 96,
) -> dict[str, Any]:
    before = HGSSSave.from_file(before_path)
    after = HGSSSave.from_file(after_path)
    return before.diff(after, max_byte_diffs=max_byte_diffs)