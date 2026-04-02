# Obtaining Dynamic Pokewalker Sprites

## Scope

This document explains:
- how dynamic sprite addresses are determined,
- where each sprite comes from (name, small Pokemon, large Pokemon, route area),
- and how route-specific sprites are selected.

It also distinguishes what is implemented now versus what is planned.

It also separates confirmed facts from current inferences when ROM origin is not fully traced.

## Current Implementation Status

Implemented now:
- Name sprites are generated directly on 3DS and patched through /api/v2/stroll/sprite-patches.
- This includes walking Pokemon name, trainer card name, route area name, the 3 encounter Pokemon names, and the 10 route item-name sprites.
- Pokemon animated sprites are extracted from ROM and patched through /api/v2/stroll/sprite-patches.
- This includes walking Pokemon small/large frames, the 3 route encounter small-frame pairs, and the route/join large-frame pair.

Implemented now (route profile semantics):
- Per-course route image mapping and per-course route item tables are loaded from HGSS overlay data (not inferred from course id).
- send flow returns both selectedCourseId and selectedRouteImageIndex (they can differ).
- send flow returns configuredRouteItems (itemId/itemName/minSteps/chance for 10 entries).

Planned next (offsets and protocol already prepared):
- Route area image sprite.

Design rule in both stages:
- If a block is not explicitly updated, it must remain unchanged in EEPROM.

## Confirmed vs Inferred

Confirmed from code and references in this repository:
- EEPROM offsets and sizes listed below are stable and validated by multiple tools.
- Pokemon animated sprite extraction workflow exists for HG/SS NARC paths a/2/4/8 (small) and a/2/5/6 (large).
- Name sprites can be generated on 3DS and written as valid 2bpp blocks.

Inferred or not yet fully mapped in this repository:
- Exact HG/SS ROM path used by the original game for course area image tiles (areaSprite).
- Whether the retail game pre-bakes some course name sprites or always rasterizes them at transfer time.

## How We Know The Addresses

We use two independent references, then mirror them in bridge constants:

1. Structural source of truth:
- pokewalker-eeprom-editor/src/pokewalker/spec.ts
- The format Struct defines field order and dimensions, so offsets are deterministic.

2. Runtime cross-check source:
- PokewalkerUtils/pokewalker_flask.py
- pngMap/gifMap uses explicit offsets used by live renderer utilities.

Bridge constants are stored in:
- scripts/eeprom_common.py (SPRITE_PATCH_LAYOUT and related constants)

Canonical route-profile data source is stored in:
- pokeheartgold/asm/overlay_112.s, table block between labels ov112_021F4138 and ov112_021F5578.
- scripts/eeprom_common.py parses this block in _load_hgss_course_profiles().

Example derivation:
- routeInfo starts at 0x8F00 and has size 0x00BD.
- Next byte is 0x8FBE, so areaSprite starts at 0x8FBE.

Size formula for 2bpp sprites:
- bytes = width * height / 4
- 80x16 = 320 bytes (0x140)
- 96x16 = 384 bytes (0x180)
- 32x24 = 192 bytes (0x0C0)
- 64x48 = 768 bytes (0x300)

## Canonical Source For Route Profiles

The route-profile source used by the backend is HGSS overlay data:
- File: pokeheartgold/asm/overlay_112.s
- Table start label: ov112_021F4138
- Table end anchor used by parser: ov112_021F5578
- Record size: 0xC0 bytes per course
- Record count used: 27 courses (same as COURSE_NAMES)

Current parser implementation:
- scripts/eeprom_common.py:_extract_asm_byte_block(...)
- scripts/eeprom_common.py:_load_hgss_course_profiles(...)

Per-course record fields currently consumed by the bridge:
- +0x04..+0x07 (u32 LE): route image selector encoded as routeImageIndex + 1
- +0x80..+0xBB: route item table (10 entries, 6 bytes each)

Route item entry layout at +0x80 + index * 6:
- +0x00..+0x01 (u16 LE): itemId
- +0x02..+0x03 (u16 LE): minSteps
- +0x04..+0x05 (u16 LE): chance (bridge clamps to u8 for EEPROM write)

Why this matters:
- selectedCourseId and routeImageIndex are not always equal.
- Using this table removes guesswork and follows the same source used by game logic.

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

### Additional known large route block (documented)

