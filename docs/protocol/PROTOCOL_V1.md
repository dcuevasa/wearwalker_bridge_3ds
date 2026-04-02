# WearWalker Bridge WiFi Protocol v1

Status: Draft (implemented by FastAPI mock server and consumed by 3DS client)

## Goal

Define a stable and testable HTTP contract between wearwalker_bridge_3ds and a WearWalker-compatible backend.

This v1 protocol is intentionally aligned with the current 3DS client behavior in source/wearwalker_api.c.

## Transport

- Protocol: HTTP/1.1
- Payloads:
  - JSON for metadata/status endpoints
  - application/octet-stream for EEPROM transfer
- Base path: /api/v1
- Test UI (mock backend only): /docs (FastAPI Swagger)

## Endpoints

### GET /api/v1/bridge/status

Purpose:
- Health check for transport and backend readiness.

Response 200 example:

{
  "status": "ok",
  "apiVersion": "v1",
  "backend": "python-mock",
  "connected": true,
  "eepromPath": "/abs/path/mock_eeprom.bin",
  "eepromSize": 65536,
  "uptimeSeconds": 42,
  "trainerName": "WWBRIDGE"
}

Notes:
- 3DS currently logs this JSON but does not require specific fields.

### GET /api/v1/device/snapshot

Purpose:
- Return core state for UI display: trainer, steps, watts.

Response 200 example:

{
  "trainerName": "WWBRIDGE",
  "trainer": "WWBRIDGE",
  "steps": 1234,
  "watts": 500,
  "protocolVersion": 1,
  "protocolSubVersion": 0,
  "lastSyncEpochSeconds": 0
}

Required compatibility keys:
- trainerName or trainer
- steps
- watts

Notes:
- 3DS parser accepts trainerName first, then fallback trainer.

### GET /api/v1/eeprom/export

Purpose:
- Download full EEPROM binary.

Response:
- 200 OK
- Content-Type: application/octet-stream
- Content-Length: 65536
- Body: exact EEPROM bytes

Client expectations:
- Non-chunked body
- Exact size 65536 bytes

### PUT /api/v1/eeprom/import

Purpose:
- Upload full EEPROM binary.

Request:
- Content-Type: application/octet-stream
- Content-Length: 65536
- Body: exact EEPROM bytes

Response 200 example:

{
  "status": "ok",
  "importedBytes": 65536
}

Validation rules:
- Reject if Content-Length missing (411)
- Reject if Content-Type invalid (415)
- Reject if payload size != 65536 (400)

### POST /api/v1/device/commands/set-steps

Purpose:
- Command-style mutation endpoint for testing clients that emulate 3DS behavior.

Request example:

{
  "steps": 12345
}

Response 200 example:

{
  "status": "ok",
  "snapshot": {
    "trainerName": "WWBRIDGE",
    "steps": 12345,
    "watts": 500
  }
}

### POST /api/v1/device/commands/set-watts

Request example:

{
  "watts": 777
}

### POST /api/v1/device/commands/set-trainer

Request example:

{
  "name": "RED"
}

### POST /api/v1/device/commands/set-sync

Request example:

{
  "epoch": 1710000000
}

Notes:
- These command endpoints are test-focused and optional for strict 3DS compatibility.
- 3DS compatibility still relies on status/snapshot/export/import endpoints.

### Expanded semantic v1 domain endpoints

The mock backend now also exposes semantic domain routes under the same v1 namespace.
These are additive and do not replace existing v1 compatibility endpoints.

Read endpoints:
- GET /api/v1/device/domains
- GET /api/v1/device/identity
- GET /api/v1/device/stats
- GET /api/v1/device/stroll
- GET /api/v1/device/inventory
- GET /api/v1/device/journal?preview=N
- GET /api/v1/device/routes

Mutation endpoints:
- PATCH /api/v1/device/identity
- PATCH /api/v1/device/stats
- PATCH /api/v1/device/stroll
- POST /api/v1/device/inventory/dowsed/{slot}
- POST /api/v1/device/inventory/gifted/{slot}
- POST /api/v1/device/inventory/caught/{slot}
- POST /api/v1/device/journal/clear

Identity patch payload example:

{
  "trainerName": "APIA",
  "protocolVersion": 2,
  "protocolSubVersion": 1,
  "lastSyncEpochSeconds": 1710000000,
  "stepCount": 12345
}

