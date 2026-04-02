# WearWalker Bridge WiFi Protocol v2

Status: Draft (implemented incrementally on top of v1)

## Goal

Add a non-destructive way to update dynamic Pokewalker sprite blocks without replacing the full EEPROM image.

v2 is an additive extension:
- v1 endpoints remain valid and unchanged.
- v2 introduces one new endpoint for block-level sprite updates.

## Compatibility Model

The bridge keeps the existing v1 stroll flow:
1. Send core stroll data to /api/v1/stroll/send.
2. Apply dynamic sprite blocks with /api/v2/stroll/sprite-patches.

This split avoids breaking existing clients and keeps v1 behavior stable.

## Transport

- Protocol: HTTP/1.1
- Payload type: application/json
- Base path for this extension: /api/v2

## Endpoint

### POST /api/v2/stroll/sprite-patches

Purpose:
- Patch only selected dynamic sprite blocks in EEPROM.
- Preserve every non-targeted block exactly as-is.

Request body:

{
  "patches": [
    {
      "key": "walkPokeName",
      "dataHex": "001122..."
    }
  ]
}

Validation rules:
- patches list length: 1..32
- key length: 1..64
- dataHex length: 2..16384
- dataHex must be valid hex and have even length
- dataHex decoded byte count must match the exact block size for the selected key
- unknown keys are rejected

Server error format:

{
  "error": "invalid_sprite_patch",
  "message": "..."
}

Success response:

{
  "status": "ok",
  "patches": {
    "applied": [
      {
        "index": 0,
        "key": "walkPokeName",
        "offset": 39230,
        "size": 320
      }
    ],
    "count": 1
  },
  "routes": {
    "routeInfoOffset": 36608,
    "routeImageIndex": 0,
    "routeCourseName": "Refreshing Field"
  }
}

Notes:
- offset and size are returned for traceability/debugging.
- routes is returned as a convenience snapshot after patching.

## Allowed Patch Keys

These keys are validated against a fixed whitelist and mapped to explicit EEPROM offsets.

| Key | Offset | Size (bytes) | Logical Sprite |
|---|---:|---:|---|
| areaSprite | 0x8FBE | 0x00C0 | Route area image (32x24) |
| areaNameSprite | 0x907E | 0x0140 | Route area name (80x16) |
| trainerCardName | 0x1250 | 0x0140 | Trainer Card name (80x16) |
| walkPokeSmall0 | 0x91BE | 0x00C0 | Walking Pokemon small frame 0 (32x24) |
| walkPokeSmall1 | 0x927E | 0x00C0 | Walking Pokemon small frame 1 (32x24) |
| walkPokeLarge0 | 0x933E | 0x0300 | Walking Pokemon large frame 0 (64x48) |
| walkPokeLarge1 | 0x963E | 0x0300 | Walking Pokemon large frame 1 (64x48) |
| joinPokeLarge0 | 0x9EFE | 0x0300 | Route/join Pokemon large frame 0 (64x48) |
| joinPokeLarge1 | 0xA1FE | 0x0300 | Route/join Pokemon large frame 1 (64x48) |
| walkPokeName | 0x993E | 0x0140 | Walking Pokemon name (80x16) |
| routePoke0Small0 | 0x9A7E | 0x00C0 | Route slot 0 small frame 0 (32x24) |
| routePoke0Small1 | 0x9B3E | 0x00C0 | Route slot 0 small frame 1 (32x24) |
| routePoke1Small0 | 0x9BFE | 0x00C0 | Route slot 1 small frame 0 (32x24) |
| routePoke1Small1 | 0x9CBE | 0x00C0 | Route slot 1 small frame 1 (32x24) |
| routePoke2Small0 | 0x9D7E | 0x00C0 | Route slot 2 small frame 0 (32x24) |
| routePoke2Small1 | 0x9E3E | 0x00C0 | Route slot 2 small frame 1 (32x24) |
| routePoke0Name | 0xA4FE | 0x0140 | Route slot 0 name (80x16) |
| routePoke1Name | 0xA63E | 0x0140 | Route slot 1 name (80x16) |
| routePoke2Name | 0xA77E | 0x0140 | Route slot 2 name (80x16) |
| routeItem0Name | 0xA8BE | 0x0180 | Route item 0 name (96x16) |
| routeItem1Name | 0xAA3E | 0x0180 | Route item 1 name (96x16) |
| routeItem2Name | 0xABBE | 0x0180 | Route item 2 name (96x16) |
| routeItem3Name | 0xAD3E | 0x0180 | Route item 3 name (96x16) |
| routeItem4Name | 0xAEBE | 0x0180 | Route item 4 name (96x16) |
| routeItem5Name | 0xB03E | 0x0180 | Route item 5 name (96x16) |
| routeItem6Name | 0xB1BE | 0x0180 | Route item 6 name (96x16) |
| routeItem7Name | 0xB33E | 0x0180 | Route item 7 name (96x16) |
| routeItem8Name | 0xB4BE | 0x0180 | Route item 8 name (96x16) |
| routeItem9Name | 0xB63E | 0x0180 | Route item 9 name (96x16) |

## Non-Destructive Behavior

The server applies patch data only to the exact ranges identified by key.

Equivalent behavior:
- EEPROM[offset:offset+size] = decoded_patch_data

Everything outside those ranges is untouched. This is the key v2 guarantee for existing EEPROM files.

## Current 3DS Client Behavior (Implemented)

The 3DS app currently uses v2 in this sequence:
1. Select HGSS .sav and HGSS .nds in the send menu.
2. Run /api/v1/stroll/send.
3. Read configuredRouteSlots and configuredRouteItems from the send response.
4. Extract dynamic sprites from ROM (areaSprite from ARM9 overlay id 112 route image table; Pokemon small from a/2/4/8; Pokemon large from a/2/5/6), generate name sprites locally (80x16 or 96x16, 2bpp), and call /api/v2/stroll/sprite-patches.

Currently patched keys from the 3DS client:
- walkPokeSmall0
- walkPokeSmall1
- walkPokeLarge0
- walkPokeLarge1
- joinPokeLarge0
- joinPokeLarge1
- walkPokeName
- trainerCardName
- routePoke0Small0
- routePoke0Small1
- routePoke1Small0
- routePoke1Small1
- routePoke2Small0
- routePoke2Small1
- areaSprite
- areaNameSprite
- routePoke0Name
- routePoke1Name
- routePoke2Name
- routeItem0Name
- routeItem1Name
- routeItem2Name
- routeItem3Name
- routeItem4Name
- routeItem5Name
- routeItem6Name
- routeItem7Name
- routeItem8Name
- routeItem9Name

Data sources used by the client:
- walkPokeSmall* and routePoke*Small*: species sprites from HG/SS NARC a/2/4/8
- walkPokeLarge*: species sprites from HG/SS NARC a/2/5/6
- joinPokeLarge*: species sprite from HG/SS NARC a/2/5/6 using configuredRouteSlots highest-chance species
- areaSprite: route area image from HG/SS ARM9 overlay id 112 route image table (selectedRouteImageIndex)
- walkPokeName: nickname from HGSS send context
- trainerCardName: trainer name from HGSS send context
- areaNameSprite: selectedCourseName from /api/v1/stroll/send response
- routePoke{0,1,2}Name: configuredRouteSlots[*].speciesName from /api/v1/stroll/send response
- routeItem{0..9}Name: configuredRouteItems[*].itemName from /api/v1/stroll/send response

## Versioning

- Keep v1 endpoints under /api/v1.
- Additive sprite patch protocol is under /api/v2.
- Any future breaking schema change for sprite patching must move to /api/v3.
