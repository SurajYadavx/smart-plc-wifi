"""
tests/test_dual_plc_mc.py
─────────────────────────
Comprehensive test suite for Mitsubishi Q-Series & R-Series Dual-PLC MC Protocol 3E Binary
communication, bit packing/unpacking, allowlist enforcement, Y0 banning, PIN authorization,
and read-back verification against the deterministic mock MC server.
"""

import sys
import unittest
from pathlib import Path

# Ensure repo root on path
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from adapters.mitsubishi.mc_control import (
    MitsubishiMcControlAdapter,
    _build_mc_3e_read_bit_frame,
    _build_mc_3e_write_bit_frame,
)
from core.control_interface import ControlCommandStatus
from core.simulated_control_adapter import SimulatedPlcControlAdapter
from tests.mock_mc_server import MockMcProtocolServer


class TestDualPlcMcProtocol(unittest.TestCase):
    """Test suite covering binary frame generation, control allowlist, and mock server verification."""

    def test_q_series_read_frame_structure(self):
        """Verify Q-Series 3E binary batch read frame (0x0401, sub 0x0001, 3-byte addr)."""
        frame = _build_mc_3e_read_bit_frame("X0", points_count=8, plc_type="Q")
        self.assertEqual(len(frame), 21)
        self.assertEqual(frame[0:2], b"\x50\x00")  # Subheader
        self.assertEqual(frame[11:13], b"\x01\x04")  # 0x0401 Batch Read
        self.assertEqual(frame[13:15], b"\x01\x00")  # 0x0001 Subcommand
        self.assertEqual(frame[18], 0x9C)  # X device code
        self.assertEqual(frame[19:21], b"\x08\x00")  # 8 points

    def test_iqr_series_read_frame_structure(self):
        """Verify iQ-R 3E binary batch read frame (0x0401, sub 0x0003, 4-byte addr)."""
        frame = _build_mc_3e_read_bit_frame("X0", points_count=8, plc_type="iQ-R")
        self.assertEqual(len(frame), 23)
        self.assertEqual(frame[0:2], b"\x50\x00")
        self.assertEqual(frame[11:13], b"\x01\x04")
        self.assertEqual(frame[13:15], b"\x03\x00")  # 0x0003 Subcommand for iQ-R
        self.assertEqual(frame[19:21], b"\x9C\x00")  # 2-byte device code for iQ-R
        self.assertEqual(frame[21:23], b"\x08\x00")

    def test_q_series_write_frame_structure(self):
        """Verify Q-Series 3E binary batch write frame (0x1401, sub 0x0001)."""
        frame = _build_mc_3e_write_bit_frame("Y20", state=1, plc_type="Q")
        self.assertEqual(len(frame), 22)
        self.assertEqual(frame[11:13], b"\x01\x14")  # 0x1401 Batch Write
        self.assertEqual(frame[13:15], b"\x01\x00")  # 0x0001 Subcommand
        self.assertEqual(frame[15], 0x20)  # Y20 address
        self.assertEqual(frame[18], 0x9D)  # Y device code
        self.assertEqual(frame[19:21], b"\x01\x00")  # 1 point
        self.assertEqual(frame[21], 0x10)  # ON in high nibble

    def test_banned_y0_is_rejected_in_control_adapter(self):
        """CRITICAL: Y0 must be strictly rejected as a banned point."""
        adapter = MitsubishiMcControlAdapter(
            device_id="melsec_q03",
            host="127.0.0.1",
            port=5010,
            permitted_outputs={"Y20"},
        )
        self.assertFalse(adapter.validate_permission("Y0"))
        self.assertFalse(adapter.validate_permission("Y00"))

        res = adapter.execute_write("Y0", 1, "1234")
        self.assertFalse(res.success)
        self.assertEqual(res.status, ControlCommandStatus.FORBIDDEN_POINT)
        self.assertIn("strictly forbidden", res.error_message)

    def test_unpermitted_point_is_rejected(self):
        """Points outside allowlist (e.g. Y21 when only Y20 is allowed) must be rejected."""
        adapter = MitsubishiMcControlAdapter(
            device_id="melsec_q03",
            host="127.0.0.1",
            port=5010,
            permitted_outputs={"Y20"},
        )
        res = adapter.execute_write("Y21", 1, "1234")
        self.assertFalse(res.success)
        self.assertEqual(res.status, ControlCommandStatus.FORBIDDEN_POINT)
        self.assertIn("not in the permitted outputs allowlist", res.error_message)

    def test_invalid_pin_is_rejected(self):
        """Invalid operator PIN must result in UNAUTHORIZED status."""
        adapter = MitsubishiMcControlAdapter(
            device_id="melsec_q03",
            host="127.0.0.1",
            port=5010,
            permitted_outputs={"Y20"},
            operator_pin="1234",
        )
        res = adapter.execute_write("Y20", 1, "wrong_pin")
        self.assertFalse(res.success)
        self.assertEqual(res.status, ControlCommandStatus.UNAUTHORIZED)

    def test_simulated_control_adapter_readback_verification(self):
        """Verify SimulatedPlcControlAdapter normal operation and readback failure injection."""
        sim = SimulatedPlcControlAdapter(permitted_outputs={"Y20"}, operator_pin="1234")
        
        # 1. Turn ON Y20
        res = sim.execute_write("Y20", 1, "1234")
        self.assertTrue(res.success)
        self.assertTrue(res.verified)
        self.assertEqual(res.read_back_state, 1)
        self.assertEqual(res.status, ControlCommandStatus.OUTPUT_CONFIRMED)

        # 2. Turn OFF Y20 (valid zero invariant)
        res_off = sim.execute_write("Y20", 0, "1234")
        self.assertTrue(res_off.success)
        self.assertTrue(res_off.verified)
        self.assertEqual(res_off.read_back_state, 0)
        self.assertEqual(res_off.status, ControlCommandStatus.OUTPUT_CONFIRMED)

        # 3. Simulate read-back mismatch failure
        sim.simulate_readback_failure = True
        res_fail = sim.execute_write("Y20", 1, "1234")
        self.assertFalse(res_fail.success)
        self.assertFalse(res_fail.verified)
        self.assertEqual(res_fail.status, ControlCommandStatus.WRITE_FAILED)
        self.assertIn("read-back mismatch", res_fail.error_message)

    def test_mock_server_q_series_full_write_and_readback(self):
        """End-to-end integration test with Mock MC Server for Q-Series."""
        server = MockMcProtocolServer(plc_type="Q")
        port = server.start()
        try:
            adapter = MitsubishiMcControlAdapter(
                device_id="melsec_q03",
                host="127.0.0.1",
                port=port,
                plc_type="Q",
                permitted_outputs={"Y20"},
                operator_pin="1234",
            )
            # Initial state in server output table: Y20 = 0
            self.assertEqual(server.outputs[0x20], 0)

            # Write ON to Y20
            res = adapter.execute_write("Y20", 1, "1234")
            self.assertTrue(res.success)
            self.assertTrue(res.verified)
            self.assertEqual(res.read_back_state, 1)
            self.assertEqual(res.status, ControlCommandStatus.OUTPUT_CONFIRMED)
            self.assertEqual(server.outputs[0x20], 1)

            # Write OFF to Y20
            res_off = adapter.execute_write("Y20", 0, "1234")
            self.assertTrue(res_off.success)
            self.assertTrue(res_off.verified)
            self.assertEqual(res_off.read_back_state, 0)
            self.assertEqual(server.outputs[0x20], 0)
        finally:
            server.stop()

    def test_mock_server_iqr_series_full_write_and_readback(self):
        """End-to-end integration test with Mock MC Server for iQ-R Series."""
        server = MockMcProtocolServer(plc_type="iQ-R")
        port = server.start()
        try:
            adapter = MitsubishiMcControlAdapter(
                device_id="melsec_r04",
                host="127.0.0.1",
                port=port,
                plc_type="iQ-R",
                permitted_outputs={"Y20"},
                operator_pin="5678",
            )
            res = adapter.execute_write("Y20", 1, "5678")
            self.assertTrue(res.success)
            self.assertTrue(res.verified)
            self.assertEqual(res.read_back_state, 1)
            self.assertEqual(server.outputs[0x20], 1)
        finally:
            server.stop()

    def test_dual_plc_independent_operation(self):
        """Verify dual-PLC isolation: offline R-PLC does not affect online Q-PLC."""
        q_server = MockMcProtocolServer(plc_type="Q")
        q_port = q_server.start()

        # R-PLC is offline (pointing to non-listening port)
        dead_port = q_port + 999

        try:
            q_adapter = MitsubishiMcControlAdapter(
                device_id="melsec_q03",
                host="127.0.0.1",
                port=q_port,
                permitted_outputs={"Y20"},
            )
            r_adapter = MitsubishiMcControlAdapter(
                device_id="melsec_r04",
                host="127.0.0.1",
                port=dead_port,
                timeout_sec=0.2,
                permitted_outputs={"Y20"},
            )

            # Q write succeeds
            q_res = q_adapter.execute_write("Y20", 1, "1234")
            self.assertTrue(q_res.success)

            # R write fails due to disconnection
            r_res = r_adapter.execute_write("Y20", 1, "1234")
            self.assertFalse(r_res.success)
            self.assertEqual(r_res.status, ControlCommandStatus.PLC_DISCONNECTED)

            # Q still healthy afterwards
            q_res2 = q_adapter.execute_write("Y20", 0, "1234")
            self.assertTrue(q_res2.success)
            self.assertEqual(q_res2.read_back_state, 0)
        finally:
            q_server.stop()

    def test_fastapi_control_endpoint_lifecycle(self):
        """Verify FastAPI REST API control endpoint validation and responses."""
        from agent.api import create_app
        from agent.service import LocalAgentService
        from core.config_loader import load_yaml_config
        from fastapi.testclient import TestClient

        cfg = load_yaml_config(REPO_ROOT / "config" / "devices.yaml")
        service = LocalAgentService(config=cfg, simulation_mode=True)
        app = create_app(service=service, enable_control=True)
        client = TestClient(app)

        # 1. System endpoint
        sys_resp = client.get("/api/v1/system")
        self.assertEqual(sys_resp.status_code, 200)
        sys_data = sys_resp.json()
        self.assertEqual(sys_data["firmware_version"], "2.0.0")
        self.assertEqual(sys_data["network"]["ssid"], "RUBY")

        # 2. Banned point Y0 -> 403 Forbidden
        y0_resp = client.post(
            "/api/v1/devices/melsec_q03/outputs/Y0/command",
            json={"state": 1, "auth_pin": "1234"},
        )
        self.assertEqual(y0_resp.status_code, 403)
        self.assertEqual(y0_resp.json()["status"], "FORBIDDEN_POINT")

        # 3. Invalid PIN -> 401 Unauthorized
        pin_resp = client.post(
            "/api/v1/devices/melsec_q03/outputs/Y20/command",
            json={"state": 1, "auth_pin": "bad_pin"},
        )
        self.assertEqual(pin_resp.status_code, 401)

        # 4. Valid write to permitted Y20 in simulation -> 200 OK with OUTPUT_CONFIRMED
        ok_resp = client.post(
            "/api/v1/devices/melsec_q03/outputs/Y20/command",
            json={"state": 1, "auth_pin": "1234"},
        )
        self.assertEqual(ok_resp.status_code, 200)
        res_data = ok_resp.json()
        self.assertTrue(res_data["success"])
        self.assertTrue(res_data["verified"])
        self.assertEqual(res_data["read_back_state"], 1)
        self.assertEqual(res_data["status"], "OUTPUT_CONFIRMED")


if __name__ == "__main__":
    unittest.main()
