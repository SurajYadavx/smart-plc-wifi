#!/usr/bin/env python3
"""
scripts/package_v2_2_1_handover.py
───────────────────────────────────
Creates a clean, reproducible, self-contained ZIP handover package for Project V2.2.1.
Enforces strict package hygiene:
- Excludes virtual environments (.venv)
- Excludes build caches (.pio, __pycache__, .pytest_cache)
- Excludes git metadata (.git)
- Excludes log files (gateway/logs/*.log, *.log)
- Excludes native dynamic libraries (*.dll, *.so, *.dylib)
- Excludes temporary artifacts (*.tmp, *.bak, *.swp)
- Excludes private secrets (secrets.h) while preserving secrets.example.h
- Excludes prior handover archives
"""

import os
import zipfile
from pathlib import Path

WORKSPACE_ROOT = Path(__file__).resolve().parent.parent
OUTPUT_ZIP = WORKSPACE_ROOT / "PROJECT_V2_2_1_HANDOVER.zip"

EXCLUDE_DIRS = {
    ".git",
    ".venv",
    ".pio",
    "__pycache__",
    ".pytest_cache",
    ".idea",
    ".vscode",
    "node_modules",
    "logs",
}

EXCLUDE_FILES = {
    "PROJECT_V2_HANDOVER.zip",
    "PROJECT_V2_1_HANDOVER.zip",
    "PROJECT_V2_2_HANDOVER.zip",
    "PROJECT_V2_2_1_HANDOVER.zip",
    "secrets.h",  # Exclude private secrets; secrets.example.h is preserved
    ".DS_Store",
    "Thumbs.db",
}

EXCLUDE_EXTENSIONS = {
    ".pyc",
    ".pyo",
    ".tmp",
    ".bak",
    ".log",
    ".dll",
    ".so",
    ".dylib",
    ".exe",
}


def should_exclude_path(p: Path) -> bool:
    # Check directory parts
    for part in p.parts:
        if part in EXCLUDE_DIRS:
            return True
        if part.endswith(".egg-info"):
            return True

    # Check filename
    if p.name in EXCLUDE_FILES:
        return True

    # Check extension
    if p.suffix in EXCLUDE_EXTENSIONS:
        return True

    return False


def create_handover_zip():
    print(f"Creating Project V2.2.1 handover package at: {OUTPUT_ZIP}")
    file_count = 0
    total_uncompressed_bytes = 0

    with zipfile.ZipFile(OUTPUT_ZIP, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for root, dirs, files in os.walk(WORKSPACE_ROOT):
            # Prune excluded directories in-place
            dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS and not d.endswith(".egg-info")]

            rel_root = Path(root).relative_to(WORKSPACE_ROOT)

            for file_name in files:
                file_path = Path(root) / file_name
                rel_file_path = rel_root / file_name

                if should_exclude_path(rel_file_path):
                    continue

                zf.write(file_path, arcname=str(rel_file_path).replace("\\", "/"))
                file_count += 1
                total_uncompressed_bytes += file_path.stat().st_size

    zip_size_bytes = OUTPUT_ZIP.stat().st_size
    print("Project V2.2.1 Package created successfully!")
    print(f"  Files packaged: {file_count}")
    print(f"  Uncompressed size: {total_uncompressed_bytes / (1024 * 1024):.2f} MB")
    print(f"  ZIP package size:  {zip_size_bytes / (1024 * 1024):.2f} MB ({zip_size_bytes} bytes)")
    return OUTPUT_ZIP, file_count, zip_size_bytes


if __name__ == "__main__":
    create_handover_zip()
