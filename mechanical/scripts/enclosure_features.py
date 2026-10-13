"""Enclosure feature positions taken from the real board layouts.

build_enclosure.py (CadQuery model) and the fit tests both use this module,
so every hole follows the parts on the stored boards. Board data comes from
hardware/build/<variant>/<board>/components_<board>.json, which
hardware/scripts/layout.py writes on every save (run `layout.py <board> sync`
first on a fresh checkout).

Coordinates: enclosure x centred at 0, y up from the bottom of the hidden
part, z from the rail. Boards face the cover with their own x right / y up,
so a board point maps to the enclosure by a translation only.
"""

import json
import sys
from functools import partial
from pathlib import Path

import yaml
from ui_stackup import OLED_HEADER_SPACER, OLED_MODULE_BOARD, OLED_PANEL

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]

DIN = SPEC["din_envelope"]
WALL = SPEC["enclosure"]["wall"]

WIDTH = DIN["modules"] * DIN["module_width"]
HIDDEN_H = DIN["hidden_part"]["height"]
FRONT_H = DIN["front_protrusion"]["height"]
FRONT_D = DIN["front_protrusion"]["depth"]
SHOULDER_H = DIN["shoulder_height"]
TOTAL_D = DIN["hidden_part"]["depth"] + FRONT_D

MAIN_W = SPEC["boards"]["main"]["max_outline"]["width"]
MAIN_H = SPEC["boards"]["main"]["max_outline"]["height"]
UI_W = SPEC["boards"]["ui"]["max_outline"]["width"]
UI_H = SPEC["boards"]["ui"]["max_outline"]["height"]

FRONT_Y_START = (HIDDEN_H - FRONT_H) / 2
FRONT_Y_END = (HIDDEN_H + FRONT_H) / 2


def components(board):
    path = REPO / "hardware/build" / VARIANT / board / f"components_{board}.json"
    return {c["ref"]: c for c in json.loads(path.read_text())}


def main_to_enc(x, y):
    return x - MAIN_W / 2, y + (HIDDEN_H - MAIN_H) / 2


def ui_to_enc(x, y):
    return x - UI_W / 2, y + FRONT_Y_START + (FRONT_H - UI_H) / 2


def board_to_enc(board, x, y):
    if board == "main":
        return main_to_enc(x, y)
    if board == "ui":
        return ui_to_enc(x, y)
    config = SPEC["boards"][board]
    return x - config["max_outline"]["width"] / 2, y + config["y_offset"]


def board_slab(board):
    config = SPEC["boards"][board]
    top = SPEC["front_ui"]["board_top_z"] if board == "ui" else config["z"]
    (x0, y0) = board_to_enc(board, 0, 0)
    (x1, y1) = board_to_enc(board, config["max_outline"]["width"], config["max_outline"]["height"])
    return {"board": board, "x0": x0, "x1": x1, "y0": y0, "y1": y1, "z0": top - config["thickness"], "z1": top}


def interconnect_corridor(link):
    """The space a board-to-board cable sweeps: the footprint of both
    connectors, between their mating faces."""
    ends = [next(p for p in part_boxes() if (p["board"], p["ref"]) == item) for item in link["refs"].items()]
    faces = [b["z1"] if b["z0"] >= board_slab(b["board"])["z1"] - 0.01 else b["z0"] for b in ends]
    return {"x0": min(b["x0"] for b in ends), "x1": max(b["x1"] for b in ends),
            "y0": min(b["y0"] for b in ends), "y1": max(b["y1"] for b in ends),
            "z0": min(faces), "z1": max(faces)}


