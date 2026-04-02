#!/usr/bin/env python3
"""WearWalker mock FastAPI server for 3DS WiFi protocol testing.

Endpoints are intentionally compatible with wearwalker_bridge_3ds source/wearwalker_api.c:
- GET /api/v1/bridge/status
- GET /api/v1/device/snapshot
- GET /api/v1/eeprom/export
- PUT /api/v1/eeprom/import

Additional test endpoints for command-style mutation:
- POST /api/v1/device/commands/set-steps
- POST /api/v1/device/commands/set-watts
- POST /api/v1/device/commands/set-trainer
- POST /api/v1/device/commands/set-sync
"""

from __future__ import annotations

import argparse
import threading
import time
from typing import Any
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Path as ApiPath, Query, Request
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel, Field

from eeprom_common import EEPROM_SIZE, load_eeprom, read_snapshot, save_eeprom
from eeprom_common import (
    add_inventory_caught_species,
    add_inventory_dowsed_item,
    add_inventory_gifted_item,
    add_walked_steps,
    apply_sprite_patches,
    apply_semantic_operations,
    apply_sync_package,
    build_sync_package,
    clear_journal,
    clear_stroll_buffers,
    read_all_domains,
    read_identity_section,
    read_inventory_section,
    read_journal_section,
    read_routes_section,
    read_stats_section,
    read_stroll_section,
    return_pokemon_from_stroll,
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
    send_pokemon_to_stroll,
    stroll_report,
)


