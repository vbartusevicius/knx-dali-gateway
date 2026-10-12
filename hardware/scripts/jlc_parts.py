"""Check every JLC-assembled part against what JLCPCB will actually place.

CI reads the versioned supplier reference in hardware/product/jlc_reference.json,
without contacting JLCPCB or EasyEDA. Release checks use live=True to fetch fresh
part records, footprints and stock (raw responses cached in hardware/build/jlc/).
Refresh the reference with `python3 hardware/scripts/jlc_parts.py snapshot`;
`--cached` captures existing downloads without networking. Then, per footprint:

  - the manufacturer part matches the circuit's MPN, and resistance,
    capacitance and voltage rating match the circuit's value
  - JLCPCB's footprint, turned in quarter turns, puts every one of its pad
    centres inside one of our pads carrying the same pin (by number, or by
    hardware/product/jlc_pad_maps.yaml where the numbering differs) and
    covers every pin of ours: this proves the package and the pin mapping,
    and yields the placement rotation and centre JLCPCB needs
  - in live mode only, current stock covers the order quantity

Placement model (JLCPCB, as documented by the KiKit and KiBot maintainers):
rotation counter-clockwise as seen from the top; a bottom part is JLCPCB's
footprint mirrored about its own x axis, then rotated and placed.
"""

import json
import math
import re
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CACHE = REPO / "hardware/build/jlc"
EASYEDA_UNIT_MM = 0.254
ORDER_BOARDS = 5
JLC_URL = "https://cart.jlcpcb.com/shoppingCart/smtGood/getComponentDetail?componentCode={}"
EASYEDA_URL = "https://easyeda.com/api/products/{}/components?version=6.5.40"
HEADERS = {"User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/130.0 Safari/537.36",
           "Accept": "application/json"}


def fetch(url, path, attempts=6, *, refresh=False):
    """Cached GET; both services answer 403 when asked too fast, so wait and retry."""
    if refresh or not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        for attempt in range(attempts):
            try:
                with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=30) as response:
                    path.write_bytes(response.read())
                break
            except urllib.error.HTTPError as error:
                if error.code != 403 or attempt == attempts - 1:
                    raise
                time.sleep(5 * (attempt + 1))
        time.sleep(1)
    return json.loads(path.read_text())


def jlc_record(lcsc, *, refresh=False):
    return fetch(JLC_URL.format(lcsc), CACHE / f"{lcsc}.jlc.json", refresh=refresh)["data"]


def easyeda_pads(lcsc, *, refresh=False):
    """(number, x, y) in mm relative to the footprint origin, y up."""
    result = fetch(EASYEDA_URL.format(lcsc), CACHE / f"{lcsc}.easyeda.json", refresh=refresh)["result"]
    data = result["packageDetail"]["dataStr"]
    ox, oy = float(data["head"]["x"]), float(data["head"]["y"])
    pads = []
    for shape in data["shape"]:
        if shape.startswith("PAD~"):
            f = shape.split("~")
            if f[8]:
                pads.append((f[8], (float(f[2]) - ox) * EASYEDA_UNIT_MM, -(float(f[3]) - oy) * EASYEDA_UNIT_MM))
    return result["packageDetail"]["title"], pads


def rotate(x, y, degrees):
    a = math.radians(degrees)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


@dataclass
class Placement:
    rotation: int
    x: float
    y: float


@dataclass(frozen=True)
class Pad:
    """One of our pads: number and copper bounding box in board mm, y up."""
    number: str
    x0: float
    y0: float
    x1: float
    y1: float

    def contains(self, x, y):
        return self.x0 <= x <= self.x1 and self.y0 <= y <= self.y1

    @property
    def centre(self):
        return (self.x0 + self.x1) / 2, (self.y0 + self.y1) / 2


