"""The front legend artwork: every label lies on one flat face, clear of the
openings and the other labels, and the polarity marks sit over the right pins."""

import sys
from itertools import pairwise
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import enclosure_features as ef
import front_legend as fl


@pytest.fixture(scope="module")
def labels():
    return fl.labels()


def test_every_label_sits_on_one_face(labels):
    assert not [lb.text for lb in labels if not any(fl.inside(lb.box(), f) for f in fl.faces().values())]


def test_labels_clear_openings_and_each_other(labels):
    boxes = [lb.box() for lb in labels]
    hits = [lb.text for lb in labels for _, *o in fl.openings() if fl.overlaps(lb.box(), o)]
    pairs = [(a.text, b.text) for i, a in enumerate(labels) for b in labels[i + 1:] if fl.overlaps(a.box(), b.box())]
    assert not hits and not pairs, (hits, pairs, len(boxes))


def test_overlap_check_catches_a_label_on_an_opening(labels):
    _, *opening = fl.openings()[0]
    cx, cy = (opening[0] + opening[2]) / 2, (opening[1] + opening[3]) / 2
    assert fl.overlaps(fl.Label("X", cx, cy, fl.LABEL_H).box(), opening)


def test_every_marking_is_present(labels):
    texts = [lb.text for lb in labels]
    for expected in ("KNX", "+", "\u2212", "DALI", "PRG/RST", "FUNC", "STATUS"):
        assert expected in texts
    assert texts.count("DA") == 2


def signs(labels, face):
    return {lb.text: lb for lb in labels if lb.text in ("+", "\u2212") and fl.face_of(lb.y) == face}


@pytest.mark.parametrize("ref,face", [("J1", "bottom"), ("J2", "top")])
def test_plus_is_over_pin_1(labels, ref, face):
    """KNX J1 pin 1 is KNX+ and AUX J2 pin 1 the fused + input (blocks/knx.py, blocks/dali.py)."""
    pads = {p[2]: ef.main_to_enc(p[0], p[1])[0] for p in ef.components("main")[ref]["pads"]}
    pair = signs(labels, face)
    assert abs(pair["+"].x - pads["1"]) < abs(pair["+"].x - pads["2"])
    assert abs(pair["\u2212"].x - pads["2"]) < abs(pair["\u2212"].x - pads["1"])


@pytest.mark.parametrize("face", ["bottom", "top"])
def test_signs_side_by_side_share_one_axis(labels, face):
    plus, minus = signs(labels, face).values()
    centre = [(b[1] + b[3]) / 2 for b in (plus.box(), minus.box())]
    assert abs(centre[0] - centre[1]) < 0.05


def test_led_labels_keep_their_distance(labels):
    names = {led["id"].upper() for led in ef.SPEC["front_ui"]["leds"]}
    row = sorted((lb for lb in labels if lb.text in names and fl.face_of(lb.y) == "front"), key=lambda lb: lb.x)
    assert len(row) == 3
    assert all(b.box()[0] - a.box()[2] >= fl.LED_LABEL_SPACING - 1e-6 for a, b in pairwise(row))