Stats patch payload example:

{
  "steps": 2222,
  "lifetimeSteps": 2222,
  "todaySteps": 1200,
  "watts": 321,
  "lastSyncEpochSeconds": 1710000000,
  "stepHistory": [0, 0, 0, 0, 0, 0, 0]
}

Stroll patch payload example:

{
  "sessionWatts": 322,
  "routeImageIndex": 7
}

Inventory payload examples:

{
  "itemId": 25
}

{
  "speciesId": 150
}

### Bridge decoupling endpoints (watch <-> backend <-> 3DS)

To prepare migration away from direct watch/3DS coupling, v1 now includes explicit sync-package
and operation-batch endpoints. These allow each side to exchange semantic updates while the
backend remains the EEPROM source of truth.

- GET /api/v1/sync/package
- POST /api/v1/sync/apply
- POST /api/v1/sync/apply-operations

Sync package response example:

{
  "schema": "wearwalker-sync-v1",
  "generatedAtEpochSeconds": 1710000000,
  "domains": {
    "identity": { "trainerName": "APIA" },
    "stats": { "steps": 2222, "watts": 322 },
    "stroll": { "sessionWatts": 322 },
    "inventory": { "caught": [], "dowsedItems": [], "giftedItems": [] },
    "journal": { "entryCount": 24 },
    "routes": { "routeInfoOffset": 36608 }
  }
}

Sync apply request example:

{
  "package": {
    "schema": "wearwalker-sync-v1",
    "domains": {
      "stats": { "steps": 5000, "watts": 600 },
      "inventory": {
        "dowsedItems": [{ "slot": 0, "itemId": 25 }]
      }
    }
  }
}

Operations batch request example:

{
  "operations": [
    { "op": "stroll-tick", "steps": 200 },
    { "op": "add-caught-species", "speciesId": 150 },
    { "op": "add-dowsed-item", "itemId": 25 }
  ]
}

Supported operation names:
- set-steps, set-watts, set-trainer, set-sync
- patch-identity, patch-stats, patch-stroll
- set-dowsed-item, set-gifted-item, set-caught-species
- add-caught-species, add-dowsed-item, add-gifted-item
- stroll-tick
- clear-journal, clear-stroll-buffers
- apply-sync-package

### Stroll lifecycle helper endpoints

- POST /api/v1/stroll/tick
- POST /api/v1/stroll/catch
- POST /api/v1/stroll/dowsing
- POST /api/v1/stroll/peer-gift
- POST /api/v1/stroll/reset-buffers

tick request:

{
  "steps": 200
}

catch request:

{
  "speciesId": 150,
  "replaceSlot": 1
}

dowsing/peer-gift request:

{
  "itemId": 25,
  "replaceSlot": 0
}

reset request:

{
  "clearCaught": true,
  "clearDowsed": true,
  "clearGifted": false,
  "clearJournal": false
}

## Error Model

Error responses should be JSON:

{
  "error": "error_code",
  "message": "human readable message"
}

Suggested status mapping:
- 400 bad request (size mismatch, malformed input)
- 404 unknown endpoint
- 405 method not allowed
- 411 missing content-length
- 415 unsupported media type
- 500 internal processing error

## Timeouts and Reliability

3DS-side defaults:
- Request timeout: 4000ms
- Inter-byte timeout: 20ms

Recommendations:
- Keep responses small for JSON endpoints
- Avoid chunked transfer for EEPROM export
- Always close connection after each request

## Versioning

v1 uses path versioning:
- /api/v1/...

Backward-compatible additions:
- Add optional JSON fields only

Breaking changes:
- Must move to /api/v2/...

## Offline Test Strategy

Two Python scripts inside scripts provide decoupled testing:

1) scripts/eeprom_server.py
- Simulates WearWalker backend with /api/v1 routes and command endpoints.
- Uses scripts/test_roms/eeprom.bin by default.

2) scripts/eeprom_client.py
- Manipulates EEPROM locally or against any /api/v1 backend.
- In server mode, mutation commands use command endpoints when available.
- For older backends, mutation commands fall back to export/import.

This allows:
- Testing 3DS without a watch
- Testing EEPROM manipulation without 3DS
