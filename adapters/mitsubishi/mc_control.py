"""
adapters/mitsubishi/mc_control.py
─────────────────────────────────
Mitsubishi MC Protocol 3E Binary Control Adapter with Read-Back Verification.
Implements authorized output control with strict whitelist validation,
banned point enforcement (strictly NO Y0), operator PIN authorization,
and read-back confirmation.
"""

from __future__ import annotations

import logging
import socket
import time
from datetime import datetime, timezone
from typing import Any, Dict, Optional, Set

from core.control_interface import (
    ControlCommandStatus,
    ForbiddenPointError,
    IPlcControlAdapter,
    OutputControlResult,
    UnauthorizedControlError,
)

logger = logging.getLogger(__name__)

BANNED_OUTPUT_POINTS = {"Y0", "Y00", "Y000", "Y0000"}


def _build_mc_3e_write_bit_frame(
    device_addr: str,
    state: int,
    plc_type: str = "Q",
    timer: int = 16,
) -> bytes:
    """
    Construct MC Protocol 3E Binary Bit-Unit Batch Write frame (0x1401).
    """
    dev_type = device_addr[0].upper()
    if dev_type != "Y":
        raise ValueError(f"Control adapter currently only allows Y-relays, got '{device_addr}'")

    addr_hex = int(device_addr[1:], 16)
    subcommand = 0x0003 if plc_type == "iQ-R" else 0x0001
    device_code = 0x9D  # Y output

    # Write data: 1 point -> 1 byte with 0x10 (ON) or 0x00 (OFF)
    bit_byte = 0x10 if state == 1 else 0x00

    request_data = bytearray()
    # Command: 0x1401 (little endian)
    request_data.extend((0x01, 0x14))
    # Subcommand (little endian)
    request_data.extend((subcommand & 0xFF, (subcommand >> 8) & 0xFF))

    if plc_type == "iQ-R":
        # 4 bytes address
        request_data.extend((
            addr_hex & 0xFF,
            (addr_hex >> 8) & 0xFF,
            (addr_hex >> 16) & 0xFF,
            (addr_hex >> 24) & 0xFF,
        ))
        # 2 bytes device code
        request_data.extend((device_code, 0x00))
    else:
        # 3 bytes address
        request_data.extend((
            addr_hex & 0xFF,
            (addr_hex >> 8) & 0xFF,
            (addr_hex >> 16) & 0xFF,
        ))
        # 1 byte device code
        request_data.append(device_code)

    # Device points count: 1 (little endian)
    request_data.extend((0x01, 0x00))
    # Data byte
    request_data.append(bit_byte)

    # Frame header: 3E binary
    frame = bytearray()
    frame.extend((0x50, 0x00))       # Subheader
    frame.append(0x00)               # Network No
    frame.append(0xFF)               # PC No
    frame.extend((0xFF, 0x03))       # Dest Module I/O
    frame.append(0x00)               # Dest Module Station
    # Length of (timer + request_data): 2 + len(request_data)
    rem_len = 2 + len(request_data)
    frame.extend((rem_len & 0xFF, (rem_len >> 8) & 0xFF))
    # Timer (little endian)
    frame.extend((timer & 0xFF, (timer >> 8) & 0xFF))
    frame.extend(request_data)

    return bytes(frame)


def _build_mc_3e_read_bit_frame(
    device_addr: str,
    points_count: int = 1,
    plc_type: str = "Q",
    timer: int = 16,
) -> bytes:
    """
    Construct MC Protocol 3E Binary Bit-Unit Batch Read frame (0x0401).
    """
    dev_type = device_addr[0].upper()
    addr_hex = int(device_addr[1:], 16)
    subcommand = 0x0003 if plc_type == "iQ-R" else 0x0001
    device_code = 0x9C if dev_type == "X" else 0x9D

    request_data = bytearray()
    # Command: 0x0401 (little endian)
    request_data.extend((0x01, 0x04))
    # Subcommand (little endian)
    request_data.extend((subcommand & 0xFF, (subcommand >> 8) & 0xFF))

    if plc_type == "iQ-R":
        request_data.extend((
            addr_hex & 0xFF,
            (addr_hex >> 8) & 0xFF,
            (addr_hex >> 16) & 0xFF,
            (addr_hex >> 24) & 0xFF,
        ))
        request_data.extend((device_code, 0x00))
    else:
        request_data.extend((
            addr_hex & 0xFF,
            (addr_hex >> 8) & 0xFF,
            (addr_hex >> 16) & 0xFF,
        ))
        request_data.append(device_code)

    request_data.extend((points_count & 0xFF, (points_count >> 8) & 0xFF))

    rem_len = 2 + len(request_data)
    frame = bytearray()
    frame.extend((0x50, 0x00))
    frame.append(0x00)
    frame.append(0xFF)
    frame.extend((0xFF, 0x03))
    frame.append(0x00)
    frame.extend((rem_len & 0xFF, (rem_len >> 8) & 0xFF))
    frame.extend((timer & 0xFF, (timer >> 8) & 0xFF))
    frame.extend(request_data)

    return bytes(frame)


