"""
tests/mock_mc_server.py
───────────────────────
Deterministic Mitsubishi MC Protocol 3E Binary Mock TCP Server.
Emulates Q03UDECPU and iQ-R Series PLC Ethernet interfaces for unit and integration testing.
Handles 0x0401 (Batch Read) and 0x1401 (Batch Write) in bit units with end-code verification.
"""

from __future__ import annotations

import logging
import socket
import threading
import time
from typing import Dict, Optional

logger = logging.getLogger(__name__)


class MockMcProtocolServer:
    """
    Lightweight, deterministic TCP server simulating Mitsubishi MC Protocol 3E Binary.
    Supports both Q-Series (subcommand 0x0001, 3-byte addr) and iQ-R (subcommand 0x0003, 4-byte addr).
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 0,
        plc_type: str = "Q",
    ) -> None:
        self.host = host
        self.requested_port = port
        self.plc_type = plc_type
        self.server_socket: Optional[socket.socket] = None
        self.port: int = port
        self._thread: Optional[threading.Thread] = None
        self._running = False

        # In-memory device tables (values are 0 or 1)
        self.inputs: Dict[int, int] = {i: 0 for i in range(16)}          # X0-XF
        self.outputs: Dict[int, int] = {0x20 + i: 0 for i in range(16)}  # Y20-Y2F

        # Simulation behavior flags
        self.force_error_end_code: int = 0  # 0 = success; >0 = return this MC error code
        self.artificial_delay_sec: float = 0.0
        self.close_on_connect: bool = False

    def start(self) -> int:
        """Start listening on background thread and return allocated port."""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.requested_port))
        self.server_socket.listen(5)
        self.port = self.server_socket.getsockname()[1]
        self._running = True

        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()
        logger.debug("Mock MC Server (%s) started on %s:%d", self.plc_type, self.host, self.port)
        return self.port

    def stop(self) -> None:
        """Stop server and release sockets."""
        self._running = False
        if self.server_socket:
            try:
                self.server_socket.close()
            except Exception:
                pass
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        logger.debug("Mock MC Server stopped on port %d", self.port)

    def _accept_loop(self) -> None:
        while self._running:
            try:
                client_sock, _ = self.server_socket.accept()
                if self.close_on_connect:
                    client_sock.close()
                    continue
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_sock,),
                    daemon=True,
                )
                client_thread.start()
            except Exception:
                break

    def _handle_client(self, sock: socket.socket) -> None:
        sock.settimeout(2.0)
        try:
            while self._running:
                data = sock.recv(2048)
                if not data:
                    break

                if self.artificial_delay_sec > 0:
                    time.sleep(self.artificial_delay_sec)

                response = self._process_request(data)
                if response:
                    sock.sendall(response)
        except Exception:
            pass
        finally:
            try:
                sock.close()
            except Exception:
                pass

    def _process_request(self, data: bytes) -> Optional[bytes]:
        # Minimal 3E binary request length is 21 bytes (Q) or 23 bytes (iQ-R)
        if len(data) < 21:
            return None

        # Verify subheader (0x50 0x00)
        if data[0] != 0x50 or data[1] != 0x00:
            return None

        # Parse command & subcommand (starts after header + rem_len + timer = byte 11)
        # Header: 0..6 (7 bytes)
        # Rem length: 7..8 (2 bytes)
        # Timer: 9..10 (2 bytes)
        # Command: 11..12 (2 bytes, little endian)
        # Subcommand: 13..14 (2 bytes, little endian)
        command = data[11] | (data[12] << 8)
        subcommand = data[13] | (data[14] << 8)

        is_iqr = (subcommand == 0x0003 or self.plc_type == "iQ-R")

        if is_iqr:
            head_addr = data[15] | (data[16] << 8) | (data[17] << 16) | (data[18] << 24)
            dev_code = data[19] | (data[20] << 8)
            points = data[21] | (data[22] << 8)
            write_data_offset = 23
        else:
            head_addr = data[15] | (data[16] << 8) | (data[17] << 16)
            dev_code = data[18]
            points = data[19] | (data[20] << 8)
            write_data_offset = 21

        # Check simulated error end code
        if self.force_error_end_code != 0:
            return self._build_response(self.force_error_end_code, b"")

        # 1. Batch Read (0x0401)
        if command == 0x0401:
            # dev_code: 0x9C is X, 0x9D is Y
            table = self.inputs if (dev_code & 0xFF) == 0x9C else self.outputs
            packed = bytearray((points + 1) // 2)
            for i in range(points):
                val = table.get(head_addr + i, 0)
                byte_idx = i // 2
                bit_shift = 4 if (i % 2 == 0) else 0
                packed[byte_idx] |= (val & 0x01) << bit_shift
            return self._build_response(0, bytes(packed))

        # 2. Batch Write (0x1401)
        elif command == 0x1401:
            table = self.inputs if (dev_code & 0xFF) == 0x9C else self.outputs
            for i in range(points):
                byte_idx = write_data_offset + (i // 2)
                if byte_idx < len(data):
                    bit_shift = 4 if (i % 2 == 0) else 0
                    val = 1 if (data[byte_idx] & (1 << bit_shift)) else 0
                    table[head_addr + i] = val
            # Write response has 0 data bytes, just end code
            return self._build_response(0, b"")

        return None

    def _build_response(self, end_code: int, data_bytes: bytes) -> bytes:
        resp = bytearray()
        resp.extend((0xD0, 0x00))       # Subheader
        resp.append(0x00)               # Network No
        resp.append(0xFF)               # PC No
        resp.extend((0xFF, 0x03))       # Dest Module I/O
        resp.append(0x00)               # Dest Module Station
        # Response length: 2 (end code) + len(data_bytes)
        resp_len = 2 + len(data_bytes)
        resp.extend((resp_len & 0xFF, (resp_len >> 8) & 0xFF))
        # End code (little endian)
        resp.extend((end_code & 0xFF, (end_code >> 8) & 0xFF))
        resp.extend(data_bytes)
        return bytes(resp)
