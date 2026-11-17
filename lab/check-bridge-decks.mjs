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

console.log('check-bridge-decks: ok');
