#!/usr/bin/env python3
"""
Release Publisher Script: MS CP V3 (Smart PLC over WiFi)
=========================================================
Automates pre-flight hygiene verification, repository isolation enforcement,
and safe public GitHub publication using the GitHub CLI (gh).

Enforces:
1. Zero credential leaks (fail-closed check on tracked files and example PINs)
2. Strict repository isolation (prohibits touching legacy mitsubishi-q-plc-app)
3. Target new public repository creation via `gh repo create`
4. Safe branch push to new repository remote
"""

import os
import sys
import subprocess
import argparse
import re
from pathlib import Path

LEGACY_REPOS = [
    "mitsubishi-q-plc-app",
    "SurajYadavx/mitsubishi-q-plc-app",
    "SurajYadavx/mitsubishi-q-plc-app.git",
]

DEFAULT_REPO_NAME = "smart-plc-wifi"
DEFAULT_REMOTE_NAME = "v3-release"

REPO_ROOT = Path(__file__).resolve().parent.parent


def run_cmd(cmd, cwd=REPO_ROOT, check=True):
    """Run a shell command and return stdout string."""
    res = subprocess.run(
        cmd,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        shell=isinstance(cmd, str)
    )
    if check and res.returncode != 0:
        raise RuntimeError(f"Command failed ({res.returncode}): {cmd}\nStderr: {res.stderr.strip()}")
    return res.stdout.strip(), res.stderr.strip(), res.returncode


def verify_fail_closed_hygiene():
    """Verify zero secret leaks and empty example PINs."""
    print("[1/5] Running Pre-Flight Security & Secret Hygiene Audit...")
    
    # 1. Check tracked files for forbidden secrets
    stdout, _, _ = run_cmd(["git", "ls-files"])
    tracked_files = stdout.splitlines()
    
    forbidden_files = ["secrets.h", "secrets.ini", "ca_cert.h", ".env"]
    for tf in tracked_files:
        name = Path(tf).name
        if name in forbidden_files and not name.endswith(".example") and not name.endswith(".example.h"):
            raise ValueError(f"FATAL: Secret file '{tf}' is tracked by Git! Untrack before publishing.")

    # 2. Check secrets.example.h PIN
    sec_example = REPO_ROOT / "firmware" / "esp32_local_gateway" / "src" / "secrets.example.h"
    if sec_example.exists():
        content = sec_example.read_text(encoding="utf-8")
        pin_match = re.search(r'#define\s+SECRET_OPERATOR_PIN\s+"([^"]*)"', content)
        if pin_match:
            pin_val = pin_match.group(1)
            if pin_val != "":
                raise ValueError(
                    f"FATAL: secrets.example.h SECRET_OPERATOR_PIN is '{pin_val}'! "
                    "Must be empty string (\"\") to fail closed."
                )

    print("      -> Zero credential leaks detected. Example templates fail closed safely.")


def verify_git_clean():
    """Verify working tree status."""
    print("[2/5] Checking Git Working Tree Status...")
    stdout, _, _ = run_cmd(["git", "status", "--porcelain"])
    if stdout.strip():
        print("      -> Note: Working tree has uncommitted modifications:")
        for line in stdout.splitlines()[:5]:
            print(f"         {line}")
    else:
        print("      -> Working tree is clean.")


def verify_gh_cli():
    """Verify GitHub CLI is installed and authenticated."""
    print("[3/5] Verifying GitHub CLI (gh) Readiness...")
    try:
        stdout, _, code = run_cmd(["gh", "--version"], check=False)
        if code != 0:
            return False, "gh is not installed or not in PATH."
    except Exception as e:
        return False, str(e)

    stdout, stderr, code = run_cmd(["gh", "auth", "status"], check=False)
    if code != 0:
        return False, f"gh is not authenticated: {stderr}"
    
    print("      -> GitHub CLI is installed and authenticated.")
    return True, stdout


def verify_repo_isolation(target_repo):
    """Enforce strict repository isolation."""
    print(f"[4/5] Enforcing Strict Repository Isolation for '{target_repo}'...")
    for legacy in LEGACY_REPOS:
        if target_repo.lower() == legacy.lower() or legacy.lower() in target_repo.lower():
            raise ValueError(
                f"FATAL SECURITY VIOLATION: Target repo '{target_repo}' matches legacy repository '{legacy}'!\n"
                "Project rules strictly prohibit pushing to or altering legacy repositories."
            )
    print("      -> Repository isolation verified. Target repository is distinct from all legacy repos.")


