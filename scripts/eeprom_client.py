#!/usr/bin/env python3
"""CLI to manipulate Pokewalker EEPROM locally or via mock WearWalker API."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any
from urllib import error, request

from eeprom_common import (
    add_inventory_caught_species,
    add_inventory_dowsed_item,
    add_inventory_gifted_item,
    add_walked_steps,
    apply_semantic_operations,
    apply_sync_package,
    build_sync_package,
    clear_journal,
    clear_stroll_buffers,
    EEPROM_SIZE,
    load_eeprom,
    read_all_domains,
    read_identity_section,
    read_inventory_section,
    read_journal_section,
    read_routes_section,
    read_snapshot,
    read_stats_section,
    read_stroll_section,
    return_pokemon_from_stroll,
    save_eeprom,
    send_pokemon_to_stroll,
    set_identity_section,
    set_inventory_caught_species,
    set_inventory_dowsed_item,
    set_inventory_gifted_item,
    set_last_sync_seconds,
    set_stats_section,
    set_steps,
    set_stroll_section,
    set_trainer_name,
    set_watts,
    stroll_report,
)
from hgss_save import (
    apply_hgss_stroll_box_return,
    diff_hgss_save_files,
    inspect_hgss_save,
    patch_hgss_save,
)


def normalize_base_url(base_url: str) -> str:
    return base_url.rstrip("/")


def http_get_json(base_url: str, route: str, timeout: float = 8.0) -> dict:
    url = normalize_base_url(base_url) + route
    req = request.Request(url, method="GET")
    req.add_header("Accept", "application/json")
    with request.urlopen(req, timeout=timeout) as resp:
        data = resp.read().decode("utf-8")
        return json.loads(data)


def http_get_binary(base_url: str, route: str, timeout: float = 12.0) -> bytes:
    url = normalize_base_url(base_url) + route
    req = request.Request(url, method="GET")
    with request.urlopen(req, timeout=timeout) as resp:
        payload = resp.read()

    if len(payload) != EEPROM_SIZE:
        raise ValueError(f"Expected {EEPROM_SIZE} bytes, got {len(payload)}")
    return payload


def http_put_binary(base_url: str, route: str, payload: bytes, timeout: float = 12.0) -> dict:
    url = normalize_base_url(base_url) + route
    req = request.Request(url, data=payload, method="PUT")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Content-Length", str(len(payload)))

    with request.urlopen(req, timeout=timeout) as resp:
        data = resp.read().decode("utf-8")
        return json.loads(data)


def http_post_json(base_url: str, route: str, payload: dict, timeout: float = 8.0) -> dict:
    url = normalize_base_url(base_url) + route
    encoded = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    req = request.Request(url, data=encoded, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json")
    req.add_header("Content-Length", str(len(encoded)))

    with request.urlopen(req, timeout=timeout) as resp:
        data = resp.read().decode("utf-8")
        return json.loads(data)


def http_patch_json(base_url: str, route: str, payload: dict, timeout: float = 8.0) -> dict:
    url = normalize_base_url(base_url) + route
    encoded = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    req = request.Request(url, data=encoded, method="PATCH")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json")
    req.add_header("Content-Length", str(len(encoded)))

    with request.urlopen(req, timeout=timeout) as resp:
        data = resp.read().decode("utf-8")
        return json.loads(data)


def try_remote_command(base_url: str, route: str, payload: dict) -> dict | None:
    try:
        return http_post_json(base_url, route, payload)
    except error.HTTPError as exc:
        # Keep compatibility with non-command backends by falling back to export/import mutation.
        if exc.code in (404, 405):
            exc.read()
            return None
        raise


def try_remote_patch(base_url: str, route: str, payload: dict) -> dict | None:
    try:
        return http_patch_json(base_url, route, payload)
    except error.HTTPError as exc:
        # Keep compatibility with non-patch backends by falling back to export/import mutation.
        if exc.code in (404, 405):
            exc.read()
            return None
        raise


def load_target_eeprom(args: argparse.Namespace) -> bytearray:
    if args.server:
        payload = http_get_binary(args.server, "/api/v1/eeprom/export")
        return bytearray(payload)
    return load_eeprom(args.eeprom)


def save_target_eeprom(args: argparse.Namespace, eeprom: bytearray) -> None:
    if len(eeprom) != EEPROM_SIZE:
        raise ValueError(f"EEPROM must be exactly {EEPROM_SIZE} bytes")

    if args.server:
        http_put_binary(args.server, "/api/v1/eeprom/import", bytes(eeprom))
        return

    save_eeprom(args.eeprom, eeprom)


def print_json(data: dict) -> None:
    print(json.dumps(data, indent=2, ensure_ascii=True, sort_keys=True))


def load_json_file(path: str) -> dict | list:
    file_path = Path(path).resolve()
    with file_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    return payload


def cmd_status(args: argparse.Namespace) -> int:
    if args.server:
        status = http_get_json(args.server, "/api/v1/bridge/status")
    else:
        eeprom = load_eeprom(args.eeprom)
        status = {
            "status": "ok",
            "apiVersion": "local",
            "connected": False,
            "eepromPath": str(Path(args.eeprom).resolve()),
            "eepromSize": len(eeprom),
        }
    print_json(status)
    return 0


def cmd_snapshot(args: argparse.Namespace) -> int:
    if args.server:
        snapshot = http_get_json(args.server, "/api/v1/device/snapshot")
    else:
        snapshot = read_snapshot(load_eeprom(args.eeprom))

    print_json(snapshot)
    return 0


def cmd_export(args: argparse.Namespace) -> int:
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    if args.server:
        payload = http_get_binary(args.server, "/api/v1/eeprom/export")
    else:
        payload = bytes(load_eeprom(args.eeprom))

    output.write_bytes(payload)
    print(f"EEPROM exported to {output} ({len(payload)} bytes)")
    return 0


def cmd_import(args: argparse.Namespace) -> int:
    input_path = Path(args.input).resolve()
    payload = input_path.read_bytes()
    if len(payload) != EEPROM_SIZE:
        raise ValueError(f"Invalid file size: expected {EEPROM_SIZE}, got {len(payload)}")

    if args.server:
        result = http_put_binary(args.server, "/api/v1/eeprom/import", payload)
        print_json(result)
    else:
        save_eeprom(args.eeprom, bytearray(payload))
        print(f"EEPROM imported into {Path(args.eeprom).resolve()}")

    return 0


def cmd_set_steps(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/device/commands/set-steps",
            {"steps": args.steps},
        )
        if result is not None:
            print_json(result.get("snapshot", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_steps(eeprom, args.steps)
    save_target_eeprom(args, eeprom)
    print_json(read_snapshot(eeprom))
    return 0


def cmd_set_watts(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/device/commands/set-watts",
            {"watts": args.watts},
        )
        if result is not None:
            print_json(result.get("snapshot", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_watts(eeprom, args.watts)
    save_target_eeprom(args, eeprom)
    print_json(read_snapshot(eeprom))
    return 0


def cmd_set_trainer(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/device/commands/set-trainer",
            {"name": args.name},
        )
        if result is not None:
            print_json(result.get("snapshot", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_trainer_name(eeprom, args.name)
    save_target_eeprom(args, eeprom)
    print_json(read_snapshot(eeprom))
    return 0


def cmd_set_sync(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/device/commands/set-sync",
            {"epoch": args.epoch},
        )
        if result is not None:
            print_json(result.get("snapshot", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_last_sync_seconds(eeprom, args.epoch)
    save_target_eeprom(args, eeprom)
    print_json(read_snapshot(eeprom))
    return 0


def cmd_identity(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/device/identity"))
        return 0

    print_json(read_identity_section(load_eeprom(args.eeprom)))
    return 0


def cmd_stats(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/device/stats"))
        return 0

    print_json(read_stats_section(load_eeprom(args.eeprom)))
    return 0


def cmd_stroll(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/device/stroll"))
        return 0

    print_json(read_stroll_section(load_eeprom(args.eeprom)))
    return 0


def cmd_inventory(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/device/inventory"))
        return 0

    print_json(read_inventory_section(load_eeprom(args.eeprom)))
    return 0


def cmd_journal(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, f"/api/v1/device/journal?preview={args.preview}"))
        return 0

    print_json(read_journal_section(load_eeprom(args.eeprom), preview_entries=args.preview))
    return 0


def cmd_routes(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/device/routes"))
        return 0

    print_json(read_routes_section(load_eeprom(args.eeprom)))
    return 0


def cmd_domains(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/device/domains"))
        return 0

    print_json(read_all_domains(load_eeprom(args.eeprom)))
    return 0


def cmd_patch_identity(args: argparse.Namespace) -> int:
    payload: dict[str, int | str] = {}
    if args.trainer_name is not None:
        payload["trainerName"] = args.trainer_name
    if args.protocol_version is not None:
        payload["protocolVersion"] = args.protocol_version
    if args.protocol_sub_version is not None:
        payload["protocolSubVersion"] = args.protocol_sub_version
    if args.last_sync is not None:
        payload["lastSyncEpochSeconds"] = args.last_sync
    if args.step_count is not None:
        payload["stepCount"] = args.step_count

    if not payload:
        raise ValueError("Provide at least one identity field to patch")

    if args.server:
        result = try_remote_patch(args.server, "/api/v1/device/identity", payload)
        if result is not None:
            print_json(result.get("identity", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_identity_section(
        eeprom,
        trainer_name=args.trainer_name,
        protocol_version=args.protocol_version,
        protocol_sub_version=args.protocol_sub_version,
        last_sync_epoch_seconds=args.last_sync,
        step_count=args.step_count,
    )
    save_target_eeprom(args, eeprom)
    print_json(read_identity_section(eeprom))
    return 0


def cmd_patch_stats(args: argparse.Namespace) -> int:
    payload: dict[str, int | list[int]] = {}
    if args.steps is not None:
        payload["steps"] = args.steps
    if args.lifetime_steps is not None:
        payload["lifetimeSteps"] = args.lifetime_steps
    if args.today_steps is not None:
        payload["todaySteps"] = args.today_steps
    if args.watts is not None:
        payload["watts"] = args.watts
    if args.last_sync is not None:
        payload["lastSyncEpochSeconds"] = args.last_sync
    if args.step_history is not None:
        payload["stepHistory"] = args.step_history

    if not payload:
        raise ValueError("Provide at least one stats field to patch")

    if args.server:
        result = try_remote_patch(args.server, "/api/v1/device/stats", payload)
        if result is not None:
            print_json(result.get("stats", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_stats_section(
        eeprom,
        steps=args.steps,
        lifetime_steps=args.lifetime_steps,
        today_steps=args.today_steps,
        watts=args.watts,
        last_sync_epoch_seconds=args.last_sync,
        step_history=args.step_history,
    )
    save_target_eeprom(args, eeprom)
    print_json(read_stats_section(eeprom))
    return 0


def cmd_patch_stroll(args: argparse.Namespace) -> int:
    payload: dict[str, int] = {}
    if args.session_watts is not None:
        payload["sessionWatts"] = args.session_watts
    if args.route_image_index is not None:
        payload["routeImageIndex"] = args.route_image_index

    if not payload:
        raise ValueError("Provide at least one stroll field to patch")

    if args.server:
        result = try_remote_patch(args.server, "/api/v1/device/stroll", payload)
        if result is not None:
            print_json(result.get("stroll", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_stroll_section(
        eeprom,
        session_watts=args.session_watts,
        route_image_index=args.route_image_index,
    )
    save_target_eeprom(args, eeprom)
    print_json(read_stroll_section(eeprom))
    return 0


def cmd_set_dowsed_item(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            f"/api/v1/device/inventory/dowsed/{args.slot}",
            {"itemId": args.item_id},
        )
        if result is not None:
            print_json(result.get("inventory", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_inventory_dowsed_item(eeprom, args.slot, args.item_id)
    save_target_eeprom(args, eeprom)
    print_json(read_inventory_section(eeprom))
    return 0


def cmd_set_gifted_item(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            f"/api/v1/device/inventory/gifted/{args.slot}",
            {"itemId": args.item_id},
        )
        if result is not None:
            print_json(result.get("inventory", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_inventory_gifted_item(eeprom, args.slot, args.item_id)
    save_target_eeprom(args, eeprom)
    print_json(read_inventory_section(eeprom))
    return 0


def cmd_set_caught_species(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            f"/api/v1/device/inventory/caught/{args.slot}",
            {"speciesId": args.species_id},
        )
        if result is not None:
            print_json(result.get("inventory", result))
            return 0

    eeprom = load_target_eeprom(args)
    set_inventory_caught_species(eeprom, args.slot, args.species_id)
    save_target_eeprom(args, eeprom)
    print_json(read_inventory_section(eeprom))
    return 0


def cmd_clear_journal(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(args.server, "/api/v1/device/journal/clear", {})
        if result is not None:
            print_json(result.get("journal", result))
            return 0

    eeprom = load_target_eeprom(args)
    clear_journal(eeprom)
    save_target_eeprom(args, eeprom)
    print_json(read_journal_section(eeprom))
    return 0


def cmd_sync_package(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/sync/package"))
        return 0

    print_json(build_sync_package(load_eeprom(args.eeprom)))
    return 0


def cmd_sync_apply(args: argparse.Namespace) -> int:
    package = load_json_file(args.input)
    if not isinstance(package, dict):
        raise ValueError("Sync package JSON must be an object")

    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/sync/apply",
            {"package": package},
        )
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    domains = apply_sync_package(eeprom, package)
    save_target_eeprom(args, eeprom)
    print_json({"status": "ok", "domains": domains, "snapshot": read_snapshot(eeprom)})
    return 0


def cmd_apply_operations(args: argparse.Namespace) -> int:
    payload = load_json_file(args.input)
    if isinstance(payload, dict):
        operations = payload.get("operations")
    else:
        operations = payload

    if not isinstance(operations, list) or not operations:
        raise ValueError("Operations JSON must be a non-empty list or {operations:[...]} object")

    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/sync/apply-operations",
            {"operations": operations},
        )
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    results = apply_semantic_operations(eeprom, operations)
    save_target_eeprom(args, eeprom)
    print_json(
        {
            "status": "ok",
            "results": results,
            "snapshot": read_snapshot(eeprom),
            "domains": read_all_domains(eeprom),
        }
    )
    return 0


def cmd_stroll_tick(args: argparse.Namespace) -> int:
    if args.server:
        result = try_remote_command(
            args.server,
            "/api/v1/stroll/tick",
            {"steps": args.steps},
        )
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    tick = add_walked_steps(eeprom, args.steps)
    save_target_eeprom(args, eeprom)
    print_json(
        {
            "status": "ok",
            "tick": tick,
            "snapshot": read_snapshot(eeprom),
            "stats": read_stats_section(eeprom),
        }
    )
    return 0


def cmd_stroll_catch(args: argparse.Namespace) -> int:
    payload = {"speciesId": args.species_id, "replaceSlot": args.replace_slot}
    if args.server:
        result = try_remote_command(args.server, "/api/v1/stroll/catch", payload)
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    placement = add_inventory_caught_species(eeprom, args.species_id, args.replace_slot)
    save_target_eeprom(args, eeprom)
    print_json({"status": "ok", "placement": placement, "inventory": read_inventory_section(eeprom)})
    return 0


def cmd_stroll_dowsing(args: argparse.Namespace) -> int:
    payload = {"itemId": args.item_id, "replaceSlot": args.replace_slot}
    if args.server:
        result = try_remote_command(args.server, "/api/v1/stroll/dowsing", payload)
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    placement = add_inventory_dowsed_item(eeprom, args.item_id, args.replace_slot)
    save_target_eeprom(args, eeprom)
    print_json({"status": "ok", "placement": placement, "inventory": read_inventory_section(eeprom)})
    return 0


def cmd_stroll_peer_gift(args: argparse.Namespace) -> int:
    payload = {"itemId": args.item_id, "replaceSlot": args.replace_slot}
    if args.server:
        result = try_remote_command(args.server, "/api/v1/stroll/peer-gift", payload)
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    placement = add_inventory_gifted_item(eeprom, args.item_id, args.replace_slot)
    save_target_eeprom(args, eeprom)
    print_json({"status": "ok", "placement": placement, "inventory": read_inventory_section(eeprom)})
    return 0


def cmd_stroll_reset_buffers(args: argparse.Namespace) -> int:
    clear_caught = not args.keep_caught
    clear_dowsed = not args.keep_dowsed
    clear_gifted = args.clear_gifted
    clear_journal = args.clear_journal

    payload = {
        "clearCaught": clear_caught,
        "clearDowsed": clear_dowsed,
        "clearGifted": clear_gifted,
        "clearJournal": clear_journal,
    }
    if args.server:
        result = try_remote_command(args.server, "/api/v1/stroll/reset-buffers", payload)
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    result = clear_stroll_buffers(
        eeprom,
        clear_caught=clear_caught,
        clear_dowsed=clear_dowsed,
        clear_gifted=clear_gifted,
        clear_journal_entries=clear_journal,
    )
    save_target_eeprom(args, eeprom)
    print_json(
        {
            "status": "ok",
            "result": result,
            "inventory": read_inventory_section(eeprom),
            "journal": read_journal_section(eeprom),
        }
    )
    return 0


def cmd_stroll_send(args: argparse.Namespace) -> int:
    payload: dict[str, Any] = {
        "speciesId": args.species_id,
        "level": args.level,
        "heldItem": args.held_item,
        "variantFlags": args.variant_flags,
        "specialFlags": args.special_flags,
        "clearBuffers": args.clear_buffers,
    }

    if args.course_id is not None:
        payload["courseId"] = args.course_id
    if args.route_image_index is not None:
        payload["routeImageIndex"] = args.route_image_index
    if args.nickname is not None:
        payload["nickname"] = args.nickname
    if args.friendship is not None:
        payload["friendship"] = args.friendship
    if args.moves is not None:
        payload["moves"] = args.moves
    if args.seed is not None:
        payload["seed"] = args.seed

    if args.server:
        result = try_remote_command(args.server, "/api/v1/stroll/send", payload)
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    result = send_pokemon_to_stroll(
        eeprom,
        species_id=args.species_id,
        level=args.level,
        route_image_index=args.route_image_index,
        course_id=args.course_id,
        nickname=args.nickname,
        friendship=args.friendship,
        held_item=args.held_item,
        moves=args.moves,
        variant_flags=args.variant_flags,
        special_flags=args.special_flags,
        seed=args.seed,
        clear_buffers=args.clear_buffers,
    )
    save_target_eeprom(args, eeprom)
    print_json({"status": "ok", **result})
    return 0


def cmd_stroll_return(args: argparse.Namespace) -> int:
    payload: dict[str, Any] = {
        "walkedSteps": args.walked_steps,
        "bonusWatts": args.bonus_watts,
        "autoCaptures": args.auto_captures,
        "replaceWhenFull": args.replace_when_full,
        "clearCaughtAfterReturn": args.clear_caught_after_return,
    }
    if args.gained_exp is not None:
        payload["gainedExp"] = args.gained_exp
    if args.capture_species_ids is not None:
        payload["captureSpeciesIds"] = args.capture_species_ids
    if args.seed is not None:
        payload["seed"] = args.seed

    if args.server:
        result = try_remote_command(args.server, "/api/v1/stroll/return", payload)
        if result is not None:
            print_json(result)
            return 0

    eeprom = load_target_eeprom(args)
    result = return_pokemon_from_stroll(
        eeprom,
        walked_steps=args.walked_steps,
        gained_exp=args.gained_exp,
        bonus_watts=args.bonus_watts,
        capture_species_ids=args.capture_species_ids,
        auto_captures=args.auto_captures,
        seed=args.seed,
        replace_when_full=args.replace_when_full,
        clear_caught_after_return=args.clear_caught_after_return,
    )
    save_target_eeprom(args, eeprom)
    print_json({"status": "ok", **result})
    return 0


def cmd_stroll_report(args: argparse.Namespace) -> int:
    if args.server:
        print_json(http_get_json(args.server, "/api/v1/stroll/report"))
        return 0

    eeprom = load_target_eeprom(args)
    print_json(stroll_report(eeprom))
    return 0


def cmd_hgss_status(args: argparse.Namespace) -> int:
    print_json(inspect_hgss_save(args.save))
    return 0


def cmd_hgss_patch(args: argparse.Namespace) -> int:
    if args.course_flags is not None and (args.unlock_all_courses or args.clear_courses):
        raise ValueError("Use either --course-flags or --unlock-all-courses/--clear-courses")
    if args.unlock_all_courses and args.clear_courses:
        raise ValueError("Use only one of --unlock-all-courses or --clear-courses")

    course_flags = args.course_flags
    if args.unlock_all_courses:
        course_flags = 0x07FFFFFF
    elif args.clear_courses:
        course_flags = 0

    if args.steps is None and args.watts is None and course_flags is None:
        raise ValueError("Provide at least one field: --steps, --watts, or course flags")

    if args.in_place:
        output_path = Path(args.save).resolve()
    elif args.output:
        output_path = Path(args.output).resolve()
    else:
        raise ValueError("Provide --output or use --in-place")

    patch, payload = patch_hgss_save(
        args.save,
        steps=args.steps,
        watts=args.watts,
        course_flags=course_flags,
        resign_storage=args.resign_storage,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    print_json(
        {
            "status": "ok",
            "output": str(output_path),
            "patch": patch,
        }
    )
    return 0


def cmd_hgss_diff(args: argparse.Namespace) -> int:
    result = diff_hgss_save_files(
        args.before,
        args.after,
        max_byte_diffs=args.max_byte_diffs,
    )
    print_json(
        {
            "status": "ok",
            "before": str(Path(args.before).resolve()),
            "after": str(Path(args.after).resolve()),
            "diff": result,
        }
    )
    return 0


def cmd_hgss_stroll_box_return(args: argparse.Namespace) -> int:
    if args.in_place:
        output_path = Path(args.save).resolve()
    elif args.output:
        output_path = Path(args.output).resolve()
    else:
        raise ValueError("Provide --output or use --in-place")

    result, payload = apply_hgss_stroll_box_return(
        args.save,
        box_number=args.box,
        source_slot_number=args.source_slot,
        target_slot_number=args.target_slot,
        source_species=args.source_species,
        extra_species=args.extra_species,
        level_gain=1,
        seed=args.seed,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    print_json(
        {
            "status": "ok",
            "output": str(output_path),
            "scenario": result,
        }
    )
    return 0


def add_target_options(parser: argparse.ArgumentParser, default_eeprom: str) -> None:
    parser.add_argument(
        "--server",
        default=None,
        help="Base URL, e.g. http://127.0.0.1:8080. If set, command runs against API.",
    )
    parser.add_argument(
        "--eeprom",
        default=default_eeprom,
        help="Local EEPROM path when --server is not set.",
    )


def build_parser() -> argparse.ArgumentParser:
    script_dir = Path(__file__).resolve().parent
    default_eeprom = str((script_dir / "test_roms" / "eeprom.bin").resolve())

    parser = argparse.ArgumentParser(description="Pokewalker EEPROM client utilities")
    sub = parser.add_subparsers(dest="command", required=True)

    p_status = sub.add_parser("status", help="Show bridge status (server) or local file status")
    add_target_options(p_status, default_eeprom)
    p_status.set_defaults(func=cmd_status)

    p_snapshot = sub.add_parser("snapshot", help="Show trainer/steps/watts")
    add_target_options(p_snapshot, default_eeprom)
    p_snapshot.set_defaults(func=cmd_snapshot)

    p_identity = sub.add_parser("identity", help="Show identity domain")
    add_target_options(p_identity, default_eeprom)
    p_identity.set_defaults(func=cmd_identity)

    p_stats = sub.add_parser("stats", help="Show stats domain")
    add_target_options(p_stats, default_eeprom)
    p_stats.set_defaults(func=cmd_stats)

    p_stroll = sub.add_parser("stroll", help="Show stroll/session domain")
    add_target_options(p_stroll, default_eeprom)
    p_stroll.set_defaults(func=cmd_stroll)

    p_inventory = sub.add_parser("inventory", help="Show inventory domain")
    add_target_options(p_inventory, default_eeprom)
    p_inventory.set_defaults(func=cmd_inventory)

    p_journal = sub.add_parser("journal", help="Show journal preview")
    add_target_options(p_journal, default_eeprom)
    p_journal.add_argument("--preview", type=int, default=6, help="Preview entries (0..24)")
    p_journal.set_defaults(func=cmd_journal)

    p_routes = sub.add_parser("routes", help="Show routes domain")
    add_target_options(p_routes, default_eeprom)
    p_routes.set_defaults(func=cmd_routes)

    p_domains = sub.add_parser("domains", help="Show all semantic domains")
    add_target_options(p_domains, default_eeprom)
    p_domains.set_defaults(func=cmd_domains)

    p_hgss_status = sub.add_parser(
        "hgss-status",
        help="Inspect HGSS save active blocks and Pokewalker fields",
    )
    p_hgss_status.add_argument("--save", required=True, help="HGSS .sav path")
    p_hgss_status.set_defaults(func=cmd_hgss_status)

    p_hgss_patch = sub.add_parser("hgss-patch", help="Patch HGSS Pokewalker fields")
    p_hgss_patch.add_argument("--save", required=True, help="Input HGSS .sav path")
    p_hgss_patch.add_argument("--output", default=None, help="Output save path")
    p_hgss_patch.add_argument(
        "--in-place",
        action="store_true",
        help="Overwrite input file instead of writing to --output",
    )
    p_hgss_patch.add_argument("--steps", type=int, default=None, help="Pokewalker total steps (u32)")
    p_hgss_patch.add_argument("--watts", type=int, default=None, help="Pokewalker watts (u32)")
    p_hgss_patch.add_argument(
        "--course-flags",
        type=lambda value: int(value, 0),
        default=None,
        help="Pokewalker course bitflags (u32, accepts decimal or 0xHEX)",
    )
    p_hgss_patch.add_argument(
        "--unlock-all-courses",
        action="store_true",
        help="Shortcut for --course-flags 0x07FFFFFF",
    )
    p_hgss_patch.add_argument(
        "--clear-courses",
        action="store_true",
        help="Shortcut for --course-flags 0",
    )
    p_hgss_patch.add_argument(
        "--resign-storage",
        action="store_true",
        help="Also resign active storage block checksum",
    )
    p_hgss_patch.set_defaults(func=cmd_hgss_patch)

    p_hgss_diff = sub.add_parser("hgss-diff", help="Diff two HGSS saves with Pokewalker focus")
    p_hgss_diff.add_argument("--before", required=True, help="Baseline HGSS .sav path")
    p_hgss_diff.add_argument("--after", required=True, help="Updated HGSS .sav path")
    p_hgss_diff.add_argument(
        "--max-byte-diffs",
        type=int,
        default=96,
        help="Maximum Pokewalker-window byte diffs to include",
    )
    p_hgss_diff.set_defaults(func=cmd_hgss_diff)

    p_hgss_stroll_box = sub.add_parser(
        "hgss-stroll-box-return",
        help="Raise one boxed Pokemon by +1 level and add a legal hatched Pidgey in same box",
    )
    p_hgss_stroll_box.add_argument("--save", required=True, help="Input HGSS .sav path")
    p_hgss_stroll_box.add_argument("--output", default=None, help="Output save path")
    p_hgss_stroll_box.add_argument(
        "--in-place",
        action="store_true",
        help="Overwrite input file instead of writing to --output",
    )
    p_hgss_stroll_box.add_argument("--box", type=int, default=17, help="1-based box number")
    p_hgss_stroll_box.add_argument(
        "--source-slot",
        type=int,
        default=1,
        help="1-based source slot in the selected box",
    )
    p_hgss_stroll_box.add_argument(
        "--target-slot",
        type=int,
        default=None,
        help="1-based target slot for extra Pokemon (default: first empty)",
    )
    p_hgss_stroll_box.add_argument(
        "--source-species",
        type=int,
        default=250,
        help="Expected species in source slot (default Ho-Oh=250)",
    )
    p_hgss_stroll_box.add_argument(
        "--extra-species",
        type=int,
        default=16,
        help="Extra species to add (default Pidgey=16)",
    )
    p_hgss_stroll_box.add_argument(
        "--seed",
        type=int,
        default=None,
        help="Deterministic RNG seed for generated extra Pokemon",
    )
    p_hgss_stroll_box.set_defaults(func=cmd_hgss_stroll_box_return)

    p_sync_package = sub.add_parser("sync-package", help="Build/read sync package for bridge decoupling")
    add_target_options(p_sync_package, default_eeprom)
    p_sync_package.set_defaults(func=cmd_sync_package)

    p_sync_apply = sub.add_parser("sync-apply", help="Apply sync package JSON")
    add_target_options(p_sync_apply, default_eeprom)
    p_sync_apply.add_argument("--input", required=True, help="Sync package JSON path")
    p_sync_apply.set_defaults(func=cmd_sync_apply)

    p_apply_ops = sub.add_parser("apply-operations", help="Apply semantic operations JSON batch")
    add_target_options(p_apply_ops, default_eeprom)
    p_apply_ops.add_argument("--input", required=True, help="Operations JSON path")
    p_apply_ops.set_defaults(func=cmd_apply_operations)

    p_stroll_tick = sub.add_parser("stroll-tick", help="Apply walked steps and derived watts")
    add_target_options(p_stroll_tick, default_eeprom)
    p_stroll_tick.add_argument("steps", type=int, help="Walked steps to add")
    p_stroll_tick.set_defaults(func=cmd_stroll_tick)

    p_stroll_catch = sub.add_parser("stroll-catch", help="Add caught species with slot policy")
    add_target_options(p_stroll_catch, default_eeprom)
    p_stroll_catch.add_argument("species_id", type=int, help="Species id (u16)")
    p_stroll_catch.add_argument(
        "--replace-slot",
        type=int,
        default=None,
        help="Slot override when no empty slot (0..2)",
    )
    p_stroll_catch.set_defaults(func=cmd_stroll_catch)

    p_stroll_dowsing = sub.add_parser("stroll-dowsing", help="Add dowsed item with slot policy")
    add_target_options(p_stroll_dowsing, default_eeprom)
    p_stroll_dowsing.add_argument("item_id", type=int, help="Item id (u16)")
    p_stroll_dowsing.add_argument(
        "--replace-slot",
        type=int,
        default=None,
        help="Slot override when no empty slot (0..2)",
    )
    p_stroll_dowsing.set_defaults(func=cmd_stroll_dowsing)

    p_stroll_peer = sub.add_parser("stroll-peer-gift", help="Add peer gift item with slot policy")
    add_target_options(p_stroll_peer, default_eeprom)
    p_stroll_peer.add_argument("item_id", type=int, help="Item id (u16)")
    p_stroll_peer.add_argument(
        "--replace-slot",
        type=int,
        default=None,
        help="Slot override when no empty slot (0..9)",
    )
    p_stroll_peer.set_defaults(func=cmd_stroll_peer_gift)

    p_stroll_reset = sub.add_parser("stroll-reset-buffers", help="Clear stroll buffers in EEPROM")
    add_target_options(p_stroll_reset, default_eeprom)
    p_stroll_reset.add_argument(
        "--keep-caught",
        action="store_true",
        help="Do not clear caught species slots",
    )
    p_stroll_reset.add_argument(
        "--keep-dowsed",
        action="store_true",
        help="Do not clear dowsed item slots",
    )
    p_stroll_reset.add_argument(
        "--clear-gifted",
        action="store_true",
        help="Also clear peer gifted item slots",
    )
    p_stroll_reset.add_argument(
        "--clear-journal",
        action="store_true",
        help="Also clear journal entries",
    )
    p_stroll_reset.set_defaults(func=cmd_stroll_reset_buffers)

    p_stroll_send = sub.add_parser(
        "stroll-send",
        help="Send a Pokemon to stroll route data in EEPROM",
    )
    add_target_options(p_stroll_send, default_eeprom)
    p_stroll_send.add_argument("species_id", type=int, help="Walking Pokemon species id (u16)")
    p_stroll_send.add_argument("--level", type=int, default=10, help="Walking Pokemon level")
    p_stroll_send.add_argument("--course-id", type=int, default=None, help="Pokewalker course id (0..26)")
    p_stroll_send.add_argument(
        "--route-image-index",
        type=int,
        default=None,
        help="Route image index override (u8)",
    )
    p_stroll_send.add_argument("--nickname", default=None, help="Walking Pokemon nickname")
    p_stroll_send.add_argument("--friendship", type=int, default=None, help="Friendship override (u8)")
    p_stroll_send.add_argument("--held-item", type=int, default=0, help="Held item id (u16)")
    p_stroll_send.add_argument(
        "--moves",
        type=int,
        nargs=4,
        default=None,
        metavar=("MOVE1", "MOVE2", "MOVE3", "MOVE4"),
        help="Four move ids (u16 each)",
    )
    p_stroll_send.add_argument("--variant-flags", type=int, default=0, help="Variant+gender bitflags")
    p_stroll_send.add_argument("--special-flags", type=int, default=0, help="Special flags (shiny/form)")
    p_stroll_send.add_argument("--seed", type=int, default=None, help="Deterministic seed")
    p_stroll_send.add_argument(
        "--clear-buffers",
        action="store_true",
        help="Clear caught/dowsed buffers before sending",
    )
    p_stroll_send.set_defaults(func=cmd_stroll_send)

    p_stroll_return = sub.add_parser(
        "stroll-return",
        help="Return walking Pokemon with EXP/steps/watts and optional captures",
    )
    add_target_options(p_stroll_return, default_eeprom)
    p_stroll_return.add_argument("walked_steps", type=int, help="Walked steps during stroll")
    p_stroll_return.add_argument("--gained-exp", type=int, default=None, help="Explicit EXP gain")
    p_stroll_return.add_argument("--bonus-watts", type=int, default=0, help="Bonus watts on return")
    p_stroll_return.add_argument(
        "--capture-species-ids",
        type=int,
        nargs="*",
        default=None,
        help="Explicit capture species list (u16 ids)",
    )
    p_stroll_return.add_argument(
        "--auto-captures",
        type=int,
        default=0,
        help="Auto-roll captures from route slots",
    )
    p_stroll_return.add_argument("--seed", type=int, default=None, help="Deterministic seed")
    p_stroll_return.add_argument(
        "--replace-when-full",
        action="store_true",
        help="Replace caught slot round-robin when full",
    )
    p_stroll_return.add_argument(
        "--clear-caught-after-return",
        action="store_true",
        help="Clear caught slots after processing return",
    )
    p_stroll_return.set_defaults(func=cmd_stroll_return)

    p_stroll_report = sub.add_parser(
        "stroll-report",
        help="Show generated stats and capture log summary",
    )
    add_target_options(p_stroll_report, default_eeprom)
    p_stroll_report.set_defaults(func=cmd_stroll_report)

    p_export = sub.add_parser("export", help="Export EEPROM to file")
    add_target_options(p_export, default_eeprom)
    p_export.add_argument("--output", required=True, help="Output EEPROM path")
    p_export.set_defaults(func=cmd_export)

    p_import = sub.add_parser("import", help="Import EEPROM from file")
    add_target_options(p_import, default_eeprom)
    p_import.add_argument("--input", required=True, help="Input EEPROM path")
    p_import.set_defaults(func=cmd_import)

    p_set_steps = sub.add_parser("set-steps", help="Set total steps")
    add_target_options(p_set_steps, default_eeprom)
    p_set_steps.add_argument("steps", type=int, help="New total steps")
    p_set_steps.set_defaults(func=cmd_set_steps)

    p_set_watts = sub.add_parser("set-watts", help="Set current watts")
    add_target_options(p_set_watts, default_eeprom)
    p_set_watts.add_argument("watts", type=int, help="New watts value")
    p_set_watts.set_defaults(func=cmd_set_watts)

    p_set_trainer = sub.add_parser("set-trainer", help="Set trainer name (max 8 chars)")
    add_target_options(p_set_trainer, default_eeprom)
    p_set_trainer.add_argument("name", help="Trainer name")
    p_set_trainer.set_defaults(func=cmd_set_trainer)

    p_set_sync = sub.add_parser("set-sync", help="Set last sync epoch seconds")
    add_target_options(p_set_sync, default_eeprom)
    p_set_sync.add_argument("epoch", type=int, help="Epoch seconds")
    p_set_sync.set_defaults(func=cmd_set_sync)

    p_patch_identity = sub.add_parser("patch-identity", help="Patch identity domain")
    add_target_options(p_patch_identity, default_eeprom)
    p_patch_identity.add_argument("--trainer-name", default=None, help="Trainer name")
    p_patch_identity.add_argument("--protocol-version", type=int, default=None, help="Protocol version")
    p_patch_identity.add_argument(
        "--protocol-sub-version", type=int, default=None, help="Protocol sub-version"
    )
    p_patch_identity.add_argument("--last-sync", type=int, default=None, help="Last sync epoch")
    p_patch_identity.add_argument("--step-count", type=int, default=None, help="Identity step count")
    p_patch_identity.set_defaults(func=cmd_patch_identity)

    p_patch_stats = sub.add_parser("patch-stats", help="Patch stats domain")
    add_target_options(p_patch_stats, default_eeprom)
    p_patch_stats.add_argument("--steps", type=int, default=None, help="Derived/global steps")
    p_patch_stats.add_argument("--lifetime-steps", type=int, default=None, help="Lifetime steps")
    p_patch_stats.add_argument("--today-steps", type=int, default=None, help="Today steps")
    p_patch_stats.add_argument("--watts", type=int, default=None, help="Current watts")
    p_patch_stats.add_argument("--last-sync", type=int, default=None, help="Last sync epoch")
    p_patch_stats.add_argument(
        "--step-history",
        type=int,
        nargs=7,
        default=None,
        metavar=("D1", "D2", "D3", "D4", "D5", "D6", "D7"),
        help="Seven day history (u32 each)",
    )
    p_patch_stats.set_defaults(func=cmd_patch_stats)

    p_patch_stroll = sub.add_parser("patch-stroll", help="Patch stroll/session domain")
    add_target_options(p_patch_stroll, default_eeprom)
    p_patch_stroll.add_argument("--session-watts", type=int, default=None, help="Session watts")
    p_patch_stroll.add_argument("--route-image-index", type=int, default=None, help="Route image index")
    p_patch_stroll.set_defaults(func=cmd_patch_stroll)

    p_dowsed = sub.add_parser("set-dowsed-item", help="Set dowsed item slot")
    add_target_options(p_dowsed, default_eeprom)
    p_dowsed.add_argument("slot", type=int, help="Slot index (0..2)")
    p_dowsed.add_argument("item_id", type=int, help="Item id (u16)")
    p_dowsed.set_defaults(func=cmd_set_dowsed_item)

    p_gifted = sub.add_parser("set-gifted-item", help="Set gifted item slot")
    add_target_options(p_gifted, default_eeprom)
    p_gifted.add_argument("slot", type=int, help="Slot index (0..9)")
    p_gifted.add_argument("item_id", type=int, help="Item id (u16)")
    p_gifted.set_defaults(func=cmd_set_gifted_item)

    p_caught = sub.add_parser("set-caught-species", help="Set caught species slot")
    add_target_options(p_caught, default_eeprom)
    p_caught.add_argument("slot", type=int, help="Slot index (0..2)")
    p_caught.add_argument("species_id", type=int, help="Species id (u16)")
    p_caught.set_defaults(func=cmd_set_caught_species)

    p_clear_journal = sub.add_parser("clear-journal", help="Clear event journal entries")
    add_target_options(p_clear_journal, default_eeprom)
    p_clear_journal.set_defaults(func=cmd_clear_journal)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        return args.func(args)
    except error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        print(f"HTTP error {exc.code}: {body}", file=sys.stderr)
        return 2
    except error.URLError as exc:
        print(f"Network error: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:  # noqa: BLE001
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
