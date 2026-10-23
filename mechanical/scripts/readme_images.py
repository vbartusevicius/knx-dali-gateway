"""Compose the README images from the release renders.

    python3 mechanical/scripts/readme_images.py      (cad container, after the releases)

Writes docs/readme/: hero.png (both variants, isometric) and, per variant,
<variant>-assembly.png (exploded, inside, section) and <variant>-boards.png
(every board, front and back, at one common scale). Each panel is cropped to
its content and labelled, so the README can show one small image per topic.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "docs/readme"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
MARGIN = 24
GAP = 36
LABEL_H = 44

HERO = [("v3", "iso", "V3 · 1 DIN (18 mm)"), ("v2", "iso", "V2 · 2 DIN (36 mm)")]
ASSEMBLY = [("exploded", "Exploded"), ("inside", "Inside"), ("section", "Side section")]
BOARDS = {
    "v3": [("main_top", "main · front"), ("main_bottom", "main · back"),
           ("ui_top", "ui · front"), ("ui_bottom", "ui · back")],
    "v2": [("main_top", "main · front"), ("main_bottom", "main · back"),
           ("interface_top", "interface · front"), ("interface_bottom", "interface · back"),
           ("ui_top", "ui · front"), ("ui_bottom", "ui · back")],
}


def render(variant, name):
    return REPO / "hardware/releases" / variant / "enclosure/renders" / f"{name}.png"


def cropped(path):
    image = Image.open(path).convert("RGB")
    mask = Image.eval(image.convert("L"), lambda v: 255 if v < 245 else 0)
    x0, y0, x1, y1 = mask.getbbox()
    return image.crop((max(0, x0 - MARGIN), max(0, y0 - MARGIN),
                       min(image.width, x1 + MARGIN), min(image.height, y1 + MARGIN)))


def strip(panels, height, common_scale=True):
    """panels: [(image, label)]. With common_scale, the tallest panel gets
    `height` and the others keep their size relative to it."""
    tallest = max(image.height for image, _ in panels)
    scaled = []
    for image, label in panels:
        factor = height / (tallest if common_scale else image.height)
        size = (round(image.width * factor), round(image.height * factor))
        scaled.append((image.resize(size, Image.LANCZOS), label))
    width = sum(image.width for image, _ in scaled) + GAP * (len(scaled) - 1)
    sheet = Image.new("RGB", (width, height + LABEL_H), "white")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, 22)
    x = 0
    for image, label in scaled:
        sheet.paste(image, (x, height - image.height))
        text_w = draw.textlength(label, font=font)
        draw.text((x + (image.width - text_w) / 2, height + 10), label, fill="#333333", font=font)
        x += image.width + GAP
    return sheet


def save(sheet, name):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    sheet.save(path, optimize=True)
    print(f"wrote {path.relative_to(REPO)} ({sheet.width}x{sheet.height})")


def main():
    save(strip([(cropped(render(v, n)), label) for v, n, label in HERO], 520, common_scale=False), "hero.png")
    for variant, boards in BOARDS.items():
        save(strip([(cropped(render(variant, n)), label) for n, label in ASSEMBLY], 460), f"{variant}-assembly.png")
        save(strip([(cropped(render(variant, n)), label) for n, label in boards], 420), f"{variant}-boards.png")


if __name__ == "__main__":
    main()