def horizontal_capacitor():
    config = SPEC["boards"]["main"].get("horizontal_capacitor")
    if not config:
        return None
    component = components("main")[config["ref"]]
    cx = sum(x for x, _, _ in component["pads"]) / len(component["pads"])
    cx, end_y = main_to_enc(cx, component["y"] - config["bend_clearance"])
    radius = config["diameter"] / 2
    centre_z = SPEC["boards"]["main"]["z"] - SPEC["boards"]["main"]["thickness"] - config["lead_drop"]
    return {"board": "main", "ref": config["ref"], "shape": "horizontal_capacitor", "through_front": False,
            "x0": cx - radius, "x1": cx + radius, "y0": end_y - config["length"], "y1": end_y,
            "z0": centre_z - radius, "z1": centre_z + radius}


def pad_points(comp):
    return [(p[0], p[1]) for p in comp["pads"]]


def bbox_centre(comp):
    b = comp["bbox"]
    return (b["x0"] + b["x1"]) / 2, (b["y0"] + b["y1"]) / 2


def terminal_holes():
    """KNX: one opening centred on the mated plug body. DALI and AUX: one
    screwdriver hole per screw; MX126 screws sit straight above their pins."""
    main = components("main")
    holes = {"KNX": [dict(zip(("x", "y"), main_to_enc(*bbox_centre(main["J1"]))))]}
    for name, ref in (("AUX", "J2"), ("DALI", "J3")):
        holes[name] = [dict(zip(("x", "y"), main_to_enc(px, py)))
                       for px, py in sorted(pad_points(main[ref]))]
    return holes


def wire_entries():
    """Openings in the top wall that the terminals' wire-entry faces sit in,
    flush with the outside."""
    main = components("main")
    z0 = SPEC["boards"]["main"]["z"]
    slots = []
    for ref in ("J2", "J3"):
        b = main[ref]["bbox"]
        x0, _ = main_to_enc(b["x0"], 0)
        x1, _ = main_to_enc(b["x1"], 0)
        slots.append({"ref": ref, "x0": x0, "x1": x1, "z0": z0, "z1": z0 + main[ref]["height"]})
    return slots


def layout_plan(board):
    return yaml.safe_load((REPO / "hardware/circuits/variants" / VARIANT / f"layout_{board}.yaml").read_text())


PLUG_HEIGHT = 11.5
BODY_MARGIN = 0.5
_MAIN_HEIGHTS = layout_plan("main")["heights"]
_MAIN_Z = SPEC["boards"]["main"]["z"]
_TALLEST_CONNECTOR = max(PLUG_HEIGHT, _MAIN_HEIGHTS["J2"], _MAIN_HEIGHTS["J3"])
SHOULDER_Z = dict.fromkeys(("bottom", "top"), round(_MAIN_Z + _TALLEST_CONNECTOR + 1.3, 2))
HIDDEN_D = max(SHOULDER_Z.values())
BASE_TOP_Z = min(SHOULDER_Z.values()) - WALL


def shoulder_of(y):
    return "bottom" if y < HIDDEN_H / 2 else "top"


TERMINAL_PIN_TO_WIRE_FACE = 3.8


def connector_faces():
    """Outer faces of the connector bodies at the case's top and bottom faces:
    the terminals' wire-entry faces (MaiXu MX126 drawing: 3.8 mm from the pin
    row) and the KNX plug's underside (courtyard edge minus its margin)."""
    main = components("main")
    faces = {ref: main_to_enc(0, max(y for _, y, _ in main[ref]["pads"]) + TERMINAL_PIN_TO_WIRE_FACE)[1]
             for ref in ("J2", "J3")}
    faces["J1"] = main_to_enc(0, main["J1"]["bbox"]["y0"] + BODY_MARGIN)[1]
    return faces


def case_points(board, kind):
    convert = main_to_enc if board == "main" else ui_to_enc
    return [dict(zip(("x", "y"), convert(p["x"], p["y"]))) for p in layout_plan(board)["case"][kind]]


def legs():
    """Legs from the case floor to the back of the main board."""
    return case_points("main", "legs")


def pegs():
    """Pegs from the cover's shoulder plates onto the front of the main board."""
    return case_points("main", "pegs")


