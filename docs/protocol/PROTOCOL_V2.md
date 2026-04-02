# WearWalker Bridge Protocol (v1 + v2)

Status: Draft, implemented incrementally.

## Goal

The current protocol has two coordinated parts:

1. `POST /api/v1/stroll/send` for semantic stroll setup.
2. `POST /api/v2/stroll/sprite-patches` for non-destructive sprite block updates.

The important architectural change is now **strict 3DS ROM-first route resolution**:

- The 3DS is the only side that parses HGSS `.nds` route tables.
- The 3DS sends the resolved route payload (`resolvedRouteConfig`) in each send request.
- The Python backend does not read `.nds` and does not choose route slots/items/encounters.

## Transport

- Protocol: HTTP/1.1
- Payload type: `application/json`
- Paths used here:
  - `v1`: semantic stroll send
  - `v2`: sprite block patching

## Strict Send Endpoint

### POST /api/v1/stroll/send

Purpose:

- Start a stroll session with dynamic data from `.sav` plus route data resolved on-device from `.nds`.
- Apply (not derive) resolved slots/items/route image to EEPROM.

### Required request shape

`resolvedRouteConfig` is required.

If it is missing, the server returns:

```json
{
  "error": "missing_resolved_route_config",
  "message": "resolvedRouteConfig is required; route/items/encounters must be resolved by 3DS"
}
```

### Request example

```json
{
  "speciesId": 157,
  "level": 36,
  "courseId": 1,
  "nickname": "TYPHLOSION",
  "friendship": 120,
  "heldItem": 0,
  "moves": [53, 52, 46, 33],
  "variantFlags": 0,
  "specialFlags": 0,
  "clearBuffers": true,
  "allowLockedCourse": false,
  "resolvedRouteConfig": {
    "schemaVersion": 1,
    "romSize": 134217728,
    "romMtime": 1712064000,
    "routeImageIndex": 1,
    "routeSeed": 3311798421,
    "advantagedTypes": [11, 15, 10],
    "slots": [
      {
        "slot": 0,
        "sourcePairIndex": 0,
        "speciesId": 115,
        "level": 8,
        "gender": 1,
        "moves": [4, 43, 252, 0],
        "minSteps": 3000,
        "chance": 50
      },
      {
        "slot": 1,
        "sourcePairIndex": 2,
        "speciesId": 29,
        "level": 5,
        "gender": 1,
        "moves": [45, 10, 0, 0],
        "minSteps": 500,
        "chance": 75
      },
      {
        "slot": 2,
        "sourcePairIndex": 4,
        "speciesId": 43,
        "level": 5,
        "gender": 0,
        "moves": [33, 28, 0, 0],
        "minSteps": 0,
        "chance": 100
      }
    ],
    "items": [
      {"routeItemIndex": 0, "itemId": 28, "minSteps": 2500, "chance": 20},
      {"routeItemIndex": 1, "itemId": 27, "minSteps": 2000, "chance": 20},
      {"routeItemIndex": 2, "itemId": 19, "minSteps": 1000, "chance": 30},
      {"routeItemIndex": 3, "itemId": 20, "minSteps": 900, "chance": 30},
      {"routeItemIndex": 4, "itemId": 150, "minSteps": 800, "chance": 30},
      {"routeItemIndex": 5, "itemId": 21, "minSteps": 700, "chance": 40},
      {"routeItemIndex": 6, "itemId": 149, "minSteps": 600, "chance": 50},
      {"routeItemIndex": 7, "itemId": 22, "minSteps": 500, "chance": 50},
      {"routeItemIndex": 8, "itemId": 155, "minSteps": 300, "chance": 50},
      {"routeItemIndex": 9, "itemId": 17, "minSteps": 0, "chance": 100}
    ]
  }
}
```

### Validation summary

- `resolvedRouteConfig` required.
- `schemaVersion >= 1`.
- `romSize` currently validated as `u32` by backend model (`0..0xFFFFFFFF`).
- `romMtime` currently validated as `u64` by backend model (`0..0xFFFFFFFFFFFFFFFF`).
- `routeImageIndex` is `u8`.
- `advantagedTypes` length must be exactly 3 (`u8` values).
- `slots` length must be exactly 3 with unique slot indexes `0..2`.
- `items` length must be exactly 10 with unique indexes `0..9`.
- Unknown extra fields are currently ignored by backend model parsing (for example `routeSeed`).

### Response additions

When resolved config is used, response includes:

- `resolvedRouteSource: "3ds-local"`
- `resolvedRouteMeta` with at least:
  - `schemaVersion`
  - `romSize`
  - `romMtime`
  - `advantagedTypes`

