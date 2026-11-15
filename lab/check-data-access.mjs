// The data access layer: a pack first, a capped live fetch after, never a fetch for a pack-only source.
import {dataAccess,sources} from '../helper/data-access.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'data-access-'));
const pack=fs.mkdtempSync(path.join(os.tmpdir(),'data-pack-'));
try {
 let calls=0;
 const fetcher=async url=>{calls++;return new Response('live:'+new URL(url).hostname)};
 // A pack folder with one EVT tile and one NSI tile.
 fs.mkdirSync(path.join(pack,'evt','14','4500'),{recursive:true});
 fs.writeFileSync(path.join(pack,'evt','14','4500','6500.tif'),'packed');
 fs.mkdirSync(path.join(pack,'nsi','16','100'),{recursive:true});
 fs.writeFileSync(path.join(pack,'nsi','16','100','200.json'),'[]');
 const a=dataAccess(dir,{pack,fetcher});
 let r=await a.get('evt',14,4500,6500);
 assert.equal(r.from,'pack');assert.equal(r.bytes.toString(),'packed');assert.equal(calls,0);
 // Not in the pack: a live source fetches (once, then from the cache), a pack-only source never does.
 r=await a.get('evt',14,4501,6500);assert.equal(r.from,'live');assert.equal(calls,1);
 r=await a.get('evt',14,4501,6500);assert.equal(calls,1,'cached');
 assert.equal((await a.get('nsi',16,100,200)).from,'pack');
 await assert.rejects(a.get('nsi',16,101,200),/Not in the pack/);
 await assert.rejects(a.get('civil',14,1,1),/Not in the pack/);assert.equal(calls,1,'no live fetch for pack-only sources');
 // The day's live cap.
 const capped=dataAccess(dir,{pack:'',fetcher});
 const cap=sources.evt.cap;
 fs.writeFileSync(path.join(dir,'.source-state','live-evt.day'),Math.floor(Date.now()/86400000)+' '+cap);
 await assert.rejects(capped.get('evt',14,4600,6600),/Live cap reached/);
 assert.equal(calls,1);
 await assert.rejects(a.get('nope',14,1,1),/Unknown data source/);
 console.log('PASS data access: pack first, capped live fetch, pack-only sources never fetched');
} finally {fs.rmSync(dir,{recursive:true,force:true});fs.rmSync(pack,{recursive:true,force:true})}
