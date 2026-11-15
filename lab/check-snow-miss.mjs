// The snow map's negative cache: a tile with no answer for a date is not asked again that day.
import {regionalSnow} from '../helper/regional-snow.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'snow-miss-'));
try {
 let asked=0;
 const failing=async()=>{asked++;throw Error('Map service returned 404')};
 const first=await regionalSnow(1000,3000,'2026-01-10',failing,dir);
 assert.equal(first.status,'unavailable');
 const firstAsked=asked;
 assert.ok(firstAsked>=1&&firstAsked<=4,'asks each of its tiles once');
 await regionalSnow(1000,3000,'2026-01-10',failing,dir);
 assert.equal(asked,firstAsked,'the same date is not asked again within the day');
 await regionalSnow(1000,3000,'2026-01-09',failing,dir);
 assert.equal(asked,2*firstAsked,'another date is asked');
 for(const f of fs.readdirSync(dir))fs.utimesSync(path.join(dir,f),new Date(Date.now()-2*86400000),new Date(Date.now()-2*86400000));
 await regionalSnow(1000,3000,'2026-01-10',failing,dir);
 assert.equal(asked,3*firstAsked,'asked again after a day');
 console.log('PASS snow tiles with no answer are left alone for a day');
} finally {fs.rmSync(dir,{recursive:true,force:true})}
