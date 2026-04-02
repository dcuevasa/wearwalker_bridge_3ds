# Obtaining Dynamic Pokewalker Sprites

## Scope

This document explains:

- where each dynamic Pokewalker sprite block comes from,
- how the 3DS resolves route-dependent data from HGSS `.nds`,
- how that data is sent to `/api/v1/stroll/send`,
- and how sprite bytes are patched through `/api/v2/stroll/sprite-patches`.

It documents the current strict architecture: route semantics are resolved on 3DS from ROM data, and the backend applies validated values to EEPROM.

## Current Implementation Status

Implemented now:

- Name sprites are generated directly on 3DS and patched via `/api/v2/stroll/sprite-patches`.
- Pokemon animated sprites are extracted from ROM and patched via `/api/v2/stroll/sprite-patches`.
- Route area image sprite (`areaSprite`) is extracted from ARM9 overlay 112 and patched via `/api/v2/stroll/sprite-patches`.
- `/api/v1/stroll/send` requires `resolvedRouteConfig`; 3DS sends full resolved route slots/items/image metadata every send.
- `resolvedRouteConfig.advantagedTypes` is included and validated.

Design rule:

- If a block is not explicitly patched, it remains unchanged in EEPROM.

## Responsibility Split (Current)

3DS responsibilities:

- Parse HGSS `.nds` data needed for route resolution.
- Resolve route image index, route slots, route items, and advantaged types.
- Build/send `resolvedRouteConfig` inside `/api/v1/stroll/send`.
- Extract sprite assets and push patch batches.

Backend responsibilities:

- Validate the resolved payload contract.
- Apply provided route slots/items/image to EEPROM.
- Return normalized API response fields (`configuredRouteSlots`, `configuredRouteItems`, `courseSelection`, etc.).

Important endpoint behavior:

- `/api/v1/stroll/send` returns `missing_resolved_route_config` if `resolvedRouteConfig` is omitted.

## Route Data Provenance

### Canonical ROM source used by 3DS

HGSS ARM9 overlay 112 (`overlay9_112`):

- Route table DS virtual address: `0x021F4138`
- Record size: `0xC0`
- Record count currently used: 27 courses

Route area image pointer table:

- DS virtual address: `0x021FF528`
- Entry lookup uses `routeImageIndex + 1`

### Reverse-engineering reference used for schema validation

The struct and field meaning are aligned with the provided reverse notes (`Pokewalker hacking - Dmitry.GR`, DS-side section), including `RouteInfo` and `advantagedTypes`.

```c
struct RouteInfo {
    uint32_t wattsToUnlock;         // +0x00
    uint32_t imageIdx;              // +0x04
    struct RoutePokeInfo pokes[6];  // +0x08 (6 * 0x14)
    struct RouteItemInfo items[10]; // +0x80 (10 * 0x06)
    uint8_t advantagedTypes[3];     // +0xBC
    uint8_t padding;                // +0xBF
};
```

`advantagedTypes` values correspond to type indexes from `mTypeNames` in the same reverse notes:

| Index | Type |
|---:|---|
| 0 | Normal |
| 1 | Fighting |
| 2 | Flying |
| 3 | Poison |
| 4 | Ground |
| 5 | Rock |
| 6 | Bug |
| 7 | Ghost |
| 8 | Steel |
| 9 | Unknown (`(? ? ?)` placeholder) |
| 10 | Fire |
| 11 | Water |
| 12 | Grass |
| 13 | Electric |
| 14 | Psychic |
| 15 | Ice |
| 16 | Dragon |
| 17 | Dark |

## How 3DS Builds `resolvedRouteConfig`

Source file:

- `source/ui.c` (`ww_build_resolved_stroll_send_json`)

Mapping summary:

- `routeImageIndex`: `imageIdx - 1` from `+0x04` (fallback to course id when zero)
- `advantagedTypes[3]`: bytes `+0xBC..+0xBE`
- `slots[3]`: chosen from pairs inside `pokes[6]`
- `items[10]`: copied from `items[10]`

Slot pair selection rule:

- For each output slot group `(0/1), (2/3), (4/5)`, one pair member is chosen by a local LCG bit.
- The chosen source index is serialized as `sourcePairIndex`.