## Data Provenance For Strict Send

This section documents where each `resolvedRouteConfig` field comes from.

### Route table source

From HGSS overlay 112 route table (`overlay9_112`) at DS virtual address `0x021F4138`.

This layout is consistent with reverse-engineering notes in:

- `Pokewalker hacking - Dmitry.GR` (`DS-side things` section)

Route record layout (`0xC0` bytes):

```c
struct RouteInfo {
    uint32_t wattsToUnlock;         // +0x00
    uint32_t imageIdx;              // +0x04
    struct RoutePokeInfo pokes[6];  // +0x08
    struct RouteItemInfo items[10]; // +0x80
    uint8_t advantagedTypes[3];     // +0xBC
    uint8_t padding;                // +0xBF
};
```

`advantagedTypes` indexes map to type names via `mTypeNames` in the same reverse notes:

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

### Mapping used by the 3DS payload builder

- `resolvedRouteConfig.routeImageIndex`
  - from `imageIdx - 1` (`+0x04`, little-endian)
- `resolvedRouteConfig.routeSeed`
  - generated on 3DS (system tick seed) and used for local pair selection in `pokes[6]`
  - currently informational for backend (ignored by model parsing)
- `resolvedRouteConfig.advantagedTypes[0..2]`
  - from `+0xBC..+0xBE`
- `resolvedRouteConfig.slots[*]`
  - selected from `pokes[6]` (`+0x08`, stride `0x14`)
  - one entry per pair `(0/1), (2/3), (4/5)`
- slot fields
  - `speciesId`: `+0x00` (`u16`)
  - `level`: `+0x02` (`u8`)
  - `gender`: flags byte at `+0x07` (low bit indicates female in reverse notes)
  - `moves[4]`: `+0x08..+0x0F`
  - `minSteps`: `+0x10` (`u16`)
  - `chance`: `+0x12` (`u8` chance + `u8` padding in reverse layout; client reads LE16 and clamps to `u8`)
- `resolvedRouteConfig.items[*]`
  - from `items[10]` (`+0x80`, stride `0x06`)
  - `itemId`: `+0x00`
  - `minSteps`: `+0x02`
  - `chance`: `+0x04` (`u8` chance + `u8` padding in reverse layout; client reads LE16 and clamps to `u8`)

### Dynamic fields from save

The following are still read per-send from `.sav` and are not cached from ROM:

- walking species/level/moves/friendship
- trainer identity values
- Pokewalker stats mirrored from save context

## 3DS SD Cache Behavior

To avoid parsing `.nds` every send, the 3DS stores route table cache at:

- `sdmc:/3ds/wearwalker_bridge/rom_course_cache.bin`

Current cache invalidation key:

- ROM file size
- ROM file mtime

Behavior:

1. On send, if cache matches current ROM identity, reuse cache.
2. Otherwise parse overlay 112 and rewrite cache.
3. `.sav` data is still re-read every send.

## Sprite Patch Endpoint (v2)

### POST /api/v2/stroll/sprite-patches

Purpose:

- Non-destructively patch only listed dynamic sprite blocks.

Request shape:

```json
{
  "patches": [
    {
      "key": "walkPokeName",
      "dataHex": "001122..."
    }
  ]
}
```

Validation rules:

- patch list length: `1..32`
- key length: `1..64`
- `dataHex` length: `2..16384`
- hex must be valid and even length
- decoded bytes must match exact block size for key

Allowed keys include:

- `areaSprite`, `areaNameSprite`, `trainerCardName`
- `walkPokeSmall*`, `walkPokeLarge*`, `joinPokeLarge*`, `walkPokeName`
- `routePoke*Small*`, `routePoke*Name`
- `routeItem0Name..routeItem9Name`

Non-destructive guarantee:

- only the exact offsets for requested keys are overwritten
- all other EEPROM ranges remain unchanged

## Current End-to-End Sequence

1. User selects `.sav` and `.nds` on 3DS.
2. 3DS loads or rebuilds ROM route cache from overlay 112.
3. 3DS reads dynamic send context from `.sav`.
4. 3DS sends strict `/api/v1/stroll/send` with `resolvedRouteConfig`.
5. Backend applies resolved slots/items/route image into EEPROM.
6. 3DS generates/loads sprite blocks and patches through `/api/v2/stroll/sprite-patches`.
7. 3DS applies resulting send mutation back into `.sav`.

## Versioning

- Keep semantic send under `v1` path for compatibility with existing flow.
- Keep sprite block patching under `v2`.
- If strict send schema requires a breaking change, increment schema (`resolvedRouteConfig.schemaVersion`) and document the migration path.
