"""Pack gateway/data/{index.html,app.css,app.js} into gateway/src/firmware/WebAssets.hpp
(runs both standalone and as a PlatformIO pre-script)."""

from __future__ import annotations

import inspect
from pathlib import Path

_SCRIPT_FILE = Path(inspect.getfile(lambda: None)).resolve()
ROOT = _SCRIPT_FILE.parents[1]
DATA_DIR = ROOT / "data"
HEADER_OUT = ROOT / "src" / "firmware" / "WebAssets.hpp"

FILES = [
    ("/index.html", "text/html; charset=utf-8", DATA_DIR / "index.html"),
    ("/app.css", "text/css; charset=utf-8", DATA_DIR / "app.css"),
    ("/app.js", "application/javascript; charset=utf-8", DATA_DIR / "app.js"),
]


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
    hdr = build_cpp_header()
    if not HEADER_OUT.exists() or HEADER_OUT.read_text(encoding="utf-8") != hdr:
        HEADER_OUT.write_text(hdr, encoding="utf-8")
    if __name__ == "__main__":
        print(f"Wrote {HEADER_OUT}")


main()