Field-level extraction:

- Slot entry base: `+0x08 + sourcePairIndex * 0x14`
- Slot fields:
  - `speciesId`: `+0x00` (`u16`)
  - `level`: `+0x02` (`u8`, clamped to minimum 1)
  - `gender`: `+0x07` (`u8`, raw forwarded)
  - `moves[4]`: `+0x08..+0x0F` (`u16` each)
  - `minSteps`: `+0x10` (`u16`)
  - `chance`: `+0x12` (`u8` chance + `u8` padding in reverse layout; client reads LE16 and clamps to `u8`)
- Item entry base: `+0x80 + routeItemIndex * 0x06`
- Item fields:
  - `itemId`: `+0x00` (`u16`)
  - `minSteps`: `+0x02` (`u16`)
  - `chance`: `+0x04` (`u8` chance + `u8` padding in reverse layout; client reads LE16 and clamps to `u8`)

Additional metadata:

- `romSize` and `romMtime` come from selected ROM file metadata.
- Client also emits `routeSeed` for reproducibility/traceability.

## 3DS ROM Route Cache

To avoid reparsing overlay 112 every send, 3DS stores a local cache:

- Path: `sdmc:/3ds/wearwalker_bridge/rom_course_cache.bin`
- Key fields: ROM size + ROM mtime
- Cached payload: full `0xC0 * 27` route table block

Behavior:

1. Try cache load by ROM identity.
2. If miss, read overlay 112 from `.nds`, extract table, save cache.
3. Continue send with resolved payload.

## Dynamic Sprite Address Map

### Area and walking Pokemon

| Logical block | Offset | Size | Meaning |
|---|---:|---:|---|
| areaSprite | 0x8FBE | 0x0C0 | Route area image (32x24) |
| areaNameSprite | 0x907E | 0x140 | Route area name (80x16) |
| walkPokeSmall0 | 0x91BE | 0x0C0 | Walking Pokemon small frame 0 (32x24) |
| walkPokeSmall1 | 0x927E | 0x0C0 | Walking Pokemon small frame 1 (32x24) |
| walkPokeLarge0 | 0x933E | 0x300 | Walking Pokemon large frame 0 (64x48) |
| walkPokeLarge1 | 0x963E | 0x300 | Walking Pokemon large frame 1 (64x48) |
| joinPokeLarge0 | 0x9EFE | 0x300 | Route/join Pokemon large frame 0 (64x48) |
| joinPokeLarge1 | 0xA1FE | 0x300 | Route/join Pokemon large frame 1 (64x48) |
| walkPokeName | 0x993E | 0x140 | Walking Pokemon name (80x16) |
| trainerCardName | 0x1250 | 0x140 | Trainer Card name (80x16) |

### Route encounter Pokemon

| Logical block | Offset | Size | Meaning |
|---|---:|---:|---|
| routePoke0Small0 | 0x9A7E | 0x0C0 | Route slot 0 small frame 0 |
| routePoke0Small1 | 0x9B3E | 0x0C0 | Route slot 0 small frame 1 |
| routePoke1Small0 | 0x9BFE | 0x0C0 | Route slot 1 small frame 0 |
| routePoke1Small1 | 0x9CBE | 0x0C0 | Route slot 1 small frame 1 |
| routePoke2Small0 | 0x9D7E | 0x0C0 | Route slot 2 small frame 0 |
| routePoke2Small1 | 0x9E3E | 0x0C0 | Route slot 2 small frame 1 |
| routePoke0Name | 0xA4FE | 0x140 | Route slot 0 name (80x16) |
| routePoke1Name | 0xA63E | 0x140 | Route slot 1 name (80x16) |
| routePoke2Name | 0xA77E | 0x140 | Route slot 2 name (80x16) |

### Route item names

