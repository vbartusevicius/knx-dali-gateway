"""The front legend artwork: every label lies on one flat face, clear of the
openings and the other labels, and the polarity marks sit over the right pins."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import cadquery as cq
import enclosure_features as ef
import front_legend as fl
import legend_color_model as lcm


@pytest.fixture(scope="module")
def labels():
    return fl.labels()


def test_every_label_sits_on_one_face(labels):
    assert not [lb.text for lb in labels if not any(fl.inside(lb.box(), f) for f in fl.faces().values())]


def test_labels_clear_openings_and_each_other(labels):
    boxes = [lb.box() for lb in labels]
    hits = [lb.text for lb in labels for _, *o in fl.openings() if fl.overlaps(lb.box(), o)]
    pairs = [(a.text, b.text) for i, a in enumerate(labels) for b in labels[i + 1:]
             if fl.overlaps(a.box(), b.box(), fl.LABEL_SPACING - 1e-6)]   # the first v2 print read "DADA"
    assert not hits and not pairs, (hits, pairs, len(boxes))


@pytest.mark.skipif(ef.VARIANT in ("v3", "v4"), reason="v3/v4 top shoulder uses a single large DALI title without bracket")
def test_dali_bracket_encloses_both_pairs_without_touching_text(labels):
    _, strokes = fl.dali_terminals()
    outline = fl.union(strokes)
    assert fl.inside(outline, fl.faces()["top"])
    assert not [o for _, *o in fl.openings() if fl.overlaps(outline, o)]
    touching = [lb.text for lb in labels for s in strokes if fl.overlaps(s, lb.box(), fl.STROKE_SPACING)]
    assert not touching, f"bracket strokes touch {touching}"
    pin_texts = [lb for lb in labels if lb.text in ("DA", "+", "\u2212") and fl.face_of(lb.y) == "top"]
    assert all(outline[0] < lb.box()[0] and lb.box()[2] < outline[2] for lb in pin_texts)


def test_legend_recess_bodies_cut_exactly_their_own_volume_from_the_cover():
    """Embossed cover: the legend bodies sit flush with the faces and are
    cut out of the cover completely, no partial or missed recess."""
    import build_enclosure as be
    import legend_color_model as lcm

    legend = lcm.legend_bodies()
    if legend is None:
        assert ef.VARIANT in ("v3", "v4") and not fl.labels() and not fl.marks()
        return
    cover = be.build_cover()
    removed = cover.val().Volume() - cover.cut(legend).val().Volume()
    assert removed == pytest.approx(legend.val().Volume(), rel=1e-4)
    box = legend.val().BoundingBox()
    used = {lcm.FACE_Z[fl.face_of(lb.y)] for lb in fl.labels()} | {lcm.FACE_Z["front"]}
    assert box.zmax == pytest.approx(max(used), abs=1e-3)
    assert box.zmin == pytest.approx(min(used) - lcm.INLAY_DEPTH, abs=1e-3)


def test_overlap_check_catches_a_label_on_an_opening(labels):
    _, *opening = fl.openings()[0]
    cx, cy = (opening[0] + opening[2]) / 2, (opening[1] + opening[3]) / 2
    assert fl.overlaps(fl.Label("X", cx, cy, fl.LABEL_H).box(), opening)


def test_every_marking_is_present(labels):
    texts = [lb.text for lb in labels]
    expected = () if ef.VARIANT in ("v3", "v4") else \
        ("KNX", "DALI", "SCROLL", "RESET")
    for text in expected:
        assert text in texts
    if ef.VARIANT not in ("v3", "v4"):
        assert texts.count("DA") == 0 and texts.count("+") == 1 and texts.count("\u2212") == 1
        assert not signs(labels, "bottom"), "the keyed KNX plug needs no +/-"


def signs(labels, face):
    return {lb.text: lb for lb in labels if lb.text in ("+", "\u2212") and fl.face_of(lb.y) == face}


@pytest.mark.skipif(ef.VARIANT in ("v3", "v4"), reason="v3/v4 puts KNX and AUX on the right side without front +/-")
@pytest.mark.parametrize(("ref", "face"), [("J2", "top")])
def test_plus_is_over_pin_1(labels, ref, face):
    """AUX J2 pin 1 is the fused + input (blocks/dali.py)."""
    pads = {p[2]: ef.main_to_enc(p[0], p[1])[0] for p in ef.components("main")[ref]["pads"]}
    pair = signs(labels, face)
    assert abs(pair["+"].x - pads["1"]) < abs(pair["+"].x - pads["2"])
    assert abs(pair["\u2212"].x - pads["2"]) < abs(pair["\u2212"].x - pads["1"])


@pytest.mark.skipif(ef.VARIANT in ("v3", "v4"), reason="v3/v4 puts KNX and AUX on the right side without front +/-")
@pytest.mark.parametrize("face", ["top"])
def test_signs_side_by_side_share_one_axis(labels, face):
    plus, minus = signs(labels, face).values()
    centre = [(b[1] + b[3]) / 2 for b in (plus.box(), minus.box())]
    assert abs(centre[0] - centre[1]) < 0.05


PIXEL = 0.02


def raster(path):
    """Filled path on a PIXEL grid (even-odd, so letter counters stay open)."""
    ext = path.get_extents()
    xs = np.arange(ext.x0 - 0.5, ext.x1 + 0.5, PIXEL)
    ys = np.arange(ext.y0 - 0.5, ext.y1 + 0.5, PIXEL)
    gx, gy = np.meshgrid(xs, ys)
    points = np.column_stack([gx.ravel(), gy.ravel()])
    inside = np.zeros(len(points), dtype=bool)
    for ring in path.to_polygons(closed_only=True):
        if len(ring) > 3:
            inside ^= fl.MplPath(ring).contains_points(points)
    return inside.reshape(gx.shape)


def opened(mask, radius):
    """Morphological opening with a disc: what a tool of that radius can fill."""
    r = round(radius / PIXEL)
    offsets = [(dy, dx) for dy in range(-r, r + 1) for dx in range(-r, r + 1) if dx * dx + dy * dy <= r * r]

    def shifted(m, dy, dx):
        out = np.zeros_like(m)
        out[max(dy, 0):m.shape[0] + min(dy, 0), max(dx, 0):m.shape[1] + min(dx, 0)] = \
            m[max(-dy, 0):m.shape[0] + min(-dy, 0), max(-dx, 0):m.shape[1] + min(-dx, 0)]
        return out

    eroded = np.ones_like(mask)
    for dy, dx in offsets:
        eroded &= shifted(mask, dy, dx)
    dilated = np.zeros_like(mask)
    for dy, dx in offsets:
        dilated |= shifted(eroded, dy, dx)
    return dilated & mask


def thin_fraction(path, stroke=fl.MIN_STROKE):
    """Share of the ink in strokes narrower than `stroke`, which a powder
    print (MJF / SLS) fills in or smears."""
    mask = raster(path)
    return 1 - opened(mask, stroke / 2).sum() / mask.sum()


def test_legend_text_is_big_enough_for_a_powder_print(labels):
    """The first MJF print (2026-11-09) had 0.85-2.6 mm letters 0.3 mm deep: unreadable."""
    assert lcm.INLAY_DEPTH >= 0.6
    # +/- are drawn as SIGN_BAR bars, so their legibility is the stroke test's job
    assert all(lb.height >= fl.MIN_TEXT_H for lb in labels if lb.text not in fl.SIGNS), \
        [(lb.text, lb.height) for lb in labels]
    assert fl.SIGN_BAR >= 0.6


def test_legend_strokes_are_wide_enough_for_a_powder_print(labels):
    thin = {lb.text: round(thin_fraction(lb.path()), 3) for lb in labels}
    assert all(v <= 0.2 for v in thin.values()), thin


def test_stroke_check_catches_the_old_small_text():
    assert thin_fraction(fl.Label("STATUS", 0, 0, 0.85).path()) > 0.5


def test_brackets_print():
    assert fl.BRACKET_LINE >= fl.MIN_STROKE


def test_no_rings_around_the_leds():
    """User decision 2026-11-09: the LED windows get no debossed rings."""
    import cadquery as cq
    legend = lcm.legend_bodies()
    if legend is None:
        return
    legend = legend.val()
    for w in ef.led_windows():
        r = be.LED_WINDOW / 2 + 0.3
        for dx, dy in ((r, 0), (-r, 0), (0, r), (0, -r)):
            assert not legend.isInside(cq.Vector(w["x"] + dx, w["y"] + dy, lcm.FACE_Z["front"] - lcm.INLAY_DEPTH / 2))


V3 = ef.VARIANT in ("v3", "v4")


@pytest.fixture(scope="module")
def side():
    return fl.side_labels()


@pytest.fixture(scope="module")
def base():
    return be.build_base().val()


def ink_points(label, n=12):
    mask = raster(label.path())
    ext = label.path().get_extents()
    ys, xs = np.nonzero(opened(mask, fl.MIN_STROKE / 2))
    pick = np.linspace(0, len(xs) - 1, n).astype(int)
    return [(ext.x0 - 0.5 + xs[i] * PIXEL, ext.y0 - 0.5 + ys[i] * PIXEL) for i in pick]


@pytest.mark.skipif(not V3, reason="v3/v4 name the connectors on the right side")
def test_connector_names_sit_on_the_right_side_next_to_their_connectors(side):
    """User request 2026-11-09: KNX, AUX and DALI beside their connectors, not on the front."""
    by_text = {lb.text: lb for lb in side}
    assert set(by_text) == {"KNX", "AUX", "DALI"}
    for text, pocket in (("KNX", be.plug_pocket("J1")), ("AUX", be.plug_pocket("J2"))):
        z, _ = fl.side_to_enc(by_text[text].x, by_text[text].y)
        assert pocket["z0"] < z < pocket["z1"], text
        assert by_text[text].box()[1] > pocket["y1"], f"{text} above its pocket"
    j3 = next(b for b in ef.part_boxes("body") if (b["board"], b["ref"]) == ("main", "J3"))
    z, y = fl.side_to_enc(by_text["DALI"].x, by_text["DALI"].y)
    assert j3["z0"] < z < j3["z1"] and j3["y0"] < y < j3["y1"]
    assert not {"KNX", "AUX", "DALI"} & {lb.text for lb in fl.labels()}, "not repeated on the front"


@pytest.mark.skipif(not V3, reason="v3/v4 name the connectors on the right side")
def test_connector_names_are_engraved_into_solid_wall(side, base):
    depth = lcm.INLAY_DEPTH
    for label in side:
        assert label.height >= fl.MIN_TEXT_H and thin_fraction(label.path()) <= 0.2, label.text
        u0, v0, u1, v1 = label.box()
        z_lo, z_hi = sorted((fl.side_to_enc(u0, v0)[0], fl.side_to_enc(u1, v1)[0]))
        assert 0 < z_lo and z_hi < ef.BASE_TOP_Z and 0 < v0 and v1 < ef.HIDDEN_H, label.text
        for u, v in ink_points(label):
            z, y = fl.side_to_enc(u, v)
            assert not base.isInside(cq.Vector(ef.WIDTH / 2 - depth / 2, y, z)), f"{label.text} not engraved"
        for i in range(5):
            for j in range(3):
                u, v = u0 + (u1 - u0) * i / 4, v0 + (v1 - v0) * j / 2
                z, y = fl.side_to_enc(u, v)
                assert base.isInside(cq.Vector(ef.WIDTH / 2 - depth - 0.3, y, z)), f"no wall behind {label.text}"
