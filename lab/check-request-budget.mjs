import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {cacheReader,budgetFor,budgetAllowed,budgetTier,budgetReport,spendBudget} from '../helper/atlas-cache.mjs';
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
 // Tiers: an essential host is never refused (but still counted); an enrichment host is refused over its own allowance.
 {
  const tiered={default:{burst:1,perKm:1},hosts:{'road.test':{burst:2,perKm:1,tier:'essential'},'fun.test':{burst:2,perKm:1,tier:'enrichment'}}};
  assert.equal(budgetTier(tiered.hosts['road.test']),'essential');
  assert.equal(budgetTier(tiered.hosts['fun.test']),'enrichment');
  assert.equal(budgetTier(tiered.default),'enrichment','no tier means enrichment');
  fs.writeFileSync(path.join(state,'session.json'),JSON.stringify({started:3000,km:0}));
  for(let i=0;i<10;i++)assert.ok(spendBudget(state,'road.test',tiered),'essential is never refused');
  assert.ok(spendBudget(state,'fun.test',tiered)&&spendBudget(state,'fun.test',tiered));
  assert.ok(!spendBudget(state,'fun.test',tiered),'enrichment refused over its own allowance');
  assert.ok(spendBudget(state,'road.test',tiered),'enrichment being over never takes from essential');
  assert.ok(spendBudget(state,'new.test',tiered)&&!spendBudget(state,'new.test',tiered),'an unlisted host is enrichment under default');
  const report=budgetReport(state,tiered);
  assert.deepEqual(report['road.test'],{n:11,allowed:2,tier:'essential'},'essential spend is counted past its allowance');
  assert.deepEqual(report['fun.test'],{n:2,allowed:2,tier:'enrichment'});
  assert.deepEqual(report['new.test'],{n:1,allowed:1,tier:'enrichment'});
  fs.writeFileSync(path.join(state,'session.json'),JSON.stringify({started:3001,km:1}));
  assert.deepEqual(budgetReport(state,tiered)['road.test'],{n:0,allowed:3,tier:'essential'},'a new session starts at zero');
  fs.rmSync(path.join(state,'session.json'));
  assert.deepEqual(budgetReport(state,tiered),{},'no game, no report');
 }
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
 for(const [h,b] of Object.entries(shipped.hosts))assert.ok(b.policy&&b.burst>0&&b.perKm>=0&&['essential','enrichment'].includes(b.tier),h);
 for(const h of ['tiles.openfreemap.org','elevation.nationalmap.gov','imagery.geoplatform.gov','basemap.nationalmap.gov'])assert.equal(budgetTier(shipped.hosts[h]),'essential',h);
 for(const h of ['photon.komoot.io','overpass-api.de','api.panoramax.xyz','aviationweather.gov','mesonet.agron.iastate.edu'])assert.equal(budgetTier(shipped.hosts[h]),'enrichment',h);
 console.log('PASS request budgets: burst, per kilometre, per host, tiers, report, new session, stale fallback, shipped table');
}finally{fs.rmSync(dir,{recursive:true,force:true})}
