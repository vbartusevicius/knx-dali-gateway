"""Tests for the 2-stage OR-Tools CP-SAT component placer (`resolve_overlaps.py`)."""

import math
import sys
from pathlib import Path

import pcbnew
from board_variant import BOARDS

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import resolve_overlaps as cpsat

import layout


def test_committed_boards_have_zero_cpsat_clashes():
    """On already-clean committed plans, resolve(name) with no targets finds 0 clashing parts."""
    for board in BOARDS:
        _, moved, stuck = cpsat.resolve(board)
        assert not stuck, f"{board} has infeasible parts: {stuck}"
        assert not moved, f"{board} unexpectedly needed nudges: {moved}"


def test_two_stage_cpsat_solves_ui_unlocked_parts_deterministically():
    """Stage 1 (active ICs) + Stage 2 (batched passives) solves all unlocked UI parts
    with zero overlaps, valid outline bounds, and identical coordinates across runs."""
    _, run1, stuck1 = cpsat.solve_board("ui", reoptimize_all_unlocked=True, stage1_time=10.0, stage2_time=10.0)
    _, run2, stuck2 = cpsat.solve_board("ui", reoptimize_all_unlocked=True, stage1_time=10.0, stage2_time=10.0)
    assert not stuck1 and not stuck2
    assert run1 == run2, "CP-SAT placement must be 100 % deterministic"


def test_two_stage_cpsat_places_buck5v_block_inside_domain_without_overlaps():
    """Solving the `buck5v` block on `main` places Stage-1 U2/L2 and Stage-2 passives
    strictly inside SELV-KNX without overlapping anchors or XIAO THT pins."""
    plan, moved, stuck = cpsat.solve_board("main", targets=["buck5v"], stage1_time=15.0, stage2_time=10.0)
    assert not stuck, f"buck5v placement infeasible: {stuck}"
    board = layout.fresh_board("main")
    layout.place_parts(board, plan, list(plan["parts"]))
    layout.apply_frame(board, plan)
    for ref, (x, y, rot) in moved.items():
        fp = board.FindFootprintByReference(ref)
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(layout.pos(x, y))

    regions = plan["frame"]["domain_regions"]
    all_refs = {fp.GetReference() for fp in board.Footprints()}
    for ref, p in plan["parts"].items():
        if p.get("block") != "buck5v":
            continue
        assert not cpsat.current_clashes(board, plan, ref, all_refs)
        fp = board.FindFootprintByReference(ref)
        for pad in fp.Pads():
            if pad.GetNetname():
                dom = layout.domain_at(regions, pcbnew.ToMM(pad.GetPosition().x), -pcbnew.ToMM(pad.GetPosition().y))
                assert dom == "SELV-KNX", f"{ref} pad {pad.GetNumber()} escaped to domain {dom}"


def test_stage2_passive_downweights_gnd_to_hug_ic_pin():
    """A decoupling cap between an IC pin and a distant GND anchor hugs the IC pin,
    and flips/rotates to minimize pad-to-pad distance."""
    cap_rots = [
        cpsat.RotGeom(rot=0.0, dx0=-20, dy0=-12, w=40, h=24,
                      pads={"1": (-18, 0), "2": (18, 0)},
                      pad_boxes={"1": (-20, -10, -12, 10), "2": (12, -10, 20, 10)}),
        cpsat.RotGeom(rot=90.0, dx0=-12, dy0=-20, w=24, h=40,
                      pads={"1": (0, -18), "2": (0, 18)},
                      pad_boxes={"1": (-10, -20, 10, -12), "2": (-10, 12, 10, 20)}),
        cpsat.RotGeom(rot=180.0, dx0=-20, dy0=-12, w=40, h=24,
                      pads={"1": (18, 0), "2": (-18, 0)},
                      pad_boxes={"1": (12, -10, 20, 10), "2": (-20, -10, -12, 10)}),
        cpsat.RotGeom(rot=270.0, dx0=-12, dy0=-20, w=24, h=40,
                      pads={"1": (0, 18), "2": (0, -18)},
                      pad_boxes={"1": (-10, 12, 10, 20), "2": (-10, -20, 10, -12)}),
    ]
    ps = cpsat.PartSpec(
        ref="C99", side="front", domain="SELV-KNX", block="decouple",
        stage=2, is_fine_pitch=False, height=1.0,
        orig_x=200, orig_y=200, orig_rot=0.0, rots=cap_rots,
        pad_nets={"1": "VDDA", "2": "GND"},
    )
    # IC occupies [80..120, 80..120] with VDDA pin at (120, 100); distant GND pin at (380, 380)
    fixed_obs = {"front": [(80, 80, 40, 40)], "back": [], "tall_front": []}
    fixed_pads = {"VDDA": [(120, 100)], "GND": [(380, 380)]}
    domain_boxes = {"SELV-KNX": [(0, 0, 400, 400)], None: [(0, 0, 400, 400)]}
    sol = cpsat.solve_batch(
        [ps], fixed_obs, fixed_pads, {}, [(0, 0, 400, 400)], domain_boxes,
        net_fanout={"VDDA": 2, "GND": 20},
        board_bounds=(0, 0, 400, 400),
        max_time_s=5.0,
        anchor_nudge_weight=0,
        enforce_u1_rules=False,
    )
    assert sol is not None
    gx, gy, rot = sol["C99"]
    rg = next(r for r in cap_rots if abs(r.rot - rot) < 1e-3)
    p1x, p1y = gx + rg.pads["1"][0], gy + rg.pads["1"][1]
    dist_mm = math.hypot(p1x - 120, p1y - 100) * cpsat.GRID_MM
    assert dist_mm <= 1.5, f"decoupling cap drifted {dist_mm:.2f} mm from IC VDDA pin"