class WearWalkerMockState:
    def __init__(self, eeprom_path: Path) -> None:
        self.eeprom_path = eeprom_path
        self._lock = threading.Lock()
        self._eeprom = load_eeprom(eeprom_path)
        self.started_at = time.time()

    def get_snapshot(self) -> dict:
        with self._lock:
            return read_snapshot(self._eeprom)

    def get_identity(self) -> dict:
        with self._lock:
            return read_identity_section(self._eeprom)

    def get_stats(self) -> dict:
        with self._lock:
            return read_stats_section(self._eeprom)

    def get_stroll(self) -> dict:
        with self._lock:
            return read_stroll_section(self._eeprom)

    def get_inventory(self) -> dict:
        with self._lock:
            return read_inventory_section(self._eeprom)

    def get_journal(self, preview_entries: int = 6) -> dict:
        with self._lock:
            return read_journal_section(self._eeprom, preview_entries=preview_entries)

    def get_routes(self) -> dict:
        with self._lock:
            return read_routes_section(self._eeprom)

    def get_stroll_report(self) -> dict:
        with self._lock:
            return stroll_report(self._eeprom)

    def get_domains(self) -> dict:
        with self._lock:
            return read_all_domains(self._eeprom)

    def get_sync_package(self) -> dict:
        with self._lock:
            return build_sync_package(self._eeprom)

    def export_eeprom(self) -> bytes:
        with self._lock:
            return bytes(self._eeprom)

    def import_eeprom(self, payload: bytes) -> None:
        if len(payload) != EEPROM_SIZE:
            raise ValueError(f"EEPROM must be exactly {EEPROM_SIZE} bytes")
        with self._lock:
            self._eeprom = bytearray(payload)
            save_eeprom(self.eeprom_path, self._eeprom)

    def mutate_steps(self, steps: int) -> dict:
        with self._lock:
            set_steps(self._eeprom, steps)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_snapshot(self._eeprom)

    def mutate_watts(self, watts: int) -> dict:
        with self._lock:
            set_watts(self._eeprom, watts)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_snapshot(self._eeprom)

    def mutate_trainer(self, name: str) -> dict:
        with self._lock:
            set_trainer_name(self._eeprom, name)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_snapshot(self._eeprom)

    def mutate_sync(self, epoch: int) -> dict:
        with self._lock:
            set_last_sync_seconds(self._eeprom, epoch)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_snapshot(self._eeprom)

    def mutate_identity(
        self,
        *,
        trainer_name: str | None = None,
        trainer_tid: int | None = None,
        trainer_sid: int | None = None,
        protocol_version: int | None = None,
        protocol_sub_version: int | None = None,
        last_sync_epoch_seconds: int | None = None,
        step_count: int | None = None,
    ) -> dict:
        with self._lock:
            set_identity_section(
                self._eeprom,
                trainer_name=trainer_name,
                trainer_tid=trainer_tid,
                trainer_sid=trainer_sid,
                protocol_version=protocol_version,
                protocol_sub_version=protocol_sub_version,
                last_sync_epoch_seconds=last_sync_epoch_seconds,
                step_count=step_count,
            )
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_identity_section(self._eeprom)

    def mutate_stats(
        self,
        *,
        steps: int | None = None,
        lifetime_steps: int | None = None,
        today_steps: int | None = None,
        watts: int | None = None,
        last_sync_epoch_seconds: int | None = None,
        step_history: list[int] | None = None,
    ) -> dict:
        with self._lock:
            set_stats_section(
                self._eeprom,
                steps=steps,
                lifetime_steps=lifetime_steps,
                today_steps=today_steps,
                watts=watts,
                last_sync_epoch_seconds=last_sync_epoch_seconds,
                step_history=step_history,
            )
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_stats_section(self._eeprom)

    def mutate_stroll(
        self,
        *,
        session_watts: int | None = None,
        route_image_index: int | None = None,
    ) -> dict:
        with self._lock:
            set_stroll_section(
                self._eeprom,
                session_watts=session_watts,
                route_image_index=route_image_index,
            )
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_stroll_section(self._eeprom)

    def mutate_dowsed_item(self, slot: int, item_id: int) -> dict:
        with self._lock:
            set_inventory_dowsed_item(self._eeprom, slot, item_id)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_inventory_section(self._eeprom)

    def mutate_gifted_item(self, slot: int, item_id: int) -> dict:
        with self._lock:
            set_inventory_gifted_item(self._eeprom, slot, item_id)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_inventory_section(self._eeprom)

    def mutate_caught_species(self, slot: int, species_id: int) -> dict:
        with self._lock:
            set_inventory_caught_species(self._eeprom, slot, species_id)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_inventory_section(self._eeprom)

    def mutate_clear_journal(self) -> dict:
        with self._lock:
            clear_journal(self._eeprom)
            save_eeprom(self.eeprom_path, self._eeprom)
            return read_journal_section(self._eeprom)

    def mutate_stroll_tick(self, steps: int) -> dict:
        with self._lock:
            tick_result = add_walked_steps(self._eeprom, steps)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "tick": tick_result,
                "snapshot": read_snapshot(self._eeprom),
                "stats": read_stats_section(self._eeprom),
            }

    def mutate_stroll_send(
        self,
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
    ) -> dict:
        with self._lock:
            payload = send_pokemon_to_stroll(
                self._eeprom,
                species_id=species_id,
                level=level,
                route_image_index=route_image_index,
                course_id=course_id,
                nickname=nickname,
                friendship=friendship,
                held_item=held_item,
                moves=moves,
                variant_flags=variant_flags,
                special_flags=special_flags,
                seed=seed,
                clear_buffers=clear_buffers,
                allow_locked_course=allow_locked_course,
                assume_national_dex=assume_national_dex,
                unlock_special_courses=unlock_special_courses,
                unlock_event_courses=unlock_event_courses,
            )
            save_eeprom(self.eeprom_path, self._eeprom)
            return payload

    def mutate_stroll_return(
        self,
        *,
        walked_steps: int,
        gained_exp: int | None = None,
        bonus_watts: int = 0,
        capture_species_ids: list[int] | None = None,
        auto_captures: int = 0,
        seed: int | None = None,
        replace_when_full: bool = False,
        clear_caught_after_return: bool = False,
    ) -> dict:
        with self._lock:
            payload = return_pokemon_from_stroll(
                self._eeprom,
                walked_steps=walked_steps,
                gained_exp=gained_exp,
                bonus_watts=bonus_watts,
                capture_species_ids=capture_species_ids,
                auto_captures=auto_captures,
                seed=seed,
                replace_when_full=replace_when_full,
                clear_caught_after_return=clear_caught_after_return,
            )
            save_eeprom(self.eeprom_path, self._eeprom)
            return payload

    def mutate_add_caught_species(self, species_id: int, replace_slot: int | None = None) -> dict:
        with self._lock:
            placement = add_inventory_caught_species(self._eeprom, species_id, replace_slot)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "placement": placement,
                "inventory": read_inventory_section(self._eeprom),
            }

    def mutate_add_dowsed_item(self, item_id: int, replace_slot: int | None = None) -> dict:
        with self._lock:
            placement = add_inventory_dowsed_item(self._eeprom, item_id, replace_slot)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "placement": placement,
                "inventory": read_inventory_section(self._eeprom),
            }

    def mutate_add_gifted_item(self, item_id: int, replace_slot: int | None = None) -> dict:
        with self._lock:
            placement = add_inventory_gifted_item(self._eeprom, item_id, replace_slot)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "placement": placement,
                "inventory": read_inventory_section(self._eeprom),
            }

    def mutate_clear_stroll_buffers(
        self,
        *,
        clear_caught: bool = True,
        clear_dowsed: bool = True,
        clear_gifted: bool = False,
        clear_journal_entries: bool = False,
    ) -> dict:
        with self._lock:
            result = clear_stroll_buffers(
                self._eeprom,
                clear_caught=clear_caught,
                clear_dowsed=clear_dowsed,
                clear_gifted=clear_gifted,
                clear_journal_entries=clear_journal_entries,
            )
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "result": result,
                "inventory": read_inventory_section(self._eeprom),
                "journal": read_journal_section(self._eeprom),
            }

    def mutate_sprite_patches(self, patches: list[dict[str, Any]]) -> dict:
        with self._lock:
            result = apply_sprite_patches(self._eeprom, patches)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "patches": result,
                "routes": read_routes_section(self._eeprom),
            }

    def mutate_apply_sync_package(self, package: dict[str, Any]) -> dict:
        with self._lock:
            domains = apply_sync_package(self._eeprom, package)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "status": "ok",
                "domains": domains,
                "snapshot": read_snapshot(self._eeprom),
            }

    def mutate_apply_operations(self, operations: list[dict[str, Any]]) -> dict:
        with self._lock:
            results = apply_semantic_operations(self._eeprom, operations)
            save_eeprom(self.eeprom_path, self._eeprom)
            return {
                "status": "ok",
                "results": results,
                "snapshot": read_snapshot(self._eeprom),
                "domains": read_all_domains(self._eeprom),
            }

    def status(self) -> dict:
        with self._lock:
            size = len(self._eeprom)
            snapshot = read_snapshot(self._eeprom)

        return {
            "status": "ok",
            "apiVersion": "v1",
            "backend": "python-fastapi-mock",
            "connected": True,
            "eepromPath": str(self.eeprom_path),
            "eepromSize": size,
            "uptimeSeconds": int(time.time() - self.started_at),
            "trainerName": snapshot["trainerName"],
        }


