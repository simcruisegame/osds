# Art director: study notes

## The job in a generated world
A hand-built game (GTA, Forza Horizon) gets realism from artists placing and dressing every street. OSDS builds the US at runtime, so realism has to come from **rules that produce what real places contain**: correct light, honest colour, believable density, wear everywhere, no machine-straight edges, and no visible repetition. The art director's tools are the Place recipe, materials and blends, density tables, the light model, and reference photos.

## The realism checklist (in priority order)
1. **Light and exposure.** Real photos: shadows lifted by sky light (not black), shadow colour slightly cool but not blue, sky brightest near the horizon on clear days, haze reducing contrast with distance (atmospheric perspective). Cockpit exposure balanced with the outside, as a camera or eye adapts.
2. **Colour.** Real-world materials are rarely saturated: grass, asphalt, concrete and brick sit in narrow albedo ranges (measured albedo tables; e.g. fresh asphalt about 0.04-0.05, aged asphalt 0.1-0.2, concrete 0.25-0.4: estimate ranges, cite per material when used). Season and climate change vegetation colour.
3. **Scale and density.** Each kind of place has a typical number of things per 100 m: street lights, poles, signs, mailboxes, trees, parked cars. Too few reads empty; too many reads like a game.
4. **Wear and age.** Nothing is new: wheel paths, oil strips, patched cracks, stained kerbs, grime at the base of walls, faded markings, dirt in gutters.
5. **Edges.** Natural and built edges are irregular, chamfered or blended (see the no-hard-edges rule on #64).
6. **Repetition.** Tiling textures, cloned trees and identical buildings are the second most obvious game tell; break them with variation driven by stable per-object seeds.
7. **Silhouettes and distance.** Rounded curves, no popping, trees and buildings that stay present and fade with haze.

## Method
- Always compare against a licensed photo of the same place (Panoramax, Wikimedia Commons) or the same kind of place; use the realism scorer for numbers and the eye for the verdict.
- Look at the frame small (thumbnail) for value and colour, then large for edges and repetition.
- Fix the cheapest, most visible tell first.

## Sources to start from
- Sandberg & Ejsmont-style reference books exist for sound; for look, use: Physically Based Shading course notes (SIGGRAPH, Hoffman and others), Epic's UE "Physically Based Materials" docs, measured albedo tables (e.g. the DONTNOD/Unreal "physically based albedo" charts), and photographic references from Panoramax and Commons.
- How large open worlds are built: Microsoft Flight Simulator (ML-generated buildings from aerial imagery), Forza Horizon 5 (ruleset-driven terrain material and road blending), GTA (density and decal layering). See the research notes from 2026-11-22.