def fit(ours, theirs, bottom, pad_map=None):
    """Quarter-turn rotation and centre at which every pad of JLCPCB's
    footprint lands inside one of our pads it may connect to, and every pin
    of ours gets a pad; None when no rotation does. `pad_map` maps JLCPCB pad
    numbers to the numbers of ours they may land on (default: the same)."""
    pad_map = pad_map or {}
    allowed = {n: set(pad_map.get(n, [n])) for n, _, _ in theirs}
    numbers = {p.number for p in ours}
    theirs = [(n, x, -y if bottom else y) for n, x, y in theirs if allowed[n] & numbers]
    if not theirs:
        return None
    reference = [p for p in ours if any(p.number in allowed[n] for n, _, _ in theirs)]
    ox = sum(p.centre[0] for p in reference) / len(reference)
    oy = sum(p.centre[1] for p in reference) / len(reference)
    landing = []
    for rotation in (0, 90, 180, 270):
        placed = [(n, *rotate(x, y, rotation)) for n, x, y in theirs]
        cx = ox - sum(x for _, x, _ in placed) / len(placed)
        cy = oy - sum(y for _, _, y in placed) / len(placed)
        hit = set()
        lands = True
        for n, x, y in placed:
            pads = [p for p in ours if p.number in allowed[n] and p.contains(x + cx, y + cy)]
            lands &= bool(pads)
            hit.update(p.number for p in pads)
        if lands and hit == numbers:
            landing.append(Placement(rotation, cx, cy))
    return landing[0] if landing else None


SI = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "M": 1e6, "": 1.0}


def si(text):
    """'4k7', '100n', '330R', '10kΩ', '100nF', '2.2uF' -> float."""
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([pnuµmkMR]?)(\d*)\s*[ΩFH]?", text.strip())
    if not m:
        return None
    whole, prefix, fraction = m.groups()
    prefix = "" if prefix == "R" else prefix
    return float(f"{whole}.{fraction}" if fraction else whole) * SI[prefix]


def attribute(record, name):
    return next((a["attribute_value_name"] for a in record.get("attributes") or []
                 if a["attribute_name_en"] == name), None)


def normalised(mpn):
    return re.sub(r"[^A-Z0-9]", "", (mpn or "").upper())


def value_problems(part, record):
    problems = []
    ours, theirs = normalised(part["mpn"]), normalised(record["componentModelEn"])
    if ours and not (ours.startswith(theirs) or theirs.startswith(ours)):
        problems.append(f"manufacturer part {record['componentModelEn']!r}, circuit wants {part['mpn']!r}")
    words = part["value"].split()
    kind = part["footprint"].split(":")[1][:2]
    if kind in ("R_", "C_") and words:
        name = "Resistance" if kind == "R_" else "Capacitance"
        wanted, actual = si(words[0]), si(attribute(record, name) or "")
        if wanted is None or actual is None or not math.isclose(wanted, actual, rel_tol=1e-6):
            problems.append(f"{name} {attribute(record, name)!r}, circuit wants {words[0]!r}")
    rated = next((w for w in words[1:] if w.endswith("V")), None)
    if rated:
        actual = attribute(record, "Voltage Rating") or attribute(record, "Voltage Rated")
        if actual is None or si(actual.rstrip("V")) < si(rated.rstrip("V")):
            problems.append(f"voltage rating {actual!r}, circuit wants at least {rated}")
    return problems


@dataclass
class Check:
    ref: str
    lcsc: str
    footprint: str
    jlc_footprint: str = ""
    placement: Placement | None = None
    stock: int | None = None
    library: str = ""
    problems: list = field(default_factory=list)


PAD_MAPS = REPO / "hardware/product/jlc_pad_maps.yaml"
REFERENCE = REPO / "hardware/product/jlc_reference.json"


def load_reference():
    return json.loads(REFERENCE.read_text())["parts"]


def supplier_data(lcsc, *, refresh=False):
    record = jlc_record(lcsc, refresh=refresh)
    footprint, pads = easyeda_pads(lcsc, refresh=refresh)
    return {"record": record, "footprint": footprint, "pads": pads}


