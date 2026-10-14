"""Unified build script run inside the `eda` or `cad` container (no Docker calls).

Inside the `eda` container (`docker compose --profile hardware run --rm eda ...`):
    python3 hardware/scripts/build.py [board...] [--fresh] [--release] [--test]

Inside the `cad` container (`docker compose --profile hardware run --rm cad ...`):
    python3 hardware/scripts/build.py [--test]

Automatically detects whether it is running in `eda` (pcbnew available) or `cad`
(cadquery available) and executes all steps in a single Python process.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def has_module(name):
    try:
        __import__(name)
        return True
    except ImportError:
        return False


def run_eda(boards, *, fresh=False, do_release=False, do_test=False):
    sys.path.insert(0, str(REPO / "hardware/scripts"))
    import gen_circuits
    import layout
    import pcbnew
    import release
    import render_board

    gen_circuits.main()
    os.chdir(REPO)
    variant = layout.VARIANT
    target_boards = boards or layout.SPEC["variants"][variant]["boards"]
    for b in target_boards:
        pcb_path = layout.stored_board_path(b)
        if fresh and pcb_path.exists():
            pcb_path.unlink()
        layout.sync(b)
        board = pcbnew.LoadBoard(str(pcb_path))
        if len(list(board.GetTracks())) == 0 or fresh:
            layout.route(b)
        layout.tidy_silk(b)
        render_board.render(b, "routed", placement=False)
    if do_release:
        sys.argv = ["release.py", variant, *target_boards]
        release.main()
    if do_test:
        subprocess.run([
            sys.executable, "-m", "pytest",
            "hardware/tests", "mechanical/tests/test_fit.py",
            "mechanical/tests/test_ui_stackup.py", "mechanical/tests/test_board_stack.py",
            "--ignore=hardware/tests/test_step_geometry.py", "-q", "-p", "no:cacheprovider",
        ], cwd=REPO, check=True)


def run_cad(*, do_test=False):
    sys.path.insert(0, str(REPO / "mechanical/scripts"))
    import build_enclosure
    import fit_drawing
    import front_legend
    import legend_color_model
    import render_assembly3d

    build_enclosure.export_parts()
    fit_drawing.main()
    front_legend.main()
    legend_color_model.main()
    render_assembly3d.main()
    if do_test:
        subprocess.run([
            sys.executable, "-m", "pytest",
            "mechanical/tests/test_enclosure_probe.py",
            "mechanical/tests/test_front_legend.py",
            "hardware/tests/test_step_geometry.py", "-q", "-p", "no:cacheprovider",
        ], cwd=REPO, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("boards", nargs="*", help="subset of boards in eda mode (default: all boards of HARDWARE_VARIANT)")
    parser.add_argument("--fresh", action="store_true", help="re-place and re-route boards from scratch")
    parser.add_argument("--release", action="store_true", help="also package hardware/releases/<variant>/")
    parser.add_argument("--test", action="store_true", help="run the container's pytest suite at the end")
    args = parser.parse_args()

    if has_module("pcbnew"):
        run_eda(args.boards, fresh=args.fresh, do_release=args.release, do_test=args.test)
    elif has_module("cadquery"):
        run_cad(do_test=args.test)
    else:
        sys.exit("Run hardware/scripts/build.py inside the `eda` or `cad` Docker container.")


if __name__ == "__main__":
    main()
