"""The wall and roof library (scan contract GENR, v2.0.4): photographs of US wall and roof materials made
once, offline, on the development PC's GPU, with Stable Diffusion XL base 1.0 (CreativeML Open RAIL++-M:
use allowed under its use restrictions; Stability AI claims no rights in the output). No runtime GPU:
the game ships the textures, and per building the scan picks a key (material, style, region, era, use)
and tints it with the place's colours (TINT, roof colour from the photo).

    python wall_library.py --list                 # the keys, prompts and seeds (no GPU)
    python wall_library.py --out raw --only brick  # generate (torch + diffusers, see lab/detail-kit/README.md)

Every entry records model, licence, prompt, seed, steps and guidance in raw/library.json (GENR META).
Neutral colours where the game tints (paint, stucco, siding); natural colour variants where the
material has its own colour (brick, stone, wood, metal). Each texture shows about 2 m of wall (bricks at
real size) or 1 m of roof or ground, frontal, flat light, so the game's lighting does the shading.
"""
import argparse
import json
import os

MODEL = "stabilityai/stable-diffusion-xl-base-1.0"
WALL = ("frontal orthographic close-up photograph of a building wall, flat even overcast light, no shadows, "
        "no perspective, no windows, no doors, no objects, fills the frame, seamless texture, sharp focus, about 2 metres across")
ROOF = ("top-down orthographic close-up photograph of a roof surface, flat even overcast light, no shadows, no perspective, "
        "fills the frame, seamless texture, sharp focus, about 1 metre across")
GROUND = ("top-down orthographic close-up photograph of the ground, flat even overcast light, no shadows, no perspective, "
          "fills the frame, seamless texture, sharp focus, about 1 metre across")
NEGATIVE = ("shadow, vignette, perspective, horizon, sky, window, door, person, car, text, watermark, logo, sign, graffiti, "
            "border, frame, blurry, painting, illustration, 3d render, cartoon")

