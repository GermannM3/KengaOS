"""Transactional A/B updater for KengaOS release artifacts.

The updater operates on a deployment root and is intentionally small enough to
be reused by the future in-kernel updater. A package is staged completely,
verified from release-manifest.json, then activated by replacing one metadata
file. The old slot remains available for rollback.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def load_manifest(package: Path) -> dict:
    manifest = package / "release-manifest.json"
    data = json.loads(manifest.read_text(encoding="utf-8"))
    for name, meta in data.get("artifacts", {}).items():
        artifact = package / name
        if not artifact.is_file() or sha256(artifact) != meta.get("sha256"):
            raise ValueError(f"manifest mismatch: {name}")
    return data


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="op", required=True)
    install = sub.add_parser("install")
    install.add_argument("package", type=Path)
    install.add_argument("root", type=Path)
    sub.add_parser("rollback").add_argument("root", type=Path)
    args = ap.parse_args()

    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    state_path = root / "active.json"
    state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else {"active": "a", "previous": None}
    active = state.get("active", "a")
    if active not in ("a", "b"):
        raise ValueError("invalid active slot")

    if args.op == "rollback":
        previous = state.get("previous")
        if previous not in ("a", "b"):
            print("rollback: no previous slot", file=sys.stderr)
            return 4
        state_path.write_text(json.dumps({"active": previous, "previous": active}, indent=2) + "\n", encoding="utf-8")
        print(f"rollback: active slot is now {previous}")
        return 0

    package = args.package.resolve()
    manifest = load_manifest(package)
    slot = "b" if active == "a" else "a"
    destination = root / slot
    staged = root / (slot + ".staging")
    if staged.exists():
        shutil.rmtree(staged)
    shutil.copytree(package, staged)
    staged.rename(destination)
    state_path.write_text(json.dumps({"active": slot, "previous": active, "version": manifest.get("version")}, indent=2) + "\n", encoding="utf-8")
    print(f"update: activated slot {slot}; rollback target {active}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"update: failed: {exc}", file=sys.stderr)
        raise SystemExit(3)