This block exists in layout references and utility renderers:
- join/route large frame block starts at 0x9EFE (2 frames, each 0x300)
- frame 0: 0x9EFE
- frame 1: 0xA1FE

Reference names:
- spec.ts: joinPokeAnimatedSprite
- pokewalker_flask.py gifMap: route_pokemon_big

## Where Each Sprite Comes From

### Source map by block type

| Block family | Source in our pipeline | ROM path currently used | Selection rule |
|---|---|---|---|
| walkPokeSmall* | Pokemon sprite assets | a/2/4/8 | walking species id |
| walkPokeLarge* | Pokemon sprite assets | a/2/5/6 | walking species id |
| routePoke0/1/2Small* | Pokemon sprite assets | a/2/4/8 | configuredRouteSlots species ids |
| joinPokeLarge* | Pokemon sprite assets | a/2/5/6 | configuredRouteSlots highest-chance species |
| routePoke0/1/2Name | Generated text sprite | none (generated) | configuredRouteSlots speciesName |
| routeItem0..9Name | Generated text sprite | none (generated) | configuredRouteItems itemName |
| walkPokeName | Generated text sprite | none (generated) | HGSS nickname/source Pokemon name |
| trainerCardName | Generated text sprite | none (generated) | HGSS trainer name |
| areaNameSprite | Generated text sprite | none (generated) | selectedCourseName |
| areaSprite | ARM9 overlay route image table | ARM9 overlay id 112 (table at 0x021FF528) | selectedRouteImageIndex |

Important clarification about route sprites:
- Route encounter Pokemon sprites come from the same Pokemon sprite families as walking Pokemon (a/2/4/8 for small, a/2/5/6 for large-capable mappings), because they are still species sprites.
- areaSprite (the 32x24 course card image) is resolved from HG/SS ARM9 overlay data, using the route image pointer table and selectedRouteImageIndex.

## 1) Name sprites (implemented)

Generated on 3DS using local text rasterization:
- source function: source/utils.c -> string_to_img(...)
- output format: 2bpp tile buffer ready for EEPROM block write

Current text sources:
- walkPokeName: HGSS send context nickname
- areaNameSprite: selectedCourseName from /api/v1/stroll/send response
- routePoke{0,1,2}Name: configuredRouteSlots[*].speciesName from /api/v1/stroll/send response
- routeItem{0..9}Name: configuredRouteItems[*].itemName from /api/v1/stroll/send response

Why we generate them:
- These are dynamic values in real gameplay (especially Pokemon nickname and per-send route composition).
- Generating on device guarantees we can always render the exact text we are sending.

## 2) Pokemon image sprites (small/large)

ROM asset source:
- HG/SS ROM NARC package a/2/4/8 for small animated species sprites (2x32x24 frames)
- HG/SS ROM NARC package a/2/5/6 for large animated species sprites (2x64x48 frames)
- tool reference: Pokewalker-Scripts/dump-sprites.py
- data is LZ10-compressed per file, decompressed before conversion

These asset sources are used for:
- walking Pokemon animated sprites,
- route encounter Pokemon animated sprites (slot 0/1/2),
- and the route/join large animated block (joinPokeLarge0/1).

Important mapping note for this bridge implementation:
- small archive (a/2/4/8): file index uses direct HGSS species id
- large archive (a/2/5/6): file index uses HGSS species id - 1

Extraction helper command:

```bash
python3 /root/emulated_pokewalker/Pokewalker-Scripts/dump-sprites.py /path/to/a_2_4_8.narc
# or
python3 /root/emulated_pokewalker/Pokewalker-Scripts/dump-sprites.py /path/to/a_2_5_6.narc
```

This produces PNG previews for each sprite entry and is used to validate index mapping.

## 3) Route sprites and route names/images

Route encounter species source:
- Backend chooses route slots in scripts/eeprom_common.py via _configure_route_from_course(...)
- Species data comes from encounter_walker4 tables (or COURSE_SPECIES fallback)

Route image source:
- Backend resolves routeImageIndex per course from HGSS overlay_112 profile data.
- routeImageIndex written to EEPROM may differ from selectedCourseId.

Route item table source:
- Backend loads per-course itemId/minSteps/chance from HGSS overlay_112 profile data.
- Backend writes the selected course item table into EEPROM during send/start.
- Backend returns the same table as configuredRouteItems in send response.

That means route sprite selection is deterministic from:
- selected course id,
- configured route slot species ids returned by /api/v1/stroll/send.