def snapshot(*, cached=False):
    import yaml

    variant = yaml.safe_load((REPO / "hardware/product/spec.yaml").read_text())["active_variant"]
    sources = [REPO / "hardware/build" / variant / board / "parts.json" for board in ("main", "ui")]
    lcscs = sorted({p["lcsc"] for path in sources for p in json.loads(path.read_text()) if p["assembly"] == "jlc"})
    parts, notices = {}, set()
    for lcsc in lcscs:
        if cached:
            for suffix in ("jlc", "easyeda"):
                path = CACHE / f"{lcsc}.{suffix}.json"
                if not path.exists():
                    raise FileNotFoundError(f"Missing cached supplier data: {path}")
        data = supplier_data(lcsc, refresh=not cached)
        record = data["record"]
        data["record"] = {key: record[key] for key in ("componentModelEn", "componentBrandEn",
                                                       "componentSpecificationEn", "dataManualUrl",
                                                       "componentLibraryType")}
        data["record"]["attributes"] = [a for a in record.get("attributes") or []
                                         if a["attribute_name_en"] in ("Resistance", "Capacitance",
                                                                       "Voltage Rating", "Voltage Rated")]
        parts[lcsc] = data
        raw = json.loads((CACHE / f"{lcsc}.easyeda.json").read_text())
        notice = raw["result"]["packageDetail"]["dataStr"]["head"].get("license")
        if notice:
            notices.add(notice)
    document = {"source": "嘉立创EDA/EasyEDA官方库 (JLCEDA/EasyEDA Official Library) and JLCPCB catalogue",
                "source_links": ["https://lceda.cn/", "https://easyeda.com", JLC_URL, EASYEDA_URL],
                "source_notices": sorted(notices), "parts": parts}
    REFERENCE.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n")
    print(f"Wrote {len(parts)} supplier references to {REFERENCE.relative_to(REPO)} (no stock data)")


def check_board(pcb, parts, *, live=False):
    """One Check per JLC-assembled footprint of a pcbnew board."""
    import pcbnew
    import yaml

    pad_maps = yaml.safe_load(PAD_MAPS.read_text())
    supplier = {} if live else load_reference()
    by_ref = {p["ref"]: p for p in parts}
    quantity = {}
    for p in parts:
        if p["assembly"] == "jlc":
            quantity[p["lcsc"]] = quantity.get(p["lcsc"], 0) + 1
    checks = []
    for fp in sorted(pcb.Footprints(), key=lambda fp: fp.GetReference()):
        part = by_ref[fp.GetReference()]
        if part["assembly"] != "jlc":
            continue
        check = Check(fp.GetReference(), part["lcsc"], str(fp.GetFPID().GetLibItemName()))
        try:
            lcsc = part["lcsc"]
            if live and lcsc not in supplier:
                supplier[lcsc] = supplier_data(lcsc, refresh=True)
            data = supplier[lcsc]
            record = data["record"]
            check.jlc_footprint, theirs = data["footprint"], data["pads"]
        except (OSError, KeyError, TypeError, ValueError) as error:
            check.problems.append(f"no JLCPCB data: {error}")
            checks.append(check)
            continue
        check.library = record.get("componentLibraryType", "")
        check.problems += value_problems(part, record)
        if live:
            check.stock = int(record.get("stockCount") or 0)
            if check.stock < quantity[part["lcsc"]] * ORDER_BOARDS:
                check.problems.append(f"stock {check.stock} < {quantity[part['lcsc']] * ORDER_BOARDS} needed")
        ours = []
        for pad in fp.Pads():
            if pad.GetNumber() and pad.GetNetname():
                box = pad.GetBoundingBox()
                ours.append(Pad(pad.GetNumber(), pcbnew.ToMM(box.GetLeft()), -pcbnew.ToMM(box.GetBottom()),
                                pcbnew.ToMM(box.GetRight()), -pcbnew.ToMM(box.GetTop())))
        pad_map = pad_maps.get(part["lcsc"], {}).get("map")
        check.placement = fit(ours, theirs, fp.IsFlipped(), pad_map)
        if check.placement is None:
            check.problems.append(f"JLCPCB footprint {check.jlc_footprint} (pads "
                                  f"{sorted({n for n, _, _ in theirs})}) does not land pin for pin on ours "
                                  f"({sorted({p.number for p in ours})}): wrong package or pin mapping")
        checks.append(check)
    return checks


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["snapshot"])
    parser.add_argument("--cached", action="store_true")
    args = parser.parse_args()
    snapshot(cached=args.cached)
