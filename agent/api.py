"""
agent/api.py
────────────
FastAPI application for the Universal PLC Windows Local Agent.
Exposes clean, versioned, strictly read-only REST endpoints for industrial
monitoring, diagnostics, and telemetry.
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Dict, List, Optional

from fastapi import Depends, FastAPI, HTTPException, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from agent.security import SecurityPolicy, validate_device_id
from agent.service import LocalAgentService

logger = logging.getLogger(__name__)


def create_app(
    service: LocalAgentService,
    security_policy: Optional[SecurityPolicy] = None,
    static_dir: Optional[Path] = None,
    allowed_origins: Optional[List[str]] = None,
    enable_control: bool = False,
) -> FastAPI:
    """
    Factory creating the FastAPI local agent application.
    Binds service instance, security policy, CORS controls, and static dashboard assets.
    """
    policy = security_policy or SecurityPolicy()
    web_dir = static_dir or (Path(__file__).resolve().parent.parent / "web")

    app = FastAPI(
        title="Universal PLC Windows Local Agent API",
        version="1.0.0",
        description=(
            "Secure, local read-only monitoring service for multi-vendor industrial PLCs "
            "(Mitsubishi, Modbus TCP, Simulated, and future OPC UA)."
        ),
        docs_url="/docs",
        redoc_url="/redoc",
        openapi_url="/openapi.json",
    )

    # Attach service and policy to app state
    app.state.service = service
    app.state.security_policy = policy

    # -------------------------------------------------------------------------
    # CORS Configuration (Strict local origin allowlist; no wildcard origins)
    # -------------------------------------------------------------------------
    cors_origins = allowed_origins if allowed_origins is not None else [
        "http://localhost:8080",
        "http://127.0.0.1:8080",
        "http://localhost:3000",
        "http://127.0.0.1:3000",
    ]
    if cors_origins:
        app.add_middleware(
            CORSMiddleware,
            allow_origins=cors_origins,
            allow_credentials=True,
            allow_methods=["GET", "POST", "OPTIONS"],
            allow_headers=["*"],
        )

    # -------------------------------------------------------------------------
    # Root & Information Endpoints
    # -------------------------------------------------------------------------

    @app.get("/api", tags=["General"])
    def get_api_catalog() -> Dict[str, Any]:
        """API Catalog providing service identification and navigation links."""
        return {
            "name": "Universal PLC Windows Local Agent API",
            "version": "1.0.0",
            "docs": "/docs",
            "health": "/api/v1/health",
            "devices": "/api/v1/devices",
            "agent_info": "/api/v1/agent/info",
        }

    @app.get("/", tags=["General"], include_in_schema=False)
    def get_root(request: Request) -> Any:
        """
        Serve local supervisory web dashboard if present, or API catalog JSON if requested.
        """
        accept = request.headers.get("accept", "")
        # If client explicitly asks for JSON and not HTML, return API catalog
        if "application/json" in accept and "text/html" not in accept:
            return get_api_catalog()

        index_file = web_dir / "index.html"
        if index_file.is_file():
            return FileResponse(str(index_file), media_type="text/html")
        return get_api_catalog()

    @app.get("/index.html", include_in_schema=False)
    def serve_index_html() -> Any:
        """Direct access to index.html dashboard."""
        index_file = web_dir / "index.html"
        if index_file.is_file():
            return FileResponse(str(index_file), media_type="text/html")
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="index.html not found.")

    @app.get("/test.html", include_in_schema=False)
    def serve_test_html() -> Any:
        """Direct access to test.html offline suite."""
        test_file = web_dir / "test.html"
        if test_file.is_file():
            return FileResponse(str(test_file), media_type="text/html")
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="test.html not found.")

    # Mount static assets directory (/assets)
    if web_dir.is_dir() and (web_dir / "assets").is_dir():
        app.mount("/assets", StaticFiles(directory=str(web_dir / "assets")), name="assets")
    # Versioned API Router (/api/v1)
    # -------------------------------------------------------------------------

    @app.get(
        "/api/v1/system",
        tags=["System"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_system() -> Dict[str, Any]:
        """
        Unified system and network status matching the ESP32 Gateway API contract.
        """
        return service.get_system_info()

    @app.get(
        "/api/v1/health",
        tags=["System"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_health() -> Dict[str, Any]:
        """
        Agent health status, uptime, API version, and monitoring engine operational state.
        """
        return service.get_health()

    @app.get(
        "/api/v1/agent/info",
        tags=["System"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_agent_info() -> Dict[str, Any]:
        """
        Agent version, registered protocol adapters, operating mode, and capabilities.
        """
        return service.get_agent_info()

    @app.get(
        "/api/v1/config/status",
        tags=["Configuration"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_config_status() -> Dict[str, Any]:
        """
        Configuration file status, validity, and non-sensitive device summary.
        """
        return service.get_config_status()

    # -------------------------------------------------------------------------
    # Device Inventory & Telemetry Endpoints
    # -------------------------------------------------------------------------

    @app.get(
        "/api/v1/devices",
        tags=["Devices"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def list_devices() -> List[Dict[str, Any]]:
        """
        List all registered devices, connection states, and last successful reading times.
        """
        return service.get_devices()

    @app.get(
        "/api/v1/devices/{device_id}",
        tags=["Devices"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_device(device_id: str) -> Dict[str, Any]:
        """
        Detailed definition, point map, and runtime tracking for a single device.
        """
        clean_id = validate_device_id(device_id)
        dev = service.get_device(clean_id)
        if not dev:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Device '{clean_id}' not found.",
            )
        return dev

    @app.get(
        "/api/v1/devices/{device_id}/telemetry",
        tags=["Telemetry"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_device_telemetry(device_id: str) -> Dict[str, Any]:
        """
        Latest normalized telemetry readings, engineering units, data quality, and scan latency.
        Distinguishes valid zero values from communication failures.
        """
        clean_id = validate_device_id(device_id)
        telemetry = service.get_device_telemetry(clean_id)
        if telemetry is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Device '{clean_id}' not found.",
            )
        return telemetry

    @app.get(
        "/api/v1/devices/{device_id}/diagnostics",
        tags=["Diagnostics"],
        dependencies=[Depends(policy.verify_auth)],
    )
    def get_device_diagnostics(device_id: str) -> Dict[str, Any]:
        """
        Low-level driver diagnostics, packet counters, latency, and sanitized connection details.
        """
        clean_id = validate_device_id(device_id)
        diag = service.get_device_diagnostics(clean_id)
        if diag is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Device '{clean_id}' not found.",
            )
        return diag

    if enable_control:
        @app.post(
            "/api/v1/devices/{device_id}/outputs/{point_id}/command",
            tags=["Control"],
        )
        async def post_output_command(
            device_id: str,
            point_id: str,
            request: Request,
        ) -> JSONResponse:
            """
            Execute an authorized write command to a permitted PLC output point.
            Strictly enforces allowlist, bans Y0, checks authorization PIN,
            and verifies write via read-back confirmation.
            """
            clean_dev = validate_device_id(device_id)
            clean_pt = point_id.strip().upper()

            if clean_pt in ("Y0", "Y00", "Y000"):
                return JSONResponse(
                    status_code=status.HTTP_403_FORBIDDEN,
                    content={
                        "success": False,
                        "device_id": clean_dev,
                        "point": clean_pt,
                        "status": "FORBIDDEN_POINT",
                        "error": "Point Y0 is strictly forbidden by project safety rules.",
                    },
                )

            try:
                body = await request.json()
            except Exception:
                return JSONResponse(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    content={
                        "success": False,
                        "device_id": clean_dev,
                        "point": clean_pt,
                        "status": "INVALID_PAYLOAD",
                        "error": "Invalid or missing JSON payload.",
                    },
                )

            req_state = body.get("state")
            if isinstance(req_state, bool) or not isinstance(req_state, int) or req_state not in (0, 1):
                return JSONResponse(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    content={
                        "success": False,
                        "device_id": clean_dev,
                        "point": clean_pt,
                        "status": "INVALID_STATE",
                        "error": "Field 'state' must be exact integer 0 (OFF) or 1 (ON).",
                    },
                )

            pin = str(body.get("auth_pin") or body.get("pin") or request.headers.get("X-Operator-PIN") or "").strip()
            if not pin:
                return JSONResponse(
                    status_code=status.HTTP_401_UNAUTHORIZED,
                    content={
                        "success": False,
                        "device_id": clean_dev,
                        "point": clean_pt,
                        "status": "PIN_REQUIRED",
                        "error": "Operator PIN is required.",
                    },
                )

            res = service.execute_output_command(
                device_id=clean_dev,
                point_id=clean_pt,
                state=int(req_state),
                auth_pin=pin,
            )

            stat_code = status.HTTP_200_OK
            if res.get("status") in ("UNAUTHORIZED", "PIN_REQUIRED", "INVALID_PIN"):
                stat_code = status.HTTP_401_UNAUTHORIZED
            elif res.get("status") in ("FORBIDDEN_POINT", "DEVICE_NOT_FOUND", "PHYSICAL_WRITES_DISABLED"):
                stat_code = status.HTTP_403_FORBIDDEN
            elif not res.get("success", False):
                stat_code = status.HTTP_502_BAD_GATEWAY

            return JSONResponse(status_code=stat_code, content=res)

    # -------------------------------------------------------------------------
    # Error Handlers
    # -------------------------------------------------------------------------

    @app.exception_handler(HTTPException)
    def http_exception_handler(request: Any, exc: HTTPException) -> JSONResponse:
        return JSONResponse(
            status_code=exc.status_code,
            content={"error": True, "detail": exc.detail},
            headers=getattr(exc, "headers", None),
        )

    @app.exception_handler(Exception)
    def generic_exception_handler(request: Any, exc: Exception) -> JSONResponse:
        logger.error("Unhandled API exception on %s: %s", getattr(request, "url", ""), exc, exc_info=True)
        return JSONResponse(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            content={"error": True, "detail": "Internal server error occurred."},
        )

    return app