For route encounter Pokemon animated sprites specifically:
- species id -> sprite file index mapping follows the same Pokemon sprite index convention used by a/2/4/8 and a/2/5/6 workflows.
- therefore route Pokemon images are sourced from Pokemon sprite assets, not from a separate "route Pokemon" archive.

Route textual data source:
- route area name: selectedCourseName
- route Pokemon names: configuredRouteSlots[*].speciesName
- route item names: configuredRouteItems[*].itemName

Route item display name dictionary source:
- pokewalker-eeprom-editor/src/pokewalker/types/items.ts
- parsed by scripts/eeprom_common.py:_load_item_names()

Route area image source:
- areaSprite block (0x8FBE) is the route card image block in EEPROM.
- In this bridge implementation, the 3DS client extracts this sprite directly from the selected HG/SS .nds.
- Source is ARM9 overlay id 112 route image pointer table (runtime address 0x021FF528).
- selectedRouteImageIndex (0..7) maps to table entry index selectedRouteImageIndex + 1; each entry points to a 0xC0 32x24 2bpp block copied to areaSprite.

## Does The Original Game Generate Text Sprites?

Short answer:
- It uses both pre-existing assets and generated text, depending on sprite type.

What is clearly pre-existing:
- Pokemon animation assets (small/large frames) exist in ROM assets (a/2/4/8 and a/2/5/6 workflows).
- Many fixed UI graphics are static assets.

What is very likely generated at send time:
- walkPokeName and routePokeNameSprites (nickname/species text can vary per send and per language).
- areaNameSprite can also be produced from the selected course name string.

Why this is the most plausible model:
- EEPROM stores both text fields and rendered text sprite blocks for the same logical names.
- Dynamic nickname content is not practical to store as fully pre-baked sprites for all possibilities.
- The bridge reproduces this behavior by rasterizing text into the exact 2bpp target buffers.

## Item Probabilities And EEPROM Item Table

This section documents exactly where item probabilities come from and what is written to EEPROM.

Source of item probabilities and thresholds:
- HGSS overlay profile table (overlay_112) per course, per item entry.
- item chance is read from each 6-byte item entry and written to route item chance bytes.

Related note about Pokemon encounter chances:
- Route Pokemon slot chances (configuredRouteSlots) continue to come from COURSE_SLOT_PROFILES / encounter logic.
- Route item chances (configuredRouteItems) come from overlay_112 item entries.

EEPROM destinations used by the bridge:
- route item ids: ROUTE_INFO + 140 (0x8F8C), 10 entries, u16 each
- route item min steps: ROUTE_INFO + 160 (0x8FA0), 10 entries, u16 each
- route item chance: ROUTE_INFO + 180 (0x8FB4), 10 entries, u8 each

Write path on send/start:
- scripts/eeprom_common.py:_configure_route_items_from_course(...)
- then _write_route_item(...) writes itemId/minSteps/chance to EEPROM offsets above.

Read path for dowsing rolls:
- scripts/eeprom_common.py:_roll_dowsed_items(...)
- uses ROUTE_ITEM_IDS_OFFSET / ROUTE_ITEM_MIN_STEPS_OFFSET / ROUTE_ITEM_CHANCE_OFFSET.

What is returned to API clients:
- /api/v1/stroll/send includes configuredRouteItems with fields:
  - routeItemIndex
  - itemId
  - itemName
  - minSteps
  - chance

## Current Image Update Flow

With current ROM extraction wiring in the 3DS app, the flow is:

1. Read selected .sav and .nds.
2. Call /api/v1/stroll/send and obtain configuredRouteSlots, configuredRouteItems, selectedCourseName, and selectedRouteImageIndex.
3. Resolve species ids:
- walking species from HGSS send context
- route species from configuredRouteSlots[0..2]
- join large species from configuredRouteSlots highest chance
4. Resolve route item names from configuredRouteItems[0..9].
5. Extract/convert image frames from ROM assets and rasterize text sprites.
6. Patch sprite blocks through /api/v2/stroll/sprite-patches using exact key/size matches.
   - Pokemon images: walkPoke*, routePoke*Small*, joinPokeLarge*
   - Text sprites: walkPokeName, trainerCardName, areaNameSprite, routePoke*Name, routeItem*Name
7. Keep every non-patched block unchanged.

This preserves existing EEPROM visual data while allowing deterministic dynamic updates.