def standoffs():
    """Standoffs from the cover's front wall onto the front of the UI board."""
    return case_points("ui", "standoffs")


def button_holes():
    ui = components("ui")
    return {name: dict(zip(("x", "y"), ui_to_enc(ui[ref]["x"], ui[ref]["y"])))
            for name, ref in (("PROG", "SW1"), ("RESET", "SW2"))}


def led_windows():
    ui = components("ui")
    holes = []
    for ref in ("D1", "D2", "D3"):
        pads = pad_points(ui[ref])
        cx = sum(x for x, _ in pads) / len(pads)
        cy = sum(y for _, y in pads) / len(pads)
        holes.append(dict(zip(("x", "y"), ui_to_enc(cx, cy))))
    return holes


def _oled(key):
    r = layout_plan("ui")["oled_module"][key]
    cx, cy = ui_to_enc(r["x"] + r["w"] / 2, r["y"] + r["h"] / 2)
    return {"cx": cx, "cy": cy, "w": r["w"], "h": r["h"]}


WINDOW_CLEARANCE = 0.2


def oled_window():
    """Window around the OLED glass, which sits flush in the front wall."""
    glass = _oled("glass")
    return dict(glass, w=glass["w"] + 2 * WINDOW_CLEARANCE, h=glass["h"] + 2 * WINDOW_CLEARANCE)


def oled_glass():
    return _oled("glass")


def oled_module():
    return _oled("outline")


SCREW_TAB_X = 4.25


def screw_tabs():
    """Cover tabs behind the top and bottom walls; the two case screws enter
    from the wire sides (top and bottom faces) along y."""
    return SPEC["enclosure"].get("screw_tabs", [{"x": SCREW_TAB_X, "side": "top"},
                                               {"x": SCREW_TAB_X, "side": "bottom"}])


THROUGH_FRONT = {"SW1", "SW2"}


def part_boxes():
    """Every part as a box in enclosure coordinates: courtyard outline times
    its height, on the side it is mounted; J1 as the mated KNX plug."""
    stack_top = {"main": SPEC["boards"]["main"]["z"], "ui": SPEC["front_ui"]["board_top_z"]}
    boxes = []
    horizontal = horizontal_capacitor()
    boards = SPEC["variants"][VARIANT]["boards"]
    for board in boards:
        convert = partial(board_to_enc, board)
        top = stack_top.get(board, SPEC["boards"][board]["z"])
        bottom = top - SPEC["boards"][board]["thickness"]
        for ref, c in components(board).items():
            if horizontal and board == "main" and ref == horizontal["ref"]:
                boxes.append(horizontal)
                continue
            (x0, y0), (x1, y1) = convert(c["bbox"]["x0"], c["bbox"]["y0"]), convert(c["bbox"]["x1"], c["bbox"]["y1"])
            h = PLUG_HEIGHT if (board, ref) == ("main", "J1") else c["height"]
            z0, z1 = (bottom - h, bottom) if c["side"] == "back" else (top, top + h)
            boxes.append({"board": board, "ref": ref, "x0": x0, "x1": x1, "y0": y0, "y1": y1, "z0": z0, "z1": z1,
                          "through_front": board == "ui" and ref in THROUGH_FRONT})
    top = stack_top["ui"]
    module_z = top + OLED_HEADER_SPACER
    for ref, key, z0, z1, through in (("OLED board", "outline", module_z, module_z + OLED_MODULE_BOARD, False),
                                      ("OLED glass", "glass", module_z + OLED_MODULE_BOARD,
                                       module_z + OLED_MODULE_BOARD + OLED_PANEL, True)):
        r = _oled(key)
        boxes.append({"board": "ui", "ref": ref, "x0": r["cx"] - r["w"] / 2, "x1": r["cx"] + r["w"] / 2,
                      "y0": r["cy"] - r["h"] / 2, "y1": r["cy"] + r["h"] / 2, "z0": z0, "z1": z1,
                      "through_front": through})
    return boxes
