"""PlatformIO pre-build script that injects GATEWAY_FW_VERSION (yy.mm.dd.hhmm)
from the FW_VERSION environment variable as a compiler define without modifying
any tracked repository files."""

from __future__ import annotations

import os

Import("env")  # type: ignore[name-defined]

fw_ver = os.environ.get("FW_VERSION", "").strip()
if fw_ver:
    env.Append(CPPDEFINES=[("GATEWAY_FW_VERSION", env.StringifyMacro(fw_ver))])  # type: ignore[name-defined]