class MitsubishiMcControlAdapter(IPlcControlAdapter):
    """
    Authorized output control adapter for Mitsubishi Q-Series & R-Series PLCs.
    Enforces allowlist, bans Y0, requires operator PIN, and performs read-back verification.
    """

    def __init__(
        self,
        device_id: str = "melsec_q03",
        host: str = "192.168.3.30",
        port: int = 5010,
        timeout_sec: float = 3.0,
        plc_type: str = "Q",
        permitted_outputs: Optional[Set[str]] = None,
        operator_pin: str = "1234",
    ) -> None:
        self._device_id = device_id
        self.host = host
        self.port = port
        self.timeout_sec = timeout_sec
        self.plc_type = plc_type
        # Default allowlist: Y20 only for Q-series; empty for R-series until verified
        if permitted_outputs is not None:
            self._permitted_outputs = set(permitted_outputs)
        else:
            self._permitted_outputs = {"Y20"} if plc_type == "Q" else set()
        self.operator_pin = operator_pin

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

        # 1. Ban Y0 check
        if clean_point in BANNED_OUTPUT_POINTS:
            err = f"Point '{clean_point}' is strictly forbidden by project safety rules."
            logger.error("Security violation: %s", err)
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.FORBIDDEN_POINT,
                timestamp_utc=ts_utc,
                error_message=err,
            )

        # 2. Whitelist permission check
        if clean_point not in self._permitted_outputs:
            err = f"Point '{clean_point}' is not in the permitted outputs allowlist: {sorted(self._permitted_outputs)}"
            logger.warning("Rejected write: %s", err)
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.FORBIDDEN_POINT,
                timestamp_utc=ts_utc,
                error_message=err,
            )

        # 3. PIN authorization check
        if auth_pin != self.operator_pin:
            err = "Invalid operator authorization PIN."
            logger.warning("Unauthorized control attempt for %s: %s", clean_point, err)
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.UNAUTHORIZED,
                timestamp_utc=ts_utc,
                error_message=err,
            )

        # 4. State validation
        if state not in (0, 1):
            err = f"Invalid requested state '{state}'. State must be 0 (OFF) or 1 (ON)."
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.WRITE_FAILED,
                timestamp_utc=ts_utc,
                error_message=err,
            )

        # 5. Connect and execute write
        sock: Optional[socket.socket] = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout_sec)
            sock.connect((self.host, self.port))

            # Send write frame
            write_frame = _build_mc_3e_write_bit_frame(clean_point, state, self.plc_type)
            sock.sendall(write_frame)

            # Receive write response
            resp = sock.recv(1024)
            if len(resp) < 11:
                raise IOError(f"Truncated write response from PLC (got {len(resp)} bytes, expected >= 11)")

            # Check End Code (bytes 9-10, little endian)
            end_code = resp[9] | (resp[10] << 8)
            if end_code != 0:
                raise IOError(f"PLC returned MC Protocol error end code: 0x{end_code:04X}")

            # 6. Read-Back Verification
            read_frame = _build_mc_3e_read_bit_frame(clean_point, 1, self.plc_type)
            sock.sendall(read_frame)
            read_resp = sock.recv(1024)
            if len(read_resp) < 12:
                raise IOError(f"Truncated read-back response from PLC (got {len(read_resp)} bytes, expected >= 12)")

            read_end_code = read_resp[9] | (read_resp[10] << 8)
            if read_end_code != 0:
                raise IOError(f"PLC returned MC Protocol error during read-back: 0x{read_end_code:04X}")

            # Bit data is at byte 11: high nibble for even point 0
            read_byte = read_resp[11]
            read_back_val = 1 if (read_byte & 0x10) else 0

            latency = (time.perf_counter() - start_t) * 1000.0
            is_verified = (read_back_val == state)

            if is_verified:
                status = ControlCommandStatus.OUTPUT_CONFIRMED
                err_msg = None
            else:
                status = ControlCommandStatus.WRITE_FAILED
                err_msg = f"Read-back mismatch: requested state {state}, but PLC output read back as {read_back_val}"

            return OutputControlResult(
                success=is_verified,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=read_back_val,
                verified=is_verified,
                status=status,
                timestamp_utc=ts_utc,
                latency_ms=latency,
                error_message=err_msg,
            )

        except (socket.timeout, socket.error, OSError) as net_err:
            latency = (time.perf_counter() - start_t) * 1000.0
            err_msg = f"Network socket error during write to {self.host}:{self.port} - {net_err}"
            logger.error(err_msg)
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.PLC_DISCONNECTED,
                timestamp_utc=ts_utc,
                latency_ms=latency,
                error_message=err_msg,
            )
        except Exception as exc:
            latency = (time.perf_counter() - start_t) * 1000.0
            err_msg = f"Write execution failed: {exc}"
            logger.error(err_msg)
            return OutputControlResult(
                success=False,
                device_id=self._device_id,
                point_id=clean_point,
                requested_state=state,
                read_back_state=None,
                verified=False,
                status=ControlCommandStatus.WRITE_FAILED,
                timestamp_utc=ts_utc,
                latency_ms=latency,
                error_message=err_msg,
            )
        finally:
            if sock:
                try:
                    sock.close()
                except Exception:
                    pass
