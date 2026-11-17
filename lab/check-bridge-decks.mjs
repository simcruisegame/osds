// Offline test of the bridge deck end heights in helper/road-grade.mjs
// (bridgeEndHeights). Synthetic pieces in metres, no network, no elevation tiles.
//
// The owner's report for v2.0.4: "the bridges go up and down, not flat across".
// Two causes in the helper: an end a tile could not measure copied the other
// end's height (a flat deck in the first tile built, a sloped one in the next),
// and a bridge cut into chains where a ramp joins on the deck took the ground
// under that joint (the valley floor) as an end height, a V.
import assert from 'node:assert/strict';
import {bridgeEndHeights} from '../helper/road-grade.mjs';

const worldKey = (x, y) => Math.round(x * 100) + '-' + Math.round(y * 100);
const memoOf = (entries) => { const m = new Map(entries.map(([x, y, z]) => [worldKey(x, y), z])); return m; };
const valley = () => 0; // the ground under the deck: a valley floor at 0 m
const nowhere = () => false; // nothing measured in this tile: the remembered heights only
const near = (a, b, tol = 0.02) => Math.abs(a - b) <= tol;

// 1. A straight deck cut at a tile edge into two pieces: the joint is inside the bridge and lies
//    on the straight line between the abutments (10 m and 14 m), not on the valley floor.
{
 const pieces = [{pts: [[0, 0], [60, 0]]}, {pts: [[60, 0], [100, 0]]}];
 const at = await bridgeEndHeights(pieces, [], valley, nowhere, memoOf([[0, 0, 10], [100, 0, 14]]), worldKey);
 assert.ok(near(await at([0, 0]), 10), 'abutment A keeps its remembered height');
 assert.ok(near(await at([100, 0]), 14), 'abutment B keeps its remembered height');
 assert.ok(near(await at([60, 0]), 12.4), `the joint is on the line between the abutments (${await at([60, 0])})`);
}

// 2. A ramp joins at the middle of the deck: the joint is not the valley floor.
{
 const pieces = [{pts: [[0, 0], [50, 0]]}, {pts: [[50, 0], [100, 0]]}, {pts: [[50, 0], [80, 40]]}];
 const at = await bridgeEndHeights(pieces, [], valley, nowhere, memoOf([[0, 0, 10], [100, 0, 14], [80, 40, 13]]), worldKey);
 const z = await at([50, 0]);
 assert.ok(z > 10 && z < 14, `the ramp joint lies between the ends' heights, not on the ground (${z})`);
}

// 3. An end nobody has measured yet: the elevation model there, never the other end's height.
{
 const pieces = [{pts: [[0, 0], [100, 0]]}];
 const ground = (x) => 5 + x * 0.05; // rising ground: 5 m at A, 10 m at B
 const at = await bridgeEndHeights(pieces, [], ground, nowhere, memoOf([[0, 0, 6]]), worldKey);
 assert.ok(near(await at([0, 0]), 6), 'the remembered end');
 assert.ok(near(await at([100, 0]), 10), `the unmeasured end takes the ground there (${await at([100, 0])}), not the other end's 6 m`);
}

// 4. The same bridge seen from two tiles gives the same heights (no build-order dependence):
//    whatever one tile remembers, the other reads.
{
 const pieces = [{pts: [[0, 0], [60, 0]]}, {pts: [[60, 0], [140, 0]]}];
 const memo = memoOf([[0, 0, 20], [140, 0, 26]]);
 const a = await bridgeEndHeights(pieces, [], valley, nowhere, memo, worldKey);
 const b = await bridgeEndHeights(pieces.slice().reverse(), [], valley, nowhere, memo, worldKey);
 for (const p of [[0, 0], [60, 0], [140, 0]]) assert.ok(near(await a(p), await b(p)), `both tiles agree at ${p}`);
}

// 5. A long bridge cut by the vector tile's edge (extent 1000): the cut ends are not abutments on the
//    lake (0 m). With a true end in the tile, the cut end takes its height (a level deck); with none,
//    6 m over the water; and a remembered cut height (the tile on the other side) wins.
{
 const extent=1000;
 const water=()=>0;
 const one=[{pts:[[600,500],[1000,500]]}];
 let at=await bridgeEndHeights(one,[],water,nowhere,memoOf([[600,500,25]]),worldKey,extent);
 assert.ok(near(await at([1000,500]),25),`the cut end continues the true end's height (${await at([1000,500])})`);
 const span=[{pts:[[0,500],[1000,500]]}];
 const memo=memoOf([]);
 at=await bridgeEndHeights(span,[],water,nowhere,memo,worldKey,extent);
 assert.ok(near(await at([0,500]),6)&&near(await at([1000,500]),6),'a span cut at both ends stands 6 m over the water, not on it');
 assert.ok(near(memo.get(worldKey(1000,500)),6),'the cut height is remembered for the tile on the other side');
 at=await bridgeEndHeights(span,[],water,nowhere,memoOf([[1000,500,31]]),worldKey,extent);
 assert.ok(near(await at([1000,500]),31),'a remembered cut height wins');
}

// 6. The two tiles of a cut see the bridge run on into their buffers (1.5 % past the edge): both
//    find the same crossing point, so the height one remembers is the one the other reads.
{
 const extent=1000;
 const memo=memoOf([[500,300,25]]); // the north tile's abutment, measured: 25 m
 const north=[{pts:[[500,300],[500,1015]]}],south=[{pts:[[500,-15],[500,700]]}];
 const a=await bridgeEndHeights(north,[],()=>0,nowhere,memo,worldKey,extent);
 const za=await a([500,1015]);
 // The south tile's coordinates start at the cut: its (500, 0) is the north tile's (500, 1000).
 const shifted=(x,y)=>worldKey(x,y+1000);
 const b=await bridgeEndHeights(south,[],()=>0,nowhere,memo,shifted,extent);
 assert.ok(near(za,25),`the north side's cut continues its abutment (${za})`);
 assert.ok(near(await b([500,-15]),za),`both sides of the cut meet (${za} vs ${await b([500,-15])})`);
}

console.log('check-bridge-decks: ok');