# key: (surface, material, style) -> subject; regions and eras say where the scan uses it.
ENTRIES = [
    # Brick: natural colours.
    ("wall", "brick", "common_red_old", "old common red brick wall, running bond, worn lime mortar, chipped edges, varied brick colour", "northeast,midwest,south", "1850-1950"),
    ("wall", "brick", "face_red_modern", "modern red face brick wall, running bond, crisp grey mortar joints, uniform bricks", "all", "1950-2025"),
    ("wall", "brick", "buff_yellow", "buff yellow brick wall, running bond, light mortar, slight colour variation", "midwest,southwest,texas", "1900-1980"),
    ("wall", "brick", "dark_brown_ironspot", "dark brown iron-spot brick wall, running bond, dark mortar", "midwest,northeast", "1950-1990"),
    ("wall", "brick", "painted_white", "painted white brick wall, running bond, paint slightly worn on the edges", "all", "1880-2025"),
    ("wall", "brick", "chicago_common", "Chicago common brick wall, mixed salmon and tan bricks, rough faces, wide mortar joints", "midwest", "1880-1940"),
    # Stucco and plaster: neutral, tinted in the game.
    ("wall", "stucco", "smooth_mission", "smooth hand-troweled white stucco wall, Spanish mission style, gentle waves, hairline cracks", "southwest,california,florida", "1900-2025"),
    ("wall", "stucco", "sand_float", "white sand float finish stucco wall, fine even grain", "all", "1950-2025"),
    ("wall", "stucco", "knockdown", "white knockdown texture stucco wall, flattened splatter pattern", "southwest,texas,florida", "1970-2025"),
    ("wall", "stucco", "aged_stained", "old off-white stucco wall, rain streaks below, patched cracks, faded paint", "southwest,california,florida", "1900-1970"),
    # Wood and vinyl siding: neutral, tinted in the game.
    ("wall", "siding", "clapboard_painted", "white painted wooden clapboard siding, horizontal lap boards 10 cm exposure, paint slightly weathered", "northeast,midwest,south", "1800-1960"),
    ("wall", "siding", "vinyl_lap", "white vinyl lap siding, horizontal double 4 inch profile, faint wood grain embossing", "all", "1970-2025"),
    ("wall", "siding", "board_and_batten", "white painted vertical board and batten wood siding", "rural,west,south", "1850-2025"),
    ("wall", "siding", "cedar_shingle_natural", "weathered grey cedar shingle siding, overlapping courses, Cape Cod", "northeast,northwest", "1700-2025"),
    ("wall", "siding", "fiber_cement_lap", "pale grey fiber cement lap siding, smooth, crisp shadow lines", "all", "2000-2025"),
    ("wall", "siding", "t111_plywood", "light painted T1-11 plywood siding, vertical grooves every 20 cm", "rural,west", "1970-2000"),
    ("wall", "log", "log_cabin", "stacked round log wall, honey brown logs, chinking between them", "mountain,northwest,rural", "1800-2025"),
    # Concrete and block.
    ("wall", "concrete", "cmu_painted", "painted concrete masonry block wall, running bond, light grey paint", "all", "1950-2025"),
    ("wall", "concrete", "cmu_split_face", "split-face concrete block wall, tan, rough faces, running bond", "southwest,all", "1970-2025"),
    ("wall", "concrete", "tilt_up_panel", "large smooth precast concrete tilt-up panel wall, light grey, form tie holes, joint line", "all", "1960-2025"),
    ("wall", "concrete", "board_formed", "board-formed concrete wall, wood grain imprint, grey", "all", "1950-2025"),
    # Stone.
    ("wall", "stone", "fieldstone", "fieldstone wall, rounded grey and brown stones of mixed size, deep mortar", "northeast,midwest,rural", "1750-1950"),
    ("wall", "stone", "limestone_ashlar", "cut limestone ashlar wall, pale cream blocks, thin joints", "midwest,south,civic", "1850-1950"),
    ("wall", "stone", "brownstone", "brownstone wall, reddish brown sandstone blocks, weathered faces", "northeast", "1850-1910"),
    ("wall", "stone", "river_rock", "river rock veneer wall, rounded smooth stones in grey mortar", "west,mountain", "1920-1970"),
    ("wall", "stone", "adobe", "adobe mud plaster wall, earthy tan, soft rounded surface, straw bits", "southwest", "1700-2025"),
    # Metal.
    ("wall", "metal", "corrugated_galvanized", "corrugated galvanized steel wall, vertical ribs, light rust stains", "rural,farm,industrial", "1900-2025"),
    ("wall", "metal", "ribbed_panel_painted", "painted ribbed steel wall panel, R-panel profile, light grey", "industrial,farm", "1970-2025"),
    # Roofs.
    ("roof", "shingle", "asphalt_3tab", "grey three-tab asphalt shingle roof, granular surface, straight courses", "all", "1950-2000"),
    ("roof", "shingle", "asphalt_architectural", "dark weathered-wood architectural asphalt shingle roof, layered tabs", "all", "1990-2025"),
    ("roof", "tile", "clay_spanish", "terracotta clay Spanish barrel tile roof, rows of curved tiles", "southwest,california,florida", "1900-2025"),
    ("roof", "tile", "concrete_flat_tile", "flat grey concrete roof tiles, overlapping rows", "southwest,florida", "1970-2025"),
    ("roof", "metal", "standing_seam", "standing seam metal roof, light grey painted steel, vertical seams 40 cm apart", "all,rural,mountain", "1950-2025"),
    ("roof", "metal", "corrugated_rusty", "old corrugated metal roof with rust patches", "rural,farm", "1900-1980"),
    ("roof", "membrane", "flat_white_tpo", "flat white membrane roof, seams, light dirt", "commercial", "1980-2025"),
    ("roof", "membrane", "gravel_ballast", "flat roof with grey gravel ballast", "commercial,city", "1950-2000"),
    ("roof", "wood", "cedar_shake", "weathered cedar shake roof, split wood shakes, grey brown", "northwest,northeast,mountain", "1900-2025"),
    ("roof", "slate", "slate_grey", "grey slate roof, rectangular slates, slight colour variation", "northeast", "1850-1950"),
    # Ground fine detail (the meso pattern comes from the photo, PHTX).
    ("ground", "grass", "lawn_mown", "mown lawn grass, short green blades, some clover, a few brown blades", "all", "-"),
    ("ground", "grass", "dry_summer", "dry summer grass, straw coloured blades, bare soil between them", "west,south", "-"),
    ("ground", "grass", "meadow_tall", "tall meadow grass, mixed green and seed heads", "all", "-"),
    ("ground", "soil", "farm_field_bare", "bare ploughed farm field soil, clods and furrows", "midwest,south,west", "-"),
    ("ground", "soil", "crop_stubble", "harvested corn field stubble, cut stalks and dry leaves on soil", "midwest", "-"),
    ("ground", "soil", "desert_pavement", "desert ground, small dark pebbles on pale sand, sparse", "southwest", "-"),
    ("ground", "soil", "red_clay", "red clay soil, slightly cracked, a few pebbles", "south", "-"),
    ("ground", "gravel", "driveway_gravel", "grey crushed gravel driveway, angular stones 1-2 cm", "all", "-"),
    ("ground", "litter", "pine_needles", "forest floor of brown pine needles and small cones", "south,west,northwest", "-"),
    ("ground", "litter", "oak_leaves", "forest floor of dry brown oak leaves and twigs", "northeast,midwest,south", "-"),
    ("ground", "snow", "old_snow", "old granular snow, slightly dirty, footprints smoothed", "north,mountain", "-"),
]
SEED0 = 20261115


