import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "scripts" / "release_gate.py"
UPDATER = ROOT / "scripts" / "update_transaction.py"


class ReleaseFaultTests(unittest.TestCase):
    def make_package(self, root: Path) -> Path:
        package = root / "package"
        (package / "iso_root" / "boot").mkdir(parents=True)
        for name, data in {
            "kengaos.elf": b"elf",
            "kengaos.iso": b"iso",
            "iso_root/boot/initrd.img": b"initrd",
        }.items():
            p = package / name
            p.write_bytes(data)
        artifacts = {}
        for name in ("kengaos.elf", "kengaos.iso", "iso_root/boot/initrd.img"):
            data = (package / name).read_bytes()
            artifacts[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        (package / "release-manifest.json").write_text(json.dumps({"format": 1, "artifacts": artifacts}), encoding="utf-8")
        return package

    def test_corrupt_package_is_rejected_and_rollback_is_atomic(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package = self.make_package(root)
            deployment = root / "deployment"
            subprocess.run([sys.executable, str(UPDATER), "install", str(package), str(deployment)], check=True)
            (package / "kengaos.iso").write_bytes(b"tampered")
            failed = subprocess.run([sys.executable, str(UPDATER), "install", str(package), str(deployment)])
            self.assertNotEqual(failed.returncode, 0)
            subprocess.run([sys.executable, str(UPDATER), "rollback", str(deployment)], check=True)
            state = json.loads((deployment / "active.json").read_text(encoding="utf-8"))
            self.assertEqual(state["active"], "a")
            self.assertTrue((deployment / "b" / "kengaos.iso").exists())


if __name__ == "__main__":
    unittest.main()
