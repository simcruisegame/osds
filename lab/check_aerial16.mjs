// The scan eye's zoom-16 photograph (helper command aerial16): offline checks.
// It answers nothing outside the lower 48 (no fetch), and the source cache manages and evicts its files.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {trimSourceCache} from '../helper/cache-eviction.mjs';

const dir=fs.mkdtempSync(path.join(os.tmpdir(),'osds-aerial16-'));
try{
 // Honolulu (Hawaii) and Juneau (Alaska) at zoom 16: regional sources, so no request at all.
 for(const [x,y] of [[4028,28796],[8296,19632]]){
  const run=spawnSync(process.execPath,[path.join(import.meta.dir,'..','helper','atlas.mjs'),'aerial16',dir,String(x),String(y)],{encoding:'utf8'});
  assert.equal(run.status,0,run.stderr);
  const out=JSON.parse(run.stdout.trim().split('\n').pop());
  const result=out.result??out;
  assert.equal(result.aerial16,undefined);
  assert.equal(result.requests,0);
 }
 // a16 files are managed by the cache budget like the other photographs (oldest first).
 const big=Buffer.alloc(400*1024,1);
 for(let i=0;i<4;i++){const f=path.join(dir,`a16-${100+i}-200.jpg`);fs.writeFileSync(f,big);const t=new Date(Date.now()-(10-i)*60000);fs.utimesSync(f,t,t)}
 const result={};
 trimSourceCache(dir,1024*1024,result);
 const left=fs.readdirSync(dir).filter(n=>n.startsWith('a16-'));
 assert.ok(left.length<4,'a16 photographs are evicted under the budget');
 assert.ok(left.includes('a16-103-200.jpg'),'the newest stays');
 console.log('aerial16 ok');
}finally{fs.rmSync(dir,{recursive:true,force:true})}