def records():
    out = []
    for i, (surface, material, style, subject, regions, era) in enumerate(ENTRIES):
        common = {"wall": WALL, "roof": ROOF, "ground": GROUND}[surface]
        out.append({"index": i, "key": f"{surface}/{material}/{style}", "surface": surface, "material": material, "style": style,
                    "regions": regions.split(","), "era": era, "prompt": f"{subject}, {common}", "seed": SEED0 + i,
                    "tint": "neutral" if material in ("stucco", "siding") and "natural" not in style else "natural"})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="raw")
    ap.add_argument("--steps", type=int, default=30)
    ap.add_argument("--guidance", type=float, default=6.0)
    ap.add_argument("--only", default="")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    recs = records()
    if a.list:
        for r in recs:
            print(r["key"], "|", r["regions"], r["era"], "|", r["tint"])
        print(len(recs), "entries")
        return
    import torch
    from diffusers import StableDiffusionXLPipeline
    os.makedirs(a.out, exist_ok=True)
    pipe = StableDiffusionXLPipeline.from_pretrained(MODEL, torch_dtype=torch.float16, variant="fp16", use_safetensors=True)
    pipe.enable_model_cpu_offload()  # 8 GB laptop GPU
    pipe.vae.enable_tiling()
    meta = {"model": MODEL, "licence": "CreativeML Open RAIL++-M", "steps": a.steps, "guidance": a.guidance, "size": 1024,
            "negative": NEGATIVE, "entries": recs}
    with open(os.path.join(a.out, "library.json"), "w") as f:
        json.dump(meta, f, indent=1)
    for r in recs:
        if a.only and not any(o in r["key"] for o in a.only.split(",")):
            continue
        path = os.path.join(a.out, r["key"].replace("/", "_") + ".png")
        if os.path.exists(path):
            continue
        g = torch.Generator("cpu").manual_seed(r["seed"])
        img = pipe(r["prompt"], negative_prompt=NEGATIVE, num_inference_steps=a.steps, guidance_scale=a.guidance,
                   width=1024, height=1024, generator=g).images[0]
        img.save(path)
        print("made", path, flush=True)


if __name__ == "__main__":
    main()
