"""Pack gateway/data/{index.html,app.css,app.js} into gateway/build/littlefs.bin and gateway/src/firmware/WebAssets.hpp."""

from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
BUILD_DIR = ROOT / "build"
HEADER_OUT = ROOT / "src" / "firmware" / "WebAssets.hpp"

FILES = [
    ("/index.html", "text/html; charset=utf-8", DATA_DIR / "index.html"),
    ("/app.css", "text/css; charset=utf-8", DATA_DIR / "app.css"),
    ("/app.js", "application/javascript; charset=utf-8", DATA_DIR / "app.js"),
]

MAGIC = b"KDWFS1\x00\x00"


def build_littlefs_bin() -> bytes:
    """Build deterministic binary WebFS/LittleFS partition image:
    8B magic ("KDWFS1\\0\\0") + uint32 count + count * (32B path, 40B mime, uint32 offset, uint32 size) + payloads.
    """
    header_size = 12 + len(FILES) * 80
    entries = bytearray()
    payloads = bytearray()
    cur_offset = header_size

    for route, mime, path in FILES:
        raw = path.read_bytes()
        route_b = route.encode("ascii")[:31].ljust(32, b"\x00")
        mime_b = mime.encode("ascii")[:39].ljust(40, b"\x00")
        entries += route_b + mime_b + struct.pack("<II", cur_offset, len(raw))
        payloads += raw
        cur_offset += len(raw)

    return MAGIC + struct.pack("<I", len(FILES)) + bytes(entries) + bytes(payloads)


def build_cpp_header() -> str:
    lines = [
        "#pragma once",
        "// clang-format off",
        "",
        "#include <cstddef>",
        "",
        "namespace gateway::firmware::web_assets {",
        "",
    ]
    for route, _, path in FILES:
        ident = route.strip("/").replace(".", "_")
        content = path.read_text(encoding="utf-8")
        lines.append(f'inline constexpr const char {ident}_data[] = R"WEBUI({content})WEBUI";')
        lines.append(f"inline constexpr std::size_t {ident}_size = sizeof({ident}_data) - 1;")
        lines.append("")
    lines.append("}")
    lines.append("// clang-format on")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    bin_path = BUILD_DIR / "littlefs.bin"
    img = build_littlefs_bin()
    bin_path.write_bytes(img)
    HEADER_OUT.write_text(build_cpp_header(), encoding="utf-8")
    print(f"Wrote {bin_path} ({len(img)} bytes) and {HEADER_OUT}")


if __name__ == "__main__":
    main()