class StepsRequest(BaseModel):
    steps: int = Field(ge=0, le=0xFFFFFFFF)


class WattsRequest(BaseModel):
    watts: int = Field(ge=0, le=0xFFFF)


class TrainerRequest(BaseModel):
    name: str = Field(min_length=1, max_length=64)


class SyncRequest(BaseModel):
    epoch: int = Field(ge=0, le=0xFFFFFFFF)


class IdentityPatchRequest(BaseModel):
    trainerName: str | None = Field(default=None, min_length=1, max_length=64)
    trainerTid: int | None = Field(default=None, ge=0, le=0xFFFF)
    trainerSid: int | None = Field(default=None, ge=0, le=0xFFFF)
    protocolVersion: int | None = Field(default=None, ge=0, le=0xFF)
    protocolSubVersion: int | None = Field(default=None, ge=0, le=0xFF)
    lastSyncEpochSeconds: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    stepCount: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)


class StatsPatchRequest(BaseModel):
    steps: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    lifetimeSteps: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    todaySteps: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    watts: int | None = Field(default=None, ge=0, le=0xFFFF)
    lastSyncEpochSeconds: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    stepHistory: list[int] | None = Field(default=None, min_length=7, max_length=7)


class StrollPatchRequest(BaseModel):
    sessionWatts: int | None = Field(default=None, ge=0, le=0xFFFF)
    routeImageIndex: int | None = Field(default=None, ge=0, le=0xFF)