def publish_release(target_repo, remote_name, dry_run=False):
    """Publish release to new public GitHub repository."""
    print(f"[5/5] Executing Publication Pipeline (Dry Run: {dry_run})...")
    
    # Check existing remotes
    stdout, _, _ = run_cmd(["git", "remote", "-v"])
    print(f"      Current remotes:\n{stdout}")

    if dry_run:
        print("\n[DRY RUN SUMMARY]")
        print(f"  Target Repository : {target_repo} (PUBLIC)")
        print(f"  New Remote Name   : {remote_name}")
        print(f"  Action            : gh repo create {target_repo} --public --source=. --remote={remote_name} --push")
        print("  Status            : DRY RUN SUCCESSFUL — No network changes made.")
        return

    # Check if remote already exists
    remotes = [line.split()[0] for line in stdout.splitlines() if line]
    
    # Create public repository with gh
    print(f"      Creating new public repository '{target_repo}' via GitHub CLI...")
    create_cmd = [
        "gh", "repo", "create",
        target_repo,
        "--public",
        "--description", "Smart PLC over WiFi (MS CP V3) — Mitsubishi Q/R Series Dual-PLC Supervisory & IoT System",
        f"--remote={remote_name}"
    ]
    
    stdout, stderr, code = run_cmd(create_cmd, check=False)
    if code != 0:
        if "already exists" in stderr or "already exists" in stdout:
            print(f"      Repository '{target_repo}' already exists on GitHub.")
            # Set remote URL if not present
            if remote_name not in remotes:
                repo_url = f"https://github.com/SurajYadavx/{target_repo}.git"
                run_cmd(["git", "remote", "add", remote_name, repo_url])
        else:
            print(f"[ERROR] Failed to create repo via gh:\n{stderr}\n{stdout}")
            print("\nFALLBACK MANUAL INSTRUCTIONS:")
            print(f"1. Go to https://github.com/new and create a new public repository named '{target_repo}'.")
            print(f"2. Add remote: git remote add {remote_name} https://github.com/SurajYadavx/{target_repo}.git")
            print(f"3. Push release: git push {remote_name} HEAD:main")
            return

    # Push to new remote
    print(f"      Pushing current HEAD to {remote_name}/main...")
    push_stdout, push_stderr, push_code = run_cmd(["git", "push", "-u", remote_name, "HEAD:main"], check=False)
    if push_code == 0:
        print(f"\n[SUCCESS] MS CP V3 Published Successfully to:")
        print(f"https://github.com/SurajYadavx/{target_repo}")
    else:
        print(f"[ERROR] Push failed:\n{push_stderr}")
        print("Run manual push:")
        print(f"git push {remote_name} HEAD:main")


def main():
    parser = argparse.ArgumentParser(description="Publish MS CP V3 Release to a New Public GitHub Repository")
    parser.add_argument("--repo-name", default=DEFAULT_REPO_NAME, help=f"Target GitHub repository name (default: {DEFAULT_REPO_NAME})")
    parser.add_argument("--remote-name", default=DEFAULT_REMOTE_NAME, help=f"Git remote name to create (default: {DEFAULT_REMOTE_NAME})")
    parser.add_argument("--dry-run", action="store_true", help="Perform pre-flight verification without creating or pushing")
    args = parser.parse_args()

    print("=====================================================================")
    print(" MS CP V3 RELEASE PUBLISHER — PRODUCTION RELEASE PIPELINE           ")
    print("=====================================================================\n")

    try:
        verify_fail_closed_hygiene()
        verify_git_clean()
        gh_ok, gh_msg = verify_gh_cli()
        if not gh_ok:
            print(f"[WARNING] GitHub CLI issue: {gh_msg}")
        verify_repo_isolation(args.repo_name)
        publish_release(args.repo_name, args.remote_name, dry_run=args.dry_run)
    except Exception as e:
        print(f"\n[ABORTED] {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
