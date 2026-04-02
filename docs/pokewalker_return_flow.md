# Pokewalker Return Flow: EEPROM to HGSS Save

## Goal

Document the real round-trip flow between HGSS and Pokewalker, and how this repository converts a capture from `eeprom.bin` into a boxed Pokemon (`PK4`, 136 bytes) inside an HGSS `.sav`.

## Quick Summary

1. Pokewalker does not store a full PK4 for each capture.
2. EEPROM stores a compact capture summary (species, level, moves, flags).
3. The full PK4 is materialized at transfer time.
4. In this repository, `hgss-stroll-box-return` reads a capture from EEPROM and generates a Pokewalker encounter PK4 (not an egg or hatch template).
5. By default, `hgss-stroll-box-return` also syncs stroll progress (steps/watts/course unlock flags), increments the HGSS Pokewalker trip counter, and can append synthetic trip journal events in EEPROM before writing the boxed capture.
6. The Pokewalker diary text shown in HGSS is generated from EEPROM event log data at sync time; it is not stored as persistent diary text inside the `.sav`.

## EEPROM Data Model Used by the Bridge

Defined in `scripts/eeprom_common.py`:

- `INVENTORY_CAUGHT_OFFSET = 0xCE8C`
- `INVENTORY_CAUGHT_SLOTS = 3`
- `INVENTORY_CAUGHT_ENTRY_SIZE = 16`
- Capture summary format (`POKE_SUMMARY_SIZE = 16`):
  - `speciesId` (u16)
  - `heldItem` (u16)
  - `moves[4]` (4 x u16)
  - `level` (u8)
  - `variantFlags` (u8)
  - `specialFlags` (u8)

## Full Flow

### 1) Send to Stroll (game to EEPROM)

Command: `stroll-send`

- Writes the walking Pokemon summary into `ROUTE_INFO`.
- Configures route/encounters.
- Prepares state for return.

### 2) Walk and Return in EEPROM (Pokewalker)

Command: `stroll-return`

- Applies walked steps and watts.
- Applies EXP and friendship changes to the walking Pokemon using stroll rules.
- Adds catches into `inventory.caught` (max 3 slots).
- Appends journal entries and clears the walking companion from active session fields.

### 3) Transfer to HGSS Save (EEPROM to PC Box)

Command: `hgss-stroll-box-return`

- Reads one capture from `inventory.caught` in `eeprom.bin`.
- Applies stroll EXP/friendship updates to one source boxed Pokemon.
- Generates a PK4 with Pokewalker encounter rules.
- Inserts that PK4 into an empty slot in the same box.
- Re-signs active storage checksums.
- Syncs Pokewalker progress from EEPROM into HGSS (`steps`, `watts`, `courseFlags`), and increments the HGSS Pokewalker trip counter unless `--no-sync-trip-counter` is used.

## Diary Visibility in HGSS

- Diary text is produced from Pokewalker EEPROM event log records during the link return flow.
- Because of that, diary text itself is not a persistent field inside the HGSS save.
- This bridge can append synthetic trip events in EEPROM during `hgss-stroll-box-return` (default behavior).
- Use `--no-trip-journal` to skip adding synthetic capture/return events.

## EXP and Friendship Rules

Applied in `scripts/eeprom_common.py` and `scripts/hgss_save.py`:

- `1 walked step = 1 requested EXP`.
- EXP gain is capped to at most one level per return.
- After the first level-up threshold is reached, no additional EXP is applied in that same return.
- Friendship increases with movement and is clamped to 255.

In formula form:

- `requested_exp = walked_steps` (or override when provided).
- `exp_cap = exp_for_next_level(current_level)`.
- `applied_exp = min(requested_exp, exp_cap - current_exp)`.

## Pokewalker PK4 Rules Applied

Implemented in `scripts/hgss_save.py`.

### Origin Data

- `speciesId`, `level`, `moves`, `variantFlags`, `specialFlags` come from the selected EEPROM capture summary.
- OT/ID/Language are inherited from the target HGSS save trainer context.
- For legality, level/moves/gender are normalized against PKHeX `encounter_walker4` templates for that species.

### Encounter Metadata

- `EggLocation = 0`
- `MetLocation = 233` (`Locations.PokeWalker4` in PKHeX)
- `Ball = Poke Ball`
- `MetLevel = legal template level selected for the captured species`
- `MetDate = current date`

### PID/Nature/Gender

- PID is generated with Pokewalker-style anti-shiny behavior aligned with `PokewalkerRNG.GetPID`.
- Nature is constrained to 0..23 (Quirky is excluded).
- Gender is adjusted from `variantFlags` and species ratio when applicable.
- Ability slot follows PID parity.

### IVs

- IVs are derived from a stroll-style seed (`< 86400`) with Pokewalker-compatible RNG advancement.

## Recommended Command Sequence (Ho-Oh + EEPROM Pidgey)

Prepare a Pidgey capture in EEPROM (example):

```bash
python3 scripts/eeprom_client.py stroll-catch 16 \
  --eeprom scripts/test_roms/eeprom.bin \
  --replace-slot 2
```

Apply return into an HGSS save with anonymized paths:

```bash
python3 scripts/eeprom_client.py hgss-stroll-box-return \
  --save scripts/test_HGSS_saves/input_hgss.sav \
  --eeprom scripts/test_roms/eeprom.bin \
  --output scripts/test_HGSS_saves/output_hgss_return.sav \
  --box 17 \
  --source-slot 1 \
  --extra-species 16 \
  --caught-slot 2 \
  --walked-steps 2400 \
  --bonus-watts 30
```

Notes:

- `--caught-slot` is a 0-based index (0..2).
- If `--caught-slot` is omitted, the first slot matching `--extra-species` is used.
- The command fails if the selected capture is empty or species mismatched.
- Use `--no-sync-eeprom-progress` to only inject the capture PK4 and skip EEPROM->HGSS progress sync.
- Use `--no-trip-journal` to skip adding synthetic capture/return journal entries during sync.
- Use `--no-sync-trip-counter` to skip incrementing the HGSS Pokewalker trip counter.

## Safety and Integrity

- Source slot species is validated before mutation.
- Target slot must be empty.
- Only two box slots are mutated (source and target), plus the active storage checksum bytes.

## PKHeX References

- `PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterStatic4Pokewalker.cs`
- `PKHeX.Core/Legality/RNG/ClassicEra/Gen4/PokewalkerRNG.cs`
- `PKHeX.Core/Game/Locations/Locations.cs`

These references keep encounter semantics aligned with PKHeX legality behavior for Pokewalker-origin Pokemon.
