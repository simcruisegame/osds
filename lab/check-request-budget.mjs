import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {cacheReader,budgetFor,budgetAllowed,spendBudget} from '../helper/atlas-cache.mjs';
const table={default:{burst:3,perKm:2},hosts:{'a.test':{burst:1,perKm:10}}};
assert.equal(budgetFor('a.test',table).perKm,10);
assert.equal(budgetFor('other.test',table).burst,3);
assert.equal(budgetAllowed({burst:3,perKm:2},0),3);
assert.equal(budgetAllowed({burst:3,perKm:2},2.6),8);
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'osds-budget-'));
const state=path.join(dir,'.source-state');fs.mkdirSync(state);
try{
 // No game (no session file): never limited.
 for(let i=0;i<50;i++)assert.ok(spendBudget(state,'a.test',table));
 // A session: the burst, then nothing until the car has driven.
 fs.writeFileSync(path.join(state,'session.json'),JSON.stringify({started:1000,km:0}));
 assert.ok(spendBudget(state,'other.test',table));assert.ok(spendBudget(state,'other.test',table));assert.ok(spendBudget(state,'other.test',table));
 assert.ok(!spendBudget(state,'other.test',table),'burst of 3 spent');
 assert.ok(spendBudget(state,'a.test',table)&&!spendBudget(state,'a.test',table),'each host has its own budget');
 fs.writeFileSync(path.join(state,'session.json'),JSON.stringify({started:1000,km:1}));
 assert.ok(spendBudget(state,'other.test',table)&&spendBudget(state,'other.test',table),'1 km earns 2 more');
 assert.ok(!spendBudget(state,'other.test',table));
 // A new start (new session) earns a new burst.
 fs.writeFileSync(path.join(state,'session.json'),JSON.stringify({started:2000,km:0}));
 assert.ok(spendBudget(state,'other.test',table));
 // The reader: over budget it makes no request and hands back the stale file.
 const cache=path.join(dir,'cache');
 let fetched=0;
 const fetcher=async()=>{fetched++;return new Response('fresh',{status:200})};
 const read=cacheReader(cache,()=>{},fetcher);
 fs.writeFileSync(path.join(cache,'.source-state','session.json'),JSON.stringify({started:5,km:0}));
 fs.writeFileSync(path.join(cache,'.source-state','example.com.spent'),JSON.stringify({started:5,n:99999}));
 fs.writeFileSync(path.join(cache,'old'),'stale');
 fs.utimesSync(path.join(cache,'old'),1,1);
 assert.equal((await read('old','https://example.com/x',1000)).toString(),'stale');
 assert.equal(fetched,0,'no request over budget');
 await assert.rejects(()=>read('new','https://example.com/y',1000),/Request budget spent/);
 assert.equal(fetched,0);
 // The shipped table lists every limited host with a policy.
 const shipped=JSON.parse(fs.readFileSync(new URL('../helper/request-budgets.json',import.meta.url)));
 assert.ok(shipped.hosts['overpass-api.de'].perKm<=0.5,'Overpass is very limited');
 for(const [h,b] of Object.entries(shipped.hosts))assert.ok(b.policy&&b.burst>0&&b.perKm>=0,h);
 console.log('PASS request budgets: burst, per kilometre, per host, new session, stale fallback, shipped table');
}finally{fs.rmSync(dir,{recursive:true,force:true})}
