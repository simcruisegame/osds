"""Paints the brand lettering out of the CARLA Mini's baked atlases (no badges or tyre logos in OSDS).

Each box is a texture rectangle (x0, y0, x1, y1 in the 1024 px atlas) holding lettering: pixels there
that stand out from the box's border colour (every pixel in a "flat" box) are set to it, in the base colour, and the normal map is
flattened inside the box so embossed letters go too. Run once after the glTF export:
  python scripts/osds-cars/debadge_carla_mini.py data/art/traffic/minif56-carla
"""
import sys
from PIL import Image

BOXES = {
    "MI_MiniCooper_Details02": [
        ("flat", 415, 12, 655, 58),   # tyre sidewall maker name (mirrored)
        ("flat", 320, 66, 495, 108),  # tyre sidewall model name
        (770, 838, 862, 884),  # MINI lettering (tailgate)
        (4, 950, 112, 996),    # COOPER lettering (tailgate)
        ("flat", 238, 712, 346, 768),  # winged logo (bonnet, tailgate release)
        ("flat", 282, 232, 378, 262),  # dark winged logo printed on the tailgate release
        ("flat", 246, 782, 350, 846),  # winged logo
    ],
    "MI_MiniCooper_Details01": [
        (4, 60, 66, 100),      # MINI label
        (490, 664, 550, 696),  # MINI on the black shield
        ("flat", 0, 843, 1024, 868),  # tread band lettering
        (0, 992, 1024, 1022),  # tread band lettering
    ],
}


# Areas recoloured to match the real car (base colour only): the F56's dash top is black soft-touch
# plastic; the CARLA atlas paints it, and some cabin trim, light grey.
RECOLOUR = {
    "MI_MiniCooper_Details02": [((612, 880, 686, 975), (36, 36, 38)), ((818, 286, 910, 454), (40, 40, 42))],
    "MI_MiniCooper_Details01": [((660, 0, 812, 70), (40, 40, 42))],
}


def border_colour(px, box):
    x0, y0, x1, y1 = box
    ring = [px[x, y0] for x in range(x0, x1)] + [px[x, y1 - 1] for x in range(x0, x1)] + \
           [px[x0, y] for y in range(y0, y1)] + [px[x1 - 1, y] for y in range(y0, y1)]
    return tuple(sorted(c[i] for c in ring)[len(ring) // 2] for i in range(3))


def main(folder):
    for mat, boxes in BOXES.items():
        p = f"{folder}/{mat}_BaseColor.png"
        im = Image.open(p).convert("RGB")
        px = im.load()
        for box in boxes:
            flat = box[0] == "flat"
            box = box[1:] if flat else box
            c = border_colour(px, box)
            for y in range(box[1], box[3]):
                for x in range(box[0], box[2]):
                    q = px[x, y]
                    if flat or sum(abs(q[i] - c[i]) for i in range(3)) > 40:
                        px[x, y] = c
        for box, colour in RECOLOUR.get(mat, []):
            for y in range(box[1], box[3]):
                for x in range(box[0], box[2]):
                    q = px[x, y]
                    # Keep the texture's shading: scale each pixel down to the target's level.
                    level = max(sum(q) / 3.0, 1.0)
                    px[x, y] = tuple(min(255, int(colour[i] * (0.75 + 0.5 * level / 160.0))) for i in range(3))
        im.save(p)
        try:
            n = Image.open(f"{folder}/{mat}_Normal.png").convert("RGB")
            npx = n.load()
            for box in boxes:
                box = box[1:] if box[0] == "flat" else box
                for y in range(box[1], box[3]):
                    for x in range(box[0], box[2]):
                        npx[x, y] = (128, 128, 255)
            n.save(f"{folder}/{mat}_Normal.png")
        except FileNotFoundError:
            pass
        # The recoloured cabin trim is matte plastic: the bake gave it mirror-gloss metal
        # (roughness 14, metallic 242 of 255), so the dash reflected the sky.
        try:
            r = Image.open(f"{folder}/{mat}_MetallicRoughness.png").convert("RGB")
            rpx = r.load()
            for box, _ in RECOLOUR.get(mat, []):
                for y in range(box[1], box[3]):
                    for x in range(box[0], box[2]):
                        rpx[x, y] = (rpx[x, y][0], 200, 0)
            r.save(f"{folder}/{mat}_MetallicRoughness.png")
        except FileNotFoundError:
            pass
        # Lettering moulded into the tyre also shows in the roughness: flatten the flat boxes there.
        try:
            r = Image.open(f"{folder}/{mat}_MetallicRoughness.png").convert("RGB")
            rpx = r.load()
            for box in boxes:
                if box[0] != "flat":
                    continue
                c = border_colour(rpx, box[1:])
                for y in range(box[2], box[4]):
                    for x in range(box[1], box[3]):
                        rpx[x, y] = c
            r.save(f"{folder}/{mat}_MetallicRoughness.png")
        except FileNotFoundError:
            pass
        print("cleaned", mat, len(boxes), "boxes")


if __name__ == "__main__":
    main(sys.argv[1])
