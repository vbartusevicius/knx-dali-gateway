"""Shared release asset packager for CI and manual GitHub Releases.

Stages the minimal, self-contained release set into an output directory,
writes SHA256SUMS.txt covering every staged file, and emits a Markdown
`| File | Use |` table for the GitHub Release body.

Examples:
    python3 scripts/package_release.py \\
        --firmware-dir gateway/.pio/build/release_c6 \\
        --knxprod knxprod/build/KnxDaliGateway.knxprod \\
        --output-dir dist/release \\
        --notes-out dist/release-notes.md \\
        --clean

    python3 scripts/package_release.py --hardware-only --output-dir dist/release --clean
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PROJECT = "knx-dali-gateway"
CHECKSUMS_NAME = "SHA256SUMS.txt"
KNXPROD_NAME = "KnxDaliGateway.knxprod"
ZIP_TIMESTAMP = (2026, 1, 1, 0, 0, 0)

EXCLUDED_NAMES = frozenset({".DS_Store", "Thumbs.db", "__pycache__"})
EXCLUDED_ARCHIVE_SUFFIXES = frozenset({".pyc", ".pyo", ".py", ".sh", ".mjs"})
FORBIDDEN_TOP_LEVEL_SUFFIXES = frozenset({".lpf", ".json", ".elf", ".map", ".svg", ".py", ".sh", ".mjs"})

HARDWARE_ROOT_FILES = (
    "README.md",
    "assembly_parts.csv",
    "order_lcsc.csv",
    "order_elsewhere.csv",
)

FIRMWARE_ASSETS = (
    (
        "firmware-full-usb.bin",
        (
            "First USB install at `0x0` on an empty ESP32-C6 "
            "(`esptool.py --chip esp32c6 write_flash 0x0 firmware-full-usb.bin`; V2 & V3, clears settings)"
        ),
    ),
    (
        "firmware.bin",
        (
            "OTA update through the gateway web UI **Firmware** tab "
            "(updates firmware and web UI together, keeps settings; V2 & V3)"
        ),
    ),
)

KNXPROD_USE = (
    "ETS 5.7 / 6 product database "
    "(64 lamp channels, 16 groups, broadcast, 8 DALI-2 input devices)"
)


@dataclass(frozen=True)
class VariantSpec:
    name: str
    label: str
    boards: tuple[str, ...]
    optional_dirs: tuple[str, ...] = ()

    @property
    def archive_name(self) -> str:
        return f"{PROJECT}-{self.name}-hardware.zip"

    @property
    def use_description(self) -> str:
        return (
            f"{self.label} manufacturing and assembly package "
            "(`boards/`, `enclosure/`, BOMs and order lists)"
        )


VARIANTS: dict[str, VariantSpec] = {
    "v2": VariantSpec(
        name="v2",
        label="V2 (2 DIN)",
        boards=("main", "interface", "ui", "panel"),
    ),
    "v3": VariantSpec(
        name="v3",
        label="V3 (1 DIN)",
        boards=("main", "ui", "panel"),
        optional_dirs=("rework",),
    ),
}


@dataclass(frozen=True)
class ReleaseAsset:
    name: str
    use: str


def is_excluded(path: Path) -> bool:
    if any(part in EXCLUDED_NAMES for part in path.parts):
        return True
    return path.suffix.lower() in EXCLUDED_ARCHIVE_SUFFIXES


def write_zip_entry(zf: zipfile.ZipFile, src: Path, arcname: str) -> None:
    info = zipfile.ZipInfo(arcname, date_time=ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    zf.writestr(info, src.read_bytes())


def collect_hardware_entries(variant_dir: Path, spec: VariantSpec) -> list[tuple[Path, str]]:
    if not variant_dir.is_dir():
        raise FileNotFoundError(f"Hardware release directory not found: {variant_dir}")

    allowed_dirs = set(spec.boards) | {"enclosure"} | set(spec.optional_dirs)
    actual_dirs = {
        p.name for p in variant_dir.iterdir()
        if p.is_dir() and p.name not in EXCLUDED_NAMES
    }
    unexpected_dirs = sorted(actual_dirs - allowed_dirs)
    if unexpected_dirs:
        raise ValueError(
            f"{spec.name}: unexpected directories in {variant_dir}: {', '.join(unexpected_dirs)}"
        )

    missing_dirs = sorted((set(spec.boards) | {"enclosure"}) - actual_dirs)
    if missing_dirs:
        raise FileNotFoundError(
            f"{spec.name}: missing required directories in {variant_dir}: {', '.join(missing_dirs)}"
        )

    entries: list[tuple[Path, str]] = []

    for filename in HARDWARE_ROOT_FILES:
        src = variant_dir / filename
        if not src.is_file():
            raise FileNotFoundError(f"{spec.name}: missing required file {src}")
        entries.append((src, filename))

    def add_tree(src_root: Path, arc_prefix: str) -> None:
        for file_path in sorted(p for p in src_root.rglob("*") if p.is_file()):
            rel = file_path.relative_to(src_root)
            if is_excluded(rel):
                continue
            entries.append((file_path, f"{arc_prefix}/{rel.as_posix()}"))

    for board in spec.boards:
        add_tree(variant_dir / board, f"boards/{board}")

    add_tree(variant_dir / "enclosure", "enclosure")

    for opt_dir in spec.optional_dirs:
        opt_path = variant_dir / opt_dir
        if opt_path.is_dir():
            add_tree(opt_path, opt_dir)

    entries.sort(key=lambda item: item[1])
    return entries


def build_hardware_archive(variant_dir: Path, spec: VariantSpec, dest: Path) -> None:
    entries = collect_hardware_entries(variant_dir, spec)
    if not entries:
        raise ValueError(f"{spec.name}: no files collected for hardware archive")

    with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for src, arcname in entries:
            write_zip_entry(zf, src, arcname)

    verify_hardware_archive(dest, spec)


def verify_hardware_archive(archive: Path, spec: VariantSpec) -> None:
    with zipfile.ZipFile(archive, "r") as zf:
        names = zf.namelist()
    if not names:
        raise ValueError(f"{archive.name} is empty")

    for name in names:
        p = Path(name)
        if is_excluded(p):
            raise ValueError(f"{archive.name} contains excluded clutter: {name}")

    board_dirs = {
        Path(n).parts[1]
        for n in names
        if len(Path(n).parts) >= 3 and Path(n).parts[0] == "boards"
    }
    expected_boards = set(spec.boards)
    if board_dirs != expected_boards:
        raise ValueError(
            f"{archive.name}: boards mismatch (got {sorted(board_dirs)}, expected {sorted(expected_boards)})"
        )

    if not any(n.startswith("enclosure/") for n in names):
        raise ValueError(f"{archive.name}: missing enclosure/ files")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def render_release_notes(assets: list[ReleaseAsset]) -> str:
    lines = [
        "| File | Use |",
        "| --- | --- |",
    ]
    for asset in assets:
        lines.append(f"| `{asset.name}` | {asset.use} |")
    lines.append(f"| `{CHECKSUMS_NAME}` | SHA-256 checksums for all release files above |")
    return "\n".join(lines) + "\n"


def validate_top_level_assets(output_dir: Path, expected_names: list[str]) -> None:
    actual = sorted(p.name for p in output_dir.iterdir() if p.is_file())
    expected = sorted([*expected_names, CHECKSUMS_NAME])
    if actual != expected:
        raise ValueError(f"Output directory contents mismatch: got {actual}, expected {expected}")

    for name in expected_names:
        path = output_dir / name
        if name in EXCLUDED_NAMES or path.suffix.lower() in FORBIDDEN_TOP_LEVEL_SUFFIXES:
            raise ValueError(f"Forbidden top-level release asset: {name}")
        if path.stat().st_size == 0:
            raise ValueError(f"Empty release asset: {name}")


def prepare_output_dir(output_dir: Path, *, clean: bool) -> None:
    resolved = output_dir.resolve()
    if resolved == REPO.resolve():
        raise ValueError("Refusing to use repository root as --output-dir")
    if clean and output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)


def package_release(
    *,
    output_dir: Path,
    hardware_dir: Path,
    variants: tuple[str, ...],
    firmware_dir: Path | None,
    knxprod_path: Path | None,
    notes_out: Path | None = None,
    clean: bool = False,
) -> list[Path]:
    prepare_output_dir(output_dir, clean=clean)

    assets: list[ReleaseAsset] = []

    if firmware_dir is not None:
        for filename, use in FIRMWARE_ASSETS:
            src = firmware_dir / filename
            if not src.is_file():
                raise FileNotFoundError(f"Firmware image not found: {src}")
            shutil.copy2(src, output_dir / filename)
            assets.append(ReleaseAsset(name=filename, use=use))

    if knxprod_path is not None:
        if not knxprod_path.is_file():
            raise FileNotFoundError(f"ETS product database not found: {knxprod_path}")
        shutil.copy2(knxprod_path, output_dir / KNXPROD_NAME)
        assets.append(ReleaseAsset(name=KNXPROD_NAME, use=KNXPROD_USE))

    for variant_name in variants:
        if variant_name not in VARIANTS:
            raise ValueError(f"Unknown hardware variant: {variant_name}")
        spec = VARIANTS[variant_name]
        dest = output_dir / spec.archive_name
        build_hardware_archive(hardware_dir / variant_name, spec, dest)
        assets.append(ReleaseAsset(name=spec.archive_name, use=spec.use_description))

    checksum_lines = [f"{sha256_file(output_dir / a.name)}  {a.name}" for a in assets]
    (output_dir / CHECKSUMS_NAME).write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")

    validate_top_level_assets(output_dir, [a.name for a in assets])

    if notes_out is not None:
        notes_out.parent.mkdir(parents=True, exist_ok=True)
        notes_out.write_text(render_release_notes(assets), encoding="utf-8")

    return [output_dir / a.name for a in assets] + [output_dir / CHECKSUMS_NAME]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--firmware-dir",
        type=Path,
        default=REPO / "gateway/.pio/build/release_c6",
        help="Directory containing firmware-full-usb.bin and firmware.bin",
    )
    parser.add_argument(
        "--knxprod",
        type=Path,
        default=REPO / "knxprod/build/KnxDaliGateway.knxprod",
        help="Path to KnxDaliGateway.knxprod",
    )
    parser.add_argument(
        "--hardware-dir",
        type=Path,
        default=REPO / "hardware/releases",
        help="Directory containing per-variant hardware release folders (v2/, v3/)",
    )
    parser.add_argument(
        "--variants",
        nargs="+",
        choices=tuple(VARIANTS),
        default=list(VARIANTS),
        help="Hardware variants to package (default: v2 v3)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=REPO / "dist/release",
        help="Output directory for staged release assets and SHA256SUMS.txt",
    )
    parser.add_argument(
        "--notes-out",
        type=Path,
        default=None,
        help="Optional path to write the Markdown release file table",
    )
    parser.add_argument(
        "--hardware-only",
        action="store_true",
        help="Package only hardware bundles (skip firmware and .knxprod; for local inspection)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove and recreate --output-dir before packaging",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    files = package_release(
        output_dir=args.output_dir,
        hardware_dir=args.hardware_dir,
        variants=tuple(args.variants),
        firmware_dir=None if args.hardware_only else args.firmware_dir,
        knxprod_path=None if args.hardware_only else args.knxprod,
        notes_out=args.notes_out,
        clean=args.clean,
    )
    for path in files:
        print(f"staged {path.name} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