class InventoryItemRequest(BaseModel):
    itemId: int = Field(ge=0, le=0xFFFF)


class CaughtSpeciesRequest(BaseModel):
    speciesId: int = Field(ge=0, le=0xFFFF)


class ReplaceableCaughtSpeciesRequest(BaseModel):
    speciesId: int = Field(ge=0, le=0xFFFF)
    replaceSlot: int | None = Field(default=None, ge=0, le=2)


class ReplaceableItemRequest(BaseModel):
    itemId: int = Field(ge=0, le=0xFFFF)
    replaceSlot: int | None = Field(default=None)


class StrollTickRequest(BaseModel):
    steps: int = Field(ge=0, le=0xFFFFFFFF)


class StrollSendRequest(BaseModel):
    speciesId: int = Field(ge=0, le=0xFFFF)
    level: int = Field(default=10, ge=1, le=100)
    routeImageIndex: int | None = Field(default=None, ge=0, le=0xFF)
    courseId: int | None = Field(default=None, ge=0, le=26)
    nickname: str | None = Field(default=None, min_length=1, max_length=32)
    friendship: int | None = Field(default=None, ge=0, le=0xFF)
    heldItem: int = Field(default=0, ge=0, le=0xFFFF)
    moves: list[int] | None = Field(default=None, min_length=4, max_length=4)
    variantFlags: int = Field(default=0, ge=0, le=0xFF)
    specialFlags: int = Field(default=0, ge=0, le=0xFF)
    seed: int | None = Field(default=None)
    clearBuffers: bool = False
    allowLockedCourse: bool = False
    assumeNationalDex: bool = True
    unlockSpecialCourses: bool = False
    unlockEventCourses: bool = False


class SpritePatchEntryRequest(BaseModel):
    key: str = Field(min_length=1, max_length=64)
    dataHex: str = Field(min_length=2, max_length=16384)


class SpritePatchBatchRequest(BaseModel):
    patches: list[SpritePatchEntryRequest] = Field(min_length=1, max_length=32)


class StrollReturnRequest(BaseModel):
    walkedSteps: int = Field(ge=0, le=0xFFFFFFFF)
    gainedExp: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    bonusWatts: int = Field(default=0, ge=0, le=0xFFFF)
    captureSpeciesIds: list[int] | None = None
    autoCaptures: int = Field(default=0, ge=0)
    seed: int | None = Field(default=None)
    replaceWhenFull: bool = False
    clearCaughtAfterReturn: bool = False


class StrollResetRequest(BaseModel):
    clearCaught: bool = True
    clearDowsed: bool = True
    clearGifted: bool = False
    clearJournal: bool = False


class SyncApplyRequest(BaseModel):
    package: dict[str, Any]


class OperationsApplyRequest(BaseModel):
    operations: list[dict[str, Any]] = Field(min_length=1)


