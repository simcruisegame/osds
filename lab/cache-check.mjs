import {cacheReader,backoffMs,hostCap,userAgent} from '../helper/atlas-cache.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'drive-cache-'));
try {
 let calls=0;let limited=false;
 const fetcher=async()=>{await new Promise(r=>setTimeout(r,25));return limited?new Response('',{status:429,headers:{'Retry-After':'120'}}):new Response('tile')};
 const get=cacheReader(dir,()=>calls++,fetcher);
 const url='https://example.org/data';
 await Promise.all([get('v1',url,3600000),get('v1',url,3600000)]);
 assert.equal(calls,1);
 limited=true;
 assert.equal((await get('v1',url,0)).toString(),'tile');assert.equal(calls,2);
 await assert.rejects(get('v2',url,0),/pause/);assert.equal(calls,2);
 assert.equal((await get('v1',url,0)).toString(),'tile');assert.equal(calls,2);
 let started=0,active=0,peak=0;
 const limitedGet=cacheReader(dir,()=>started++,async()=>{active++;peak=Math.max(peak,active);await new Promise(r=>setTimeout(r,35));active--;return new Response('tile')});
 await Promise.all(Array.from({length:9},(_,i)=>limitedGet('of-'+i,'https://tiles.openfreemap.org/'+i,60000)));
 assert.equal(started,9);assert.equal(peak,4);
 await limitedGet('wx1','https://aviationweather.gov/api/data/metar',60000);
 await assert.rejects(limitedGet('wx2','https://aviationweather.gov/api/data/metar',60000),/interval/);
 assert.equal(started,10);
 // Every other host: at most two requests at a time.
 started=0;active=0;peak=0;
 await Promise.all(Array.from({length:7},(_,i)=>limitedGet('s3-'+i,'https://bucket.example.com/'+i,60000)));
 assert.equal(started,7);assert.equal(peak,2);assert.equal(hostCap('bucket.example.com'),2);
 // A failing host is paused for a time that grows with each failure in a row, and one success resets it.
 assert.equal(backoffMs(1,()=>0.5),15000);assert.equal(backoffMs(2,()=>0.5),30000);assert.equal(backoffMs(99,()=>0.5),6*3600000);
 let sent=0,down=true,agent='';
 const flaky=cacheReader(dir,()=>sent++,async(u,o)=>{agent=o.headers['User-Agent'];return down?new Response('',{status:502}):new Response('ok')});
 await assert.rejects(flaky('f1','https://flaky.example.net/a',0),/502/);assert.equal(sent,1);
 await assert.rejects(flaky('f2','https://flaky.example.net/b',0),/pause/);assert.equal(sent,1);
 const until=Number(fs.readFileSync(path.join(dir,'.source-state','flaky.example.net.until'),'utf8'));
 assert.ok(until-Date.now()>10000&&until-Date.now()<20000,'first pause about 15 s');
 fs.rmSync(path.join(dir,'.source-state','flaky.example.net.until'));
 await assert.rejects(flaky('f3','https://flaky.example.net/c',0),/502/);
 const until2=Number(fs.readFileSync(path.join(dir,'.source-state','flaky.example.net.until'),'utf8'));
 assert.ok(until2-Date.now()>22000,'second pause about 30 s');
 fs.rmSync(path.join(dir,'.source-state','flaky.example.net.until'));down=false;
 assert.equal((await flaky('f4','https://flaky.example.net/d',0)).toString(),'ok');
 assert.ok(!fs.existsSync(path.join(dir,'.source-state','flaky.example.net.fails')),'a success resets the count');
 // A missing tile (404) is not the host's fault: no pause.
 const missing=cacheReader(dir,()=>{},async()=>new Response('',{status:404}));
 await assert.rejects(missing('m1','https://tiles.example.org/x',0),/404/);
 assert.ok(!fs.existsSync(path.join(dir,'.source-state','tiles.example.org.until')));
 // One generic User-Agent naming the game, no person.
 assert.equal(agent,userAgent);assert.match(userAgent,/^OSDS\/[0-9A-Za-z.+-]{1,24} \(open-source driving simulator; \+https:\/\/github\.com\/simcruisegame\/osds\)$/);assert.ok(!userAgent.includes('2.0.3'),'the version is read, not hard-coded');assert.equal(userAgent,'OSDS/'+(process.env.OSDS_VERSION||'dev')+' (open-source driving simulator; +https://github.com/simcruisegame/osds)');
 console.log('PASS concurrent miss, warm read, stale fallback, provider-wide Retry-After, per-host cap, growing backoff and User-Agent');
} finally {fs.rmSync(dir,{recursive:true,force:true})}
