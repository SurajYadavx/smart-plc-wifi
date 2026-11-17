"""
core/simulated_control_adapter.py
─────────────────────────────────
Simulated PLC Control Adapter for Offline Testing and CI.
Allows deterministic testing of output writes, PIN validation, allowlist enforcement,
and read-back verification in simulated environments without hardware.
"""

from __future__ import annotations

import logging
import time
from datetime import datetime, timezone
from typing import Dict, Optional, Set

from core.control_interface import (
    ControlCommandStatus,
    ForbiddenPointError,
    IPlcControlAdapter,
    OutputControlResult,
    UnauthorizedControlError,
)

logger = logging.getLogger(__name__)

BANNED_OUTPUT_POINTS = {"Y0", "Y00", "Y000", "Y0000"}


class SimulatedPlcControlAdapter(IPlcControlAdapter):
    """Simulated output control adapter managing an in-memory device state table."""

    def __init__(
        self,
        device_id: str = "melsec_q03",
        permitted_outputs: Optional[Set[str]] = None,
        operator_pin: str = "1234",
        simulate_readback_failure: bool = False,
        simulate_comm_failure: bool = False,
    ) -> None:
        self._device_id = device_id
        self._permitted_outputs = (
            set(permitted_outputs)
            if permitted_outputs is not None
            else {"Y20", "Y21", "Y22", "Y23", "Y24", "Y25", "Y26", "Y27"}
        )
        self.operator_pin = operator_pin
        self.simulate_readback_failure = simulate_readback_failure
        self.simulate_comm_failure = simulate_comm_failure
        # In-memory virtual relay coils
        self.state_table: Dict[str, int] = {p: 0 for p in self._permitted_outputs}

    @property
    def device_id(self) -> str:
        return self._device_id

    @property
    def permitted_outputs(self) -> Set[str]:
        return set(self._permitted_outputs)

    def validate_permission(self, point_id: str) -> bool:
        clean = point_id.strip().upper()
        if clean in BANNED_OUTPUT_POINTS:
            return False
        return clean in self._permitted_outputs

    def execute_write(
        self,
        point_id: str,
        state: int,
        auth_pin: str,
    ) -> OutputControlResult:
        start_t = time.perf_counter()
        ts_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        clean_point = point_id.strip().upper()

        if clean_point in BANNED_OUTPUT_POINTS:
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.FORBIDDEN_POINT,
                timestamp_utc=ts_utc,
                error_message=f"Point '{clean_point}' is strictly forbidden by project safety rules.",
            )

        if clean_point not in self._permitted_outputs:
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.FORBIDDEN_POINT,
                timestamp_utc=ts_utc,
                error_message=f"Point '{clean_point}' is not in permitted outputs allowlist.",
            )

        if auth_pin != self.operator_pin:
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.UNAUTHORIZED,
                timestamp_utc=ts_utc,
                error_message="Invalid operator authorization PIN.",
            )

        if state not in (0, 1):
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.WRITE_FAILED,
                timestamp_utc=ts_utc,
                error_message=f"Invalid requested state '{state}'. Must be 0 or 1.",
            )

        if self.simulate_comm_failure:
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.PLC_DISCONNECTED,
                timestamp_utc=ts_utc,
                error_message="Simulated PLC communication failure.",
            )

        # Mutate simulated relay state
        if not self.simulate_readback_failure:
            self.state_table[clean_point] = state
            read_back_val = state
            verified = True
            status = ControlCommandStatus.OUTPUT_CONFIRMED
            err = None
        else:
            # Simulate failure where PLC coil didn't take the write
            read_back_val = 0 if state == 1 else 1
            verified = False
            status = ControlCommandStatus.WRITE_FAILED
            err = f"Simulated read-back mismatch: requested {state}, read back {read_back_val}"

        latency = (time.perf_counter() - start_t) * 1000.0
        return OutputControlResult(
            success=verified,
            device_id=self._device_id,
            point_id=clean_point,
            requested_state=state,
            read_back_state=read_back_val,
            verified=verified,
            status=status,
            timestamp_utc=ts_utc,
            latency_ms=latency,
            error_message=err,
        )
