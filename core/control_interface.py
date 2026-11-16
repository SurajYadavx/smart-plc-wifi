"""
core/control_interface.py
─────────────────────────
Explicit Industrial Output Control and Verification Architecture.
Maintains strict separation between read-only monitoring and authorized
write operations with allowlist enforcement and read-back verification.
"""

from __future__ import annotations

import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Dict, List, Optional, Set

logger = logging.getLogger(__name__)


class ControlCommandStatus(str, Enum):
    """Detailed lifecycle states for an industrial output write operation."""
    REQUEST_RECEIVED = "REQUEST_RECEIVED"
    VALIDATED = "VALIDATED"
    COMMAND_TRANSMITTED = "COMMAND_TRANSMITTED"
    PLC_ACK_RECEIVED = "PLC_ACK_RECEIVED"
    READBACK_COMPLETED = "READBACK_COMPLETED"
    OUTPUT_CONFIRMED = "OUTPUT_CONFIRMED"
    WRITE_FAILED = "WRITE_FAILED"
    UNAUTHORIZED = "UNAUTHORIZED"
    FORBIDDEN_POINT = "FORBIDDEN_POINT"
    PLC_DISCONNECTED = "PLC_DISCONNECTED"


class ControlSecurityError(Exception):
    """Base exception for output control security violations."""
    pass


class UnauthorizedControlError(ControlSecurityError):
    """Raised when an operation lacks valid operator credentials/PIN."""
    pass


class ForbiddenPointError(ControlSecurityError):
    """Raised when an attempt is made to control a non-whitelisted or banned point (such as Y0)."""
    pass


@dataclass
class OutputControlResult:
    """Detailed record of an executed output control command."""
    success: bool
    device_id: str
    point_id: str
    requested_state: int                 # 0 (OFF) or 1 (ON)
    read_back_state: Optional[int]       # State verified via post-write read
    verified: bool                       # True only if read_back == requested_state
    status: ControlCommandStatus
    timestamp_utc: str
    latency_ms: float = 0.0
    error_message: Optional[str] = None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "success": self.success,
            "device_id": self.device_id,
            "point_id": self.point_id,
            "requested_state": self.requested_state,
            "read_back_state": self.read_back_state,
            "verified": self.verified,
            "status": self.status.value,
            "timestamp_utc": self.timestamp_utc,
            "latency_ms": round(self.latency_ms, 2),
            "error_message": self.error_message,
        }


class IPlcControlAdapter(ABC):
    """
    Abstract interface for authorized PLC output actuation.
    Completely isolated from IPlcProtocolAdapter to preserve read-only monitoring invariants.
    """

    @property
    @abstractmethod
    def device_id(self) -> str:
        """Target device identifier."""
        pass

    @property
    @abstractmethod
    def permitted_outputs(self) -> Set[str]:
        """Set of allowed output points (e.g. {'Y20'})."""
        pass

    @abstractmethod
    def validate_permission(self, point_id: str) -> bool:
        """
        Verify if a point is allowed for control.
        Must strictly reject Y0 and any point not in permitted_outputs.
        """
        pass

    @abstractmethod
    def execute_write(
        self,
        point_id: str,
        state: int,
        auth_pin: str,
    ) -> OutputControlResult:
        """
        Execute an authorized write with read-back verification.
        
        Args:
            point_id: Native address / point identifier (e.g. 'Y20')
            state: Desired state (0 or 1)
            auth_pin: Operator authorization PIN
            
        Returns:
            OutputControlResult with read-back status
        """
        pass
