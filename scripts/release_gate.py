"""Verify and manifest a KengaOS release build.

The gate is deliberately independent of GitHub Actions so the same checks can
be run before publishing an ISO locally or in CI.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


REQUIRED = ("kengaos.elf", "kengaos.iso", "iso_root/boot/initrd.img")


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="KengaOS reproducible release gate")
    ap.add_argument("--build", type=Path, default=Path("build"))
    ap.add_argument("--manifest", type=Path)
    args = ap.parse_args()
    root = args.build.resolve()
    missing = [name for name in REQUIRED if not (root / name).is_file()]
    if missing:
        print("release gate: missing: " + ", ".join(missing), file=sys.stderr)
        return 2
    artifacts = {}
    for name in REQUIRED:
        p = root / name
        size = p.stat().st_size
        if size == 0:
            print(f"release gate: empty artifact: {name}", file=sys.stderr)
            return 3
        artifacts[name] = {"bytes": size, "sha256": digest(p)}
    manifest = {"format": 1, "artifacts": artifacts}
    out = args.manifest or (root / "release-manifest.json")
    out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"release gate: OK ({len(artifacts)} artifacts) -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