def create_app(eeprom_path: Path) -> FastAPI:
    state = WearWalkerMockState(eeprom_path)
    app = FastAPI(
        title="WearWalker Mock Backend",
        version="1.0.0",
        description="FastAPI mock backend for wearwalker_bridge_3ds protocol testing.",
    )
    app.state.ww_state = state

    def json_error(status_code: int, code: str, message: str, **extra: Any) -> JSONResponse:
        payload = {"error": code, "message": message}
        payload.update(extra)
        return JSONResponse(status_code=status_code, content=payload)

    def ensure_patch_has_values(body: BaseModel) -> None:
        if not any(value is not None for value in body.model_dump().values()):
            raise ValueError("At least one field must be provided")

    @app.get("/")
    def root() -> dict:
        return {
            "status": "ok",
            "message": "WearWalker mock backend",
            "docs": "/docs",
        }

    @app.get("/api/v1/bridge/status")
    def bridge_status() -> dict:
        return app.state.ww_state.status()

    @app.get("/api/v1/device/snapshot")
    def device_snapshot() -> dict:
        return app.state.ww_state.get_snapshot()

    @app.get("/api/v1/device/domains")
    def device_domains() -> dict:
        return app.state.ww_state.get_domains()

    @app.get("/api/v1/device/identity")
    def device_identity() -> dict:
        return app.state.ww_state.get_identity()

    @app.patch("/api/v1/device/identity")
    def patch_identity(body: IdentityPatchRequest) -> dict:
        try:
            ensure_patch_has_values(body)
            identity = app.state.ww_state.mutate_identity(
                trainer_name=body.trainerName,
                trainer_tid=body.trainerTid,
                trainer_sid=body.trainerSid,
                protocol_version=body.protocolVersion,
                protocol_sub_version=body.protocolSubVersion,
                last_sync_epoch_seconds=body.lastSyncEpochSeconds,
                step_count=body.stepCount,
            )
            return {"status": "ok", "identity": identity}
        except ValueError as exc:
            return json_error(400, "invalid_identity_patch", str(exc))

    @app.get("/api/v1/device/stats")
    def device_stats() -> dict:
        return app.state.ww_state.get_stats()

    @app.patch("/api/v1/device/stats")
    def patch_stats(body: StatsPatchRequest) -> dict:
        try:
            ensure_patch_has_values(body)
            stats = app.state.ww_state.mutate_stats(
                steps=body.steps,
                lifetime_steps=body.lifetimeSteps,
                today_steps=body.todaySteps,
                watts=body.watts,
                last_sync_epoch_seconds=body.lastSyncEpochSeconds,
                step_history=body.stepHistory,
            )
            return {"status": "ok", "stats": stats}
        except ValueError as exc:
            return json_error(400, "invalid_stats_patch", str(exc))

    @app.get("/api/v1/device/stroll")
    def device_stroll() -> dict:
        return app.state.ww_state.get_stroll()

    @app.patch("/api/v1/device/stroll")
    def patch_stroll(body: StrollPatchRequest) -> dict:
        try:
            ensure_patch_has_values(body)
            stroll = app.state.ww_state.mutate_stroll(
                session_watts=body.sessionWatts,
                route_image_index=body.routeImageIndex,
            )
            return {"status": "ok", "stroll": stroll}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_patch", str(exc))

    @app.get("/api/v1/device/inventory")
    def device_inventory() -> dict:
        return app.state.ww_state.get_inventory()

    @app.post("/api/v1/device/inventory/dowsed/{slot}")
    def patch_inventory_dowsed(
        slot: int = ApiPath(ge=0, le=2),
        body: InventoryItemRequest | None = None,
    ) -> dict:
        if body is None:
            return json_error(400, "missing_payload", "JSON payload is required")
        try:
            inventory = app.state.ww_state.mutate_dowsed_item(slot=slot, item_id=body.itemId)
            return {"status": "ok", "inventory": inventory}
        except ValueError as exc:
            return json_error(400, "invalid_inventory_patch", str(exc))

    @app.post("/api/v1/device/inventory/gifted/{slot}")
    def patch_inventory_gifted(
        slot: int = ApiPath(ge=0, le=9),
        body: InventoryItemRequest | None = None,
    ) -> dict:
        if body is None:
            return json_error(400, "missing_payload", "JSON payload is required")
        try:
            inventory = app.state.ww_state.mutate_gifted_item(slot=slot, item_id=body.itemId)
            return {"status": "ok", "inventory": inventory}
        except ValueError as exc:
            return json_error(400, "invalid_inventory_patch", str(exc))

    @app.post("/api/v1/device/inventory/caught/{slot}")
    def patch_inventory_caught(
        slot: int = ApiPath(ge=0, le=2),
        body: CaughtSpeciesRequest | None = None,
    ) -> dict:
        if body is None:
            return json_error(400, "missing_payload", "JSON payload is required")
        try:
            inventory = app.state.ww_state.mutate_caught_species(slot=slot, species_id=body.speciesId)
            return {"status": "ok", "inventory": inventory}
        except ValueError as exc:
            return json_error(400, "invalid_inventory_patch", str(exc))

    @app.get("/api/v1/device/journal")
    def device_journal(preview: int = Query(default=6, ge=0, le=24)) -> dict:
        return app.state.ww_state.get_journal(preview_entries=preview)

    @app.post("/api/v1/device/journal/clear")
    def clear_device_journal() -> dict:
        journal = app.state.ww_state.mutate_clear_journal()
        return {"status": "ok", "journal": journal}

    @app.get("/api/v1/device/routes")
    def device_routes() -> dict:
        return app.state.ww_state.get_routes()

    @app.get("/api/v1/sync/package")
    def sync_package() -> dict:
        return app.state.ww_state.get_sync_package()

    @app.post("/api/v1/sync/apply")
    def sync_apply(body: SyncApplyRequest) -> dict:
        try:
            return app.state.ww_state.mutate_apply_sync_package(body.package)
        except ValueError as exc:
            return json_error(400, "invalid_sync_package", str(exc))

    @app.post("/api/v1/sync/apply-operations")
    def sync_apply_operations(body: OperationsApplyRequest) -> dict:
        try:
            return app.state.ww_state.mutate_apply_operations(body.operations)
        except ValueError as exc:
            return json_error(400, "invalid_operations", str(exc))

    @app.post("/api/v1/stroll/tick")
    def stroll_tick(body: StrollTickRequest) -> dict:
        try:
            payload = app.state.ww_state.mutate_stroll_tick(body.steps)
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_tick", str(exc))

    @app.post("/api/v1/stroll/send")
    def stroll_send(body: StrollSendRequest) -> dict:
        try:
            payload = app.state.ww_state.mutate_stroll_send(
                species_id=body.speciesId,
                level=body.level,
                route_image_index=body.routeImageIndex,
                course_id=body.courseId,
                nickname=body.nickname,
                friendship=body.friendship,
                held_item=body.heldItem,
                moves=body.moves,
                variant_flags=body.variantFlags,
                special_flags=body.specialFlags,
                seed=body.seed,
                clear_buffers=body.clearBuffers,
                allow_locked_course=body.allowLockedCourse,
                assume_national_dex=body.assumeNationalDex,
                unlock_special_courses=body.unlockSpecialCourses,
                unlock_event_courses=body.unlockEventCourses,
            )
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_send", str(exc))

    @app.post("/api/v2/stroll/sprite-patches")
    def stroll_sprite_patches(body: SpritePatchBatchRequest) -> dict:
        try:
            payload = app.state.ww_state.mutate_sprite_patches(
                [entry.model_dump() for entry in body.patches]
            )
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_sprite_patch", str(exc))

    @app.post("/api/v1/stroll/return")
    def stroll_return(body: StrollReturnRequest) -> dict:
        try:
            payload = app.state.ww_state.mutate_stroll_return(
                walked_steps=body.walkedSteps,
                gained_exp=body.gainedExp,
                bonus_watts=body.bonusWatts,
                capture_species_ids=body.captureSpeciesIds,
                auto_captures=body.autoCaptures,
                seed=body.seed,
                replace_when_full=body.replaceWhenFull,
                clear_caught_after_return=body.clearCaughtAfterReturn,
            )
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_return", str(exc))

    @app.get("/api/v1/stroll/report")
    def stroll_report_endpoint() -> dict:
        return app.state.ww_state.get_stroll_report()

    @app.post("/api/v1/stroll/catch")
    def stroll_catch(body: ReplaceableCaughtSpeciesRequest) -> dict:
        try:
            payload = app.state.ww_state.mutate_add_caught_species(
                body.speciesId,
                replace_slot=body.replaceSlot,
            )
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_catch", str(exc))

    @app.post("/api/v1/stroll/dowsing")
    def stroll_dowsing(body: ReplaceableItemRequest) -> dict:
        if body.replaceSlot is not None and (body.replaceSlot < 0 or body.replaceSlot > 2):
            return json_error(400, "invalid_replace_slot", "replaceSlot for dowsing must be 0..2")
        try:
            payload = app.state.ww_state.mutate_add_dowsed_item(
                body.itemId,
                replace_slot=body.replaceSlot,
            )
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_dowsing", str(exc))

    @app.post("/api/v1/stroll/peer-gift")
    def stroll_peer_gift(body: ReplaceableItemRequest) -> dict:
        if body.replaceSlot is not None and (body.replaceSlot < 0 or body.replaceSlot > 9):
            return json_error(400, "invalid_replace_slot", "replaceSlot for peer gift must be 0..9")
        try:
            payload = app.state.ww_state.mutate_add_gifted_item(
                body.itemId,
                replace_slot=body.replaceSlot,
            )
            return {"status": "ok", **payload}
        except ValueError as exc:
            return json_error(400, "invalid_stroll_peer_gift", str(exc))

    @app.post("/api/v1/stroll/reset-buffers")
    def stroll_reset_buffers(body: StrollResetRequest) -> dict:
        payload = app.state.ww_state.mutate_clear_stroll_buffers(
            clear_caught=body.clearCaught,
            clear_dowsed=body.clearDowsed,
            clear_gifted=body.clearGifted,
            clear_journal_entries=body.clearJournal,
        )
        return {"status": "ok", **payload}

    @app.get("/api/v1/eeprom/export")
    def eeprom_export() -> Response:
        payload = app.state.ww_state.export_eeprom()
        headers = {
            "Content-Length": str(len(payload)),
            "Connection": "close",
        }
        return Response(content=payload, media_type="application/octet-stream", headers=headers)

    @app.put("/api/v1/eeprom/import")
    async def eeprom_import(request: Request) -> dict:
        content_type = request.headers.get("content-type", "")
        if "application/octet-stream" not in content_type:
            return json_error(
                415,
                "invalid_content_type",
                "Content-Type must be application/octet-stream",
            )

        content_length_header = request.headers.get("content-length")
        if content_length_header is None:
            return json_error(411, "length_required", "Missing Content-Length")

        try:
            content_length = int(content_length_header)
        except ValueError:
            return json_error(400, "invalid_length", "Content-Length is not a valid integer")

        if content_length != EEPROM_SIZE:
            return json_error(
                400,
                "invalid_size",
                f"EEPROM payload must be exactly {EEPROM_SIZE} bytes",
                expected=EEPROM_SIZE,
                received=content_length,
            )

        payload = await request.body()
        if len(payload) != content_length:
            return json_error(
                400,
                "short_read",
                "Received fewer bytes than Content-Length",
                expected=content_length,
                received=len(payload),
            )

        try:
            app.state.ww_state.import_eeprom(payload)
        except ValueError as exc:
            return json_error(400, "invalid_payload", str(exc))

        return {"status": "ok", "importedBytes": content_length}

    @app.post("/api/v1/device/commands/set-steps")
    def command_set_steps(body: StepsRequest) -> dict:
        snapshot = app.state.ww_state.mutate_steps(body.steps)
        return {"status": "ok", "snapshot": snapshot}

    @app.post("/api/v1/device/commands/set-watts")
    def command_set_watts(body: WattsRequest) -> dict:
        snapshot = app.state.ww_state.mutate_watts(body.watts)
        return {"status": "ok", "snapshot": snapshot}

    @app.post("/api/v1/device/commands/set-trainer")
    def command_set_trainer(body: TrainerRequest) -> dict:
        snapshot = app.state.ww_state.mutate_trainer(body.name)
        return {"status": "ok", "snapshot": snapshot}

    @app.post("/api/v1/device/commands/set-sync")
    def command_set_sync(body: SyncRequest) -> dict:
        snapshot = app.state.ww_state.mutate_sync(body.epoch)
        return {"status": "ok", "snapshot": snapshot}

    return app


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    default_eeprom = script_dir / "test_roms" / "eeprom.bin"

    parser = argparse.ArgumentParser(description="WearWalker mock HTTP server")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Bind port (default: 8080)")
    parser.add_argument(
        "--eeprom",
        default=str(default_eeprom),
        help=f"EEPROM file path (default: {default_eeprom})",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    eeprom_path = Path(args.eeprom).resolve()
    app = create_app(eeprom_path)

    print(f"Mock WearWalker API listening on http://{args.host}:{args.port}")
    print(f"EEPROM file: {eeprom_path}")
    print("FastAPI docs: /docs")

    uvicorn.run(app, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