| Logical block | Offset | Size | Meaning |
|---|---:|---:|---|
| routeItem0Name | 0xA8BE | 0x180 | Route item 0 name (96x16) |
| routeItem1Name | 0xAA3E | 0x180 | Route item 1 name (96x16) |
| routeItem2Name | 0xABBE | 0x180 | Route item 2 name (96x16) |
| routeItem3Name | 0xAD3E | 0x180 | Route item 3 name (96x16) |
| routeItem4Name | 0xAEBE | 0x180 | Route item 4 name (96x16) |
| routeItem5Name | 0xB03E | 0x180 | Route item 5 name (96x16) |
| routeItem6Name | 0xB1BE | 0x180 | Route item 6 name (96x16) |
| routeItem7Name | 0xB33E | 0x180 | Route item 7 name (96x16) |
| routeItem8Name | 0xB4BE | 0x180 | Route item 8 name (96x16) |
| routeItem9Name | 0xB63E | 0x180 | Route item 9 name (96x16) |

## Source Map By Sprite Family

| Block family | Source in pipeline | ROM path used | Selection rule |
|---|---|---|---|
| walkPokeSmall* | Pokemon sprite assets | `a/2/4/8` | walking species id |
| walkPokeLarge* | Pokemon sprite assets | `a/2/5/6` | walking species id |
| routePoke0/1/2Small* | Pokemon sprite assets | `a/2/4/8` | resolved slot species ids |
| joinPokeLarge* | Pokemon sprite assets | `a/2/5/6` | highest-chance resolved route slot |
| routePoke0/1/2Name | 3DS text rasterization | n/a | species names from send response |
| routeItem0..9Name | 3DS text rasterization | n/a | item names from send response |
| walkPokeName | 3DS text rasterization | n/a | source Pokemon nickname |
| trainerCardName | 3DS text rasterization | n/a | trainer name from save context |
| areaNameSprite | 3DS text rasterization | n/a | selected course name from send response |
| areaSprite | overlay 112 route image table | ARM9 overlay id 112 | `selectedRouteImageIndex` |

## Name Sprites (3DS-generated)

Name sprites are generated on 3DS using local text rasterization (`string_to_img`) and sent as 2bpp patch blocks:

- `walkPokeName`
- `trainerCardName`
- `areaNameSprite`
- `routePoke0Name`, `routePoke1Name`, `routePoke2Name`
- `routeItem0Name` .. `routeItem9Name`

This guarantees exact text output for dynamic content.

## Pokemon Image Sprites (ROM extracted)

ROM asset archives used:

- Small animations: `a/2/4/8` (2 x 32x24 frames, LZ10 payload)
- Large animations: `a/2/5/6` (2 x 64x48 frames, LZ10 payload)

Index convention used by the client:

- Small archive index: direct HGSS species id
- Large archive index: HGSS species id - 1

The extracted frames are remapped with a neutral 2bpp shade map (`{0,1,2,3}`) before patching.

## Route Area Image Extraction

`areaSprite` is extracted from overlay 112, not generated from text.

Current flow:

1. Load/decompress ARM9 overlay 112 from `.nds`.
2. Convert DS pointer table address (`0x021FF528`) to file offset using overlay RAM base.
3. Read pointer table entry `routeImageIndex + 1`.
4. Resolve pointed sprite bytes and copy `0xC0` block (32x24, 2bpp).
5. Patch `areaSprite`.

## Strict Send + Patch Flow

1. 3DS reads `.sav` send context (species, moves, trainer identity, stats).
2. 3DS loads route table from cache or extracts from overlay 112.
3. 3DS builds full `/api/v1/stroll/send` payload with `resolvedRouteConfig` (including `advantagedTypes`).
4. Backend validates and applies resolved route config into EEPROM.
5. 3DS uses response fields (`configuredRouteSlots`, `configuredRouteItems`, `selectedRouteImageIndex`) to select sprite targets and names.
6. 3DS patches dynamic image/text blocks through `/api/v2/stroll/sprite-patches`.
7. Non-patched EEPROM data remains untouched.

## Confirmed vs Inferred

Confirmed in this repository:

- EEPROM dynamic sprite offsets/sizes listed above.
- 3DS extraction paths for Pokemon sprites and route area sprite.
- strict `/api/v1/stroll/send` requirement for `resolvedRouteConfig`.
- inclusion and backend validation of `advantagedTypes` length/value bounds.

Still inferred or not consumed yet:

- semantic use of `advantagedTypes` beyond metadata/traceability in current bridge behavior.
- exact gameplay meaning of all route slot flag bits beyond fields already serialized.
