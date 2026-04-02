# pwalkerHax

An hacking tool for the Pokewalker written as a 3DS homebrew application that uses the built-in infrared transceiver to communicate with the Pokewalker.

You might also be interested in my other project [**RtcPwalker**](https://github.com/francesco265/RtcPwalker).
[Here](https://youtu.be/f6f8RSxqG20) you can find a demo of my two projects in action (**pwalkerHax** + **RtcPwalker**).


## Current Features

At the moment, the following features are implemented or planned:
- [x] **Add watts**
- [x] **Set today steps**
- [x] **Set total steps to 9.999.999**
- [x] **Gift an event item**
- [x] **Gift an event Pokemon**
- [x] **Dump ROM**
- [x] **Dump EEPROM**


Feel free to open an issue if you have any suggestions or requests for new features.

When connecting to the Pokewalker, make sure to point it towards the 3DS's infrared sensor, which is the black spot on the back side of the device, and keep the two devices close to each other.

### Commands

| Command    | Action                |
|------------|-----------------------|
| Up/Down    | Move selection        |
| Left/Right | Move selection faster |
| A          | Select                |
| B          | Cancel                |
| Y          | Go to item number     |

## Installation

Just download the latest [release](https://github.com/francesco265/pwalkerHax/releases) as a `.3dsx` file and execute it using the homebrew launcher.

This repository can now also build a `.cia` package for install-based testing.

## How to build

The only requirement to build this project is to have the [libctru](https://github.com/devkitPro/libctru) library installed on your system.

Build `.3dsx`:

```sh
make
```

Build `.cia` (requires `bannertool` + `makerom` and local `banner.png` + `audio.wav` assets):

```sh
make cia
```

## WiFi protocol and offline testing

This repository now includes a first WiFi protocol draft and two Python tools to test each side independently.

Current 3DS UI now exposes four practical menus: API test, HGSS sync/patch, HGSS stroll send-from-box, and HGSS stroll return-to-save.

- Protocol spec: docs/protocol/PROTOCOL_V1.md
- Mock backend (no watch required): scripts/eeprom_server.py
- EEPROM manipulator (no 3DS required): scripts/eeprom_client.py

### Start mock backend

From the repository root:

```sh
python3 -m pip install -r requirements.txt
python3 scripts/eeprom_server.py --host 127.0.0.1 --port 8080
```

For real 3DS LAN testing, bind the server on all interfaces and use your PC LAN IP in the 3DS menu:

```sh
python3 scripts/eeprom_server.py --host 0.0.0.0 --port 8080
```

Open FastAPI interactive docs at:

- http://127.0.0.1:8080/docs

This exposes:

- GET /api/v1/bridge/status
- GET /api/v1/device/snapshot
- GET /api/v1/eeprom/export
- PUT /api/v1/eeprom/import
- GET /api/v1/device/domains
- GET /api/v1/device/identity
- GET /api/v1/device/stats
- GET /api/v1/device/stroll
- GET /api/v1/device/inventory
- GET /api/v1/device/journal
- GET /api/v1/device/routes
- PATCH /api/v1/device/identity
- PATCH /api/v1/device/stats
- PATCH /api/v1/device/stroll
- POST /api/v1/device/inventory/dowsed/{slot}
- POST /api/v1/device/inventory/gifted/{slot}
- POST /api/v1/device/inventory/caught/{slot}
- POST /api/v1/device/journal/clear
- POST /api/v1/stroll/send
- POST /api/v1/stroll/return
- GET /api/v1/stroll/report

### Manipulate EEPROM locally

```sh
python3 scripts/eeprom_client.py snapshot
python3 scripts/eeprom_client.py set-steps 12345
python3 scripts/eeprom_client.py set-watts 500
python3 scripts/eeprom_client.py set-trainer ASH
```

By default, scripts use scripts/test_roms/eeprom.bin.
You can override it with --eeprom /path/to/your/eeprom.bin.

### Manipulate EEPROM via API

```sh
python3 scripts/eeprom_client.py snapshot --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py set-steps 9000 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py set-watts 777 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py set-trainer RED --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py export --server http://127.0.0.1:8080 --output WWEEPROM.bin
python3 scripts/eeprom_client.py import --server http://127.0.0.1:8080 --input WWEEPROM.bin
```

### Semantic domain API testing with CLI

```sh
python3 scripts/eeprom_client.py identity --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stats --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py inventory --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py journal --preview 3 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py routes --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py domains --server http://127.0.0.1:8080

python3 scripts/eeprom_client.py patch-identity --server http://127.0.0.1:8080 --trainer-name APIA --protocol-version 2 --protocol-sub-version 1
python3 scripts/eeprom_client.py patch-stats --server http://127.0.0.1:8080 --steps 2222 --lifetime-steps 2222 --today-steps 1200 --watts 321
python3 scripts/eeprom_client.py patch-stroll --server http://127.0.0.1:8080 --session-watts 322 --route-image-index 7
python3 scripts/eeprom_client.py set-dowsed-item --server http://127.0.0.1:8080 0 25
python3 scripts/eeprom_client.py set-gifted-item --server http://127.0.0.1:8080 1 44
python3 scripts/eeprom_client.py set-caught-species --server http://127.0.0.1:8080 2 150
python3 scripts/eeprom_client.py clear-journal --server http://127.0.0.1:8080
```

### HGSS save fidelity tooling (Pokewalker)

These commands operate directly on HGSS `.sav` files (no watch/3DS required) and use PKHeX-aligned block detection/checksum logic.

```sh
python3 scripts/eeprom_client.py hgss-status --save scripts/test_HGSS_saves/input_hgss.sav

python3 scripts/eeprom_client.py hgss-patch \
	--save scripts/test_HGSS_saves/input_hgss.sav \
	--output scripts/test_HGSS_saves/output_hgss_patched.sav \
	--steps 123456 --watts 321 --unlock-all-courses

python3 scripts/eeprom_client.py hgss-diff \
	--before scripts/test_HGSS_saves/input_hgss.sav \
	--after scripts/test_HGSS_saves/output_hgss_patched.sav

# Full synthetic return from EEPROM capture to HGSS box.
# By default this also syncs EEPROM trip progress into HGSS Pokewalker fields
# (steps/watts/course flags), increments the HGSS trip counter,
# and can append synthetic trip journal events in EEPROM before generating the boxed PK4 capture.
python3 scripts/eeprom_client.py hgss-stroll-box-return \
	--save scripts/test_HGSS_saves/input_hgss.sav \
	--eeprom scripts/test_roms/eeprom.bin \
	--output scripts/test_HGSS_saves/output_hgss_return.sav \
	--box 17 --source-slot 1 --extra-species 16 --caught-slot 2 \
	--walked-steps 2400 --bonus-watts 30
```

Notes for `hgss-stroll-box-return`:

- `--no-sync-eeprom-progress`: capture injection only (skip EEPROM progress sync).
- `--no-trip-journal`: skip synthetic capture/return events in EEPROM journal.
- `--no-sync-trip-counter`: skip incrementing the HGSS Pokewalker trip counter.
- The diary text visible in HGSS is generated from Pokewalker EEPROM event logs at sync time; it is not persisted as diary text inside the HGSS `.sav`.

### Decoupled sync workflow (ready for watch/backend/3DS split)

```sh
# 1) Read current sync package from backend
python3 scripts/eeprom_client.py sync-package --server http://127.0.0.1:8080

# 2) Apply a package produced by another actor (watch, test harness, etc.)
python3 scripts/eeprom_client.py sync-apply --server http://127.0.0.1:8080 --input sync_package.json

# 3) Apply batch operations atomically in order
python3 scripts/eeprom_client.py apply-operations --server http://127.0.0.1:8080 --input ops.json
```

Example operations file (`ops.json`):

```json
{
	"operations": [
		{ "op": "stroll-tick", "steps": 200 },
		{ "op": "add-caught-species", "speciesId": 150 },
		{ "op": "add-dowsed-item", "itemId": 25 }
	]
}
```

### Stroll lifecycle helper commands

```sh
python3 scripts/eeprom_client.py stroll-send 25 --course-id 0 --level 14 --clear-buffers --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-send 25 --course-id 20 --allow-locked-course --unlock-special-courses --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-return 2400 --auto-captures 2 --bonus-watts 30 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-report --server http://127.0.0.1:8080

python3 scripts/eeprom_client.py stroll-tick 200 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-catch 150 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-dowsing 25 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-peer-gift 44 --server http://127.0.0.1:8080
python3 scripts/eeprom_client.py stroll-reset-buffers --server http://127.0.0.1:8080
```

In server mode, set-* commands try command endpoints first and automatically fall back to export/import for older backends.

### 3DS integration test against mock backend

1. Start scripts/eeprom_server.py on your PC.
2. Build 3DS with WiFi backend:

```sh
make USE_WIFI=1
```

3. On 3DS, open the WearWalker API test menu and set host/port to the mock server endpoint.
4. Test status, snapshot, export and import flows.

### HGSS save sync/patch menu (MVP)

The 3DS app now includes a dedicated **HGSS save sync/patch** menu:

- Browse SD folders and select a `.sav` file directly on-device.
- Apply local HGSS Pokewalker field patch manually (steps/watts/course flags).
- Apply local HGSS patch from backend `/api/v1/sync/package` (`stats.steps`, `stats.watts`, `routes.courseUnlocks.unlockFlags`).
- Optional trip-counter increment (maps to HGSS Pokewalker trip counter at `0xE700` in the active General block).

Current MVP patch scope is the Pokewalker progress area + General block checksum refresh.

### HGSS stroll send/return menus on 3DS

Two additional on-device workflows are now available:

- **HGSS -> Stroll Send**
	- Configure backend host/port.
	- Select a real HGSS `.sav` from SD.
	- Choose source **box** and **slot**.
	- Inspect selected slot summary (`species`, `exp`, estimated level).
	- Before send, seed backend EEPROM identity/stats from HGSS save:
		- trainer name + TID/SID from HGSS trainer block,
		- Pokewalker steps/watts from HGSS Pokewalker fields.
	- Choose route/course id and trigger `/api/v1/stroll/send`.
	- The send payload now uses real source-slot fields from `.sav` (`nickname`, `friendship`, `heldItem`, `moves[4]`, `variantFlags`, `specialFlags`) instead of species/level only.
	- On success, patch the selected `.sav` locally by writing the sent source PK4 into the active HGSS walker-pair area and clearing the source slot.

- **Stroll -> HGSS Return**
	- Configure backend host/port.
	- Select HGSS `.sav`, source box/slot, optional target slot (`0 = auto-empty`).
	- Configure walked steps / bonus watts / auto captures.
	- Trigger `/api/v1/stroll/return`, fetch `/api/v1/sync/package`, then patch `.sav` locally:
		- if source slot is empty, restore source PK4 from the HGSS walker-pair area first,
		- source slot EXP/friendship update,
		- optional captured Pokemon insertion into target/auto slot,
		- Pokewalker steps/watts/course flags sync,
		- optional trip counter increment,
		- General + Storage checksum refresh.

### HGSS -> EEPROM data fidelity and legality notes

This project now supports a deterministic HGSS-to-EEPROM send workflow that explicitly documents where each field comes from and why it is considered legal.

#### Validated model (against reverse-engineering notes)

- Full boxed Pokemon data (`PK4`, 136 bytes) remains in HGSS save storage.
- During send, HGSS keeps the full Pokemon in save state (walker-pair/quarantine semantics) while EEPROM receives a compact stroll payload.
- During return, EEPROM trip/capture data is merged back into HGSS source/capture slots.
- This matches the high-level model in `pokemon_storage.md`: EEPROM is a lightweight stroll/session representation, not the authoritative full Pokemon store.

#### Legal data sources used by the implementation

- Pokewalker encounter legality table: `pkhex/PKHeX.Core/Resources/legality/wild/Gen4/encounter_walker4.pkl`
	- Used to configure route slots (species/level/moves/gender) for each course.
- HGSS personal table: `pkhex/PKHeX.Core/Resources/byte/personal/personal_hgss`
	- Used for species-level legality metadata (growth/friendship/ability constraints in related builders).
- HGSS `.sav` source slot PK4
	- Used for walking Pokemon species/level/held item/moves/friendship/nickname and save trainer identity context.

#### What is written to EEPROM in HGSS->send flow

- Identity domain
	- Trainer name from HGSS save trainer block.
	- Trainer TID/SID from HGSS save trainer block.
- Stats domain
	- Steps and watts seeded from HGSS Pokewalker fields.
- Stroll domain
	- Walking companion summary (species/level/held item/moves/friendship/flags/nickname).
	- Selected route/course id and route name.
- Route encounter slots
	- Three configured capture slots (one per route group) with min-steps and chance.

Sprites are currently not regenerated in this flow yet; only semantic EEPROM fields are updated.

#### Generate an EEPROM from HGSS save (local, reproducible)

Use the new command:

```sh
python3 scripts/eeprom_client.py hgss-stroll-box-send \
	--save scripts/test_HGSS_saves/input_hgss.sav \
	--eeprom scripts/test_roms/eeprom.bin \
	--box 17 --source-slot 1 --course-id 0 --clear-buffers \
	--output-eeprom outputs/eeprom_from_hgss_send.bin
```

Verify the resulting EEPROM domains:

```sh
python3 scripts/eeprom_client.py identity --eeprom outputs/eeprom_from_hgss_send.bin
python3 scripts/eeprom_client.py stats --eeprom outputs/eeprom_from_hgss_send.bin
python3 scripts/eeprom_client.py stroll --eeprom outputs/eeprom_from_hgss_send.bin
python3 scripts/eeprom_client.py routes --eeprom outputs/eeprom_from_hgss_send.bin
```

The command output also includes `routeSlotsPreview` (3 entries) and `strollSend.configuredRouteSlots` for quick auditing.

## Credits

This code is just a port of Dmitry's amazing [work](https://dmitry.gr/?r=05.Projects&proj=28.%20pokewalker), for which he originally developed a running demo for PalmOS devices. He reverse-engineered the Pokewalker's protocol and created all the exploits, i just ported his work to the 3DS.

---
If you appreciate my works and want to support me, you can offer me a coffee :coffee::heart:.

- **BTC**: `bc1qvvjndu7mqe9l2ze4sm0eqzkq839m0w6ldms8ex`
- **PayPal**: [![](https://www.paypalobjects.com/en_US/i/btn/btn_donate_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=AAZDH3SM7T9P6)
