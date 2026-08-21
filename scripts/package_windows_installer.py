"""Build a portable Windows installer package from a gated release build."""
from __future__ import annotations

import argparse
import json
import shutil
import zipfile
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", type=Path, default=Path("build"))
    ap.add_argument("--output", type=Path, default=Path("dist/KengaOS-Installer-x86_64.zip"))
    args = ap.parse_args()
    build = args.build.resolve()
    manifest = build / "release-manifest.json"
    if not manifest.is_file():
        raise SystemExit("release-manifest.json is missing; run release_gate.py first")
    data = json.loads(manifest.read_text(encoding="utf-8"))
    files = ["release-manifest.json", "scripts/install-kengaos.ps1"]
    files += list(data["artifacts"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
        for relative in files:
            source = (build / relative) if relative != "scripts/install-kengaos.ps1" else Path(__file__).with_name("install-kengaos.ps1")
            if not source.is_file():
                raise SystemExit(f"missing package file: {source}")
            archive.write(source, relative)
    print(f"installer package: {args.output} ({args.output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
