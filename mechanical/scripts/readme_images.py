"""Compose the README images from the release renders.

    python3 mechanical/scripts/readme_images.py      (cad container, after the releases)

Writes docs/readme/: hero.png (both variants, isometric) and, per variant,
<variant>-assembly.png (exploded, inside, section) and <variant>-boards.png
(every board, front and back, photorealistic KiCad 10 raytraced 3D renders
at one common physical scale).
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
    "v3": [("main", "top", "main · front"), ("main", "bottom", "main · back"),
           ("ui", "top", "ui · front"), ("ui", "bottom", "ui · back")],
    "v2": [("main", "top", "main · front"), ("main", "bottom", "main · back"),
           ("interface", "top", "interface · front"), ("interface", "bottom", "interface · back"),
           ("ui", "top", "ui · front"), ("ui", "bottom", "ui · back")],
}


def render(variant, name):
    return REPO / "hardware/releases" / variant / "enclosure/renders" / f"{name}.png"


def board_render_3d(variant, board, side):
    return REPO / "hardware/releases" / variant / board / "renders" / f"{side}_3d.png"


def cropped(path, bg="white"):
    raw = Image.open(path)
    if raw.mode in ("RGBA", "LA") or "transparency" in raw.info:
        rgba = raw.convert("RGBA")
        # KiCad 10 high-quality renders include semi-transparent floor shadows;
        # strip low-alpha shadow haze so panel edges stay crisp on white.
        alpha = rgba.getchannel("A").point(lambda a: a if a >= 200 else 0)
        bbox = alpha.getbbox()
        canvas = Image.new("RGB", rgba.size, bg)
        canvas.paste(rgba, mask=alpha)
        image = canvas
    else:
        image = raw.convert("RGB")
        mask = Image.eval(image.convert("L"), lambda v: 255 if v < 245 else 0)
        bbox = mask.getbbox()
    if not bbox:
        return image
    x0, y0, x1, y1 = bbox
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
        save(strip([(cropped(board_render_3d(variant, b, s)), label) for b, s, label in boards], 540),
             f"{variant}-boards.png")


if __name__ == "__main__":
    main()
