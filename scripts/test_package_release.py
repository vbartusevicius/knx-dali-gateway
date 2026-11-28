"""Unit tests for scripts/package_release.py."""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from package_release import (
    CHECKSUMS_NAME,
    FORBIDDEN_TOP_LEVEL_SUFFIXES,
    KNXPROD_NAME,
    REPO,
    VARIANTS,
    package_release,
)


class PackageReleaseTests(unittest.TestCase):
    def _create_fake_inputs(self, root: Path) -> tuple[Path, Path]:
        fw_dir = root / "firmware"
        fw_dir.mkdir(parents=True)
        (fw_dir / "firmware-full-usb.bin").write_bytes(b"FULL_USB_IMAGE" * 16)
        (fw_dir / "firmware.bin").write_bytes(b"OTA_IMAGE" * 16)
        # Intermediate clutter in the build directory must never be copied out.
        (fw_dir / "firmware.elf").write_bytes(b"ELF")
        (fw_dir / "firmware.map").write_text("MAP", encoding="utf-8")
        (fw_dir / "helper.py").write_text("print(1)\n", encoding="utf-8")

        knxprod = root / KNXPROD_NAME
        knxprod.write_bytes(b"PK\x03\x04knxprod-payload")
        return fw_dir, knxprod

    def test_full_release_against_committed_hardware(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            fw_dir, knxprod = self._create_fake_inputs(tmp_path)
            out_dir = tmp_path / "release"
            notes_path = tmp_path / "release-notes.md"

            staged = package_release(
                output_dir=out_dir,
                hardware_dir=REPO / "hardware/releases",
                variants=("v2", "v3"),
                firmware_dir=fw_dir,
                knxprod_path=knxprod,
                notes_out=notes_path,
                clean=True,
            )

            expected_files = [
                "firmware-full-usb.bin",
                "firmware.bin",
                KNXPROD_NAME,
                "knx-dali-gateway-v2-hardware.zip",
                "knx-dali-gateway-v3-hardware.zip",
                CHECKSUMS_NAME,
            ]
            self.assertEqual([p.name for p in staged], expected_files)
            self.assertEqual(sorted(p.name for p in out_dir.iterdir()), sorted(expected_files))

            for path in out_dir.iterdir():
                self.assertNotIn(path.suffix.lower(), FORBIDDEN_TOP_LEVEL_SUFFIXES)

            # Verify SHA256SUMS.txt covers every non-checksum release file accurately.
            checksum_lines = (out_dir / CHECKSUMS_NAME).read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(checksum_lines), len(expected_files) - 1)
            for line, expected_name in zip(checksum_lines, expected_files[:-1]):
                digest, name = line.split("  ")
                self.assertEqual(name, expected_name)
                actual = hashlib.sha256((out_dir / name).read_bytes()).hexdigest()
                self.assertEqual(digest, actual)

            # Verify hardware zip layouts and variant isolation.
            for variant_name, spec in VARIANTS.items():
                zip_path = out_dir / spec.archive_name
                with zipfile.ZipFile(zip_path, "r") as zf:
                    names = zf.namelist()
                self.assertFalse(any(".DS_Store" in n for n in names), variant_name)
                self.assertFalse(any(n.endswith((".py", ".sh", ".mjs")) for n in names), variant_name)
                self.assertIn("README.md", names)
                self.assertIn("assembly_parts.csv", names)
                self.assertIn("order_lcsc.csv", names)
                self.assertIn("order_elsewhere.csv", names)
                self.assertTrue(any(n.startswith("enclosure/") for n in names))
                for board in spec.boards:
                    self.assertTrue(
                        any(n.startswith(f"boards/{board}/") for n in names),
                        f"{variant_name} missing boards/{board}/",
                    )
                if variant_name == "v3":
                    self.assertFalse(any(n.startswith("boards/interface/") for n in names))
                    self.assertTrue(any(n.startswith("rework/") for n in names))
                else:
                    self.assertFalse(any(n.startswith("rework/") for n in names))

            # Verify Markdown release table lists every staged file.
            notes = notes_path.read_text(encoding="utf-8")
            self.assertIn("| File | Use |", notes)
            for filename in expected_files:
                self.assertIn(f"| `{filename}` |", notes)

    def test_hardware_archive_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            out_a = tmp_path / "a"
            out_b = tmp_path / "b"
            package_release(
                output_dir=out_a,
                hardware_dir=REPO / "hardware/releases",
                variants=("v3",),
                firmware_dir=None,
                knxprod_path=None,
                clean=True,
            )
            package_release(
                output_dir=out_b,
                hardware_dir=REPO / "hardware/releases",
                variants=("v3",),
                firmware_dir=None,
                knxprod_path=None,
                clean=True,
            )
            self.assertEqual(
                (out_a / "knx-dali-gateway-v3-hardware.zip").read_bytes(),
                (out_b / "knx-dali-gateway-v3-hardware.zip").read_bytes(),
            )
            self.assertEqual(
                (out_a / CHECKSUMS_NAME).read_text(encoding="utf-8"),
                (out_b / CHECKSUMS_NAME).read_text(encoding="utf-8"),
            )

    def test_rejects_unexpected_board_in_variant(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            hw_v3 = tmp_path / "hw/v3"
            for name in ("main", "ui", "panel", "interface", "enclosure"):
                (hw_v3 / name).mkdir(parents=True)
                (hw_v3 / name / "file.txt").write_text("x", encoding="utf-8")
            for name in ("README.md", "assembly_parts.csv", "order_lcsc.csv", "order_elsewhere.csv"):
                (hw_v3 / name).write_text("x", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unexpected directories.*interface"):
                package_release(
                    output_dir=tmp_path / "out",
                    hardware_dir=tmp_path / "hw",
                    variants=("v3",),
                    firmware_dir=None,
                    knxprod_path=None,
                    clean=True,
                )

    def test_rejects_missing_firmware_image(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            fw_dir = tmp_path / "fw"
            fw_dir.mkdir()
            (fw_dir / "firmware.bin").write_bytes(b"OTA")
            knxprod = tmp_path / KNXPROD_NAME
            knxprod.write_bytes(b"KNXPROD")

            with self.assertRaises(FileNotFoundError):
                package_release(
                    output_dir=tmp_path / "out",
                    hardware_dir=REPO / "hardware/releases",
                    variants=("v3",),
                    firmware_dir=fw_dir,
                    knxprod_path=knxprod,
                    clean=True,
                )

    def test_refuses_repo_root_as_output_dir(self) -> None:
        with self.assertRaisesRegex(ValueError, "repository root"):
            package_release(
                output_dir=REPO,
                hardware_dir=REPO / "hardware/releases",
                variants=("v3",),
                firmware_dir=None,
                knxprod_path=None,
                clean=False,
            )


if __name__ == "__main__":
    unittest.main()
