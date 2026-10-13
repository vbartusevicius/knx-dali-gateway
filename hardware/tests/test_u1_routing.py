"""NCN5130 (U1) layout rules on the stored main board.

- VBUS1 -> C1 -> CCP loop under 5 mm (datasheet "Layout Recommendation")
- CAV, CEQ1, CEQ2: two-pin nets, routed copper under 8 mm (the datasheet gives
  no figure for these slow analog nodes; 8 mm keeps them local to U1)
- VDDA (C5), VDDD (C6), VIN (C15) decoupling pads within 3 mm of their pins
- DC1 switch node (VSW1, U1 pin 17 to L1) at most 10 mm of track
- at least 5 thermal vias in the exposed pad
- KNX_P_PROT at least 0.22 mm wide (the power-class neck-down limit) outside U1's fine-pitch area
"""

import math
from pathlib import Path

import pcbnew
import pytest

REPO = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def board():
    return pcbnew.LoadBoard(str(REPO / "hardware/layout/v1/main/main.kicad_pcb"))


def pad(board, ref, number):
    return next(p for p in board.FindFootprintByReference(ref).Pads() if p.GetNumber() == number)


def distance(a, b):
    pa, pb = a.GetPosition(), b.GetPosition()
    return pcbnew.ToMM(math.hypot(pa.x - pb.x, pa.y - pb.y))


def track_length(board, net):
    return sum(pcbnew.ToMM(t.GetLength()) for t in board.GetTracks()
               if t.GetNetname() == net and t.GetClass() != "PCB_VIA")


def test_ccp_loop_under_5mm(board):
    loop = distance(pad(board, "U1", "6"), pad(board, "C1", "1")) + track_length(board, "CCP")
    print(f"\n  VBUS1-C1-CCP loop {loop:.1f} mm")
    assert loop < 5.0


@pytest.mark.parametrize("net", ["CAV", "CEQ1", "CEQ2"])
def test_two_pin_analog_nets_short(board, net):
    assert track_length(board, net) < 8.0


@pytest.mark.parametrize("cap,pin", [("C5", "40"), ("C6", "30"), ("C15", "16")])
def test_decoupling_next_to_pin(board, cap, pin):
    assert distance(pad(board, cap, "1"), pad(board, "U1", pin)) <= 3.0


def test_dc1_switch_node_short(board):
    assert track_length(board, "VSW1") <= 10.0


def test_exposed_pad_thermal_vias(board):
    vias = [p for p in board.FindFootprintByReference("U1").Pads()
            if p.GetNumber() == "41" and p.GetDrillSize().x > 0]
    assert len(vias) >= 5


def test_knx_supply_path_width(board):
    u1 = board.FindFootprintByReference("U1")
    u1.BuildCourtyardCaches()
    area = u1.GetCourtyard(pcbnew.F_CrtYd).BBox()
    thin = [pcbnew.ToMM(t.GetWidth()) for t in board.GetTracks()
            if t.GetNetname() == "KNX_P_PROT" and t.GetClass() != "PCB_VIA"
            and pcbnew.ToMM(t.GetWidth()) < 0.22
            and not (area.Contains(t.GetStart()) or area.Contains(t.GetEnd()))]
    assert not thin
