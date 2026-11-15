import fs from 'node:fs';
import path from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const minute = 60000;
// One User-Agent for the helper and the game (generic: the project and its public page, no person).
export const userAgent='OSDS/2.0.3 (open-source driving simulator; +https://github.com/simcruisegame/simcruise)';
// A host that fails (a network error, a timeout, 429, 403 or a 5xx) is left alone for a while that
// grows with each failure in a row: 15 s, 30 s, 1 min ... up to 6 h, with a fifth of jitter so the
// players' helpers do not come back at once; one success resets it. Shared by every helper process
// through the cache's source-state files. 429 and 503 also honour Retry-After.
export function backoffMs(failures,random=Math.random){
 const base=Math.min(6*3600000,15000*2**Math.max(0,Math.min(failures-1,20)));
 return Math.round(base*(0.8+0.4*random()));
}
// Every host gets at most two requests at a time from this computer (OpenFreeMap four).
export function hostCap(host){return host==='tiles.openfreemap.org'?4:2}
function read(file) {
 try { return fs.readFileSync(file); }
 catch (error) { if (error.code !== 'ENOENT') throw error; }
}
function atomic(file, bytes) {
 const temp = file + '.' + process.pid + '.' + crypto.randomUUID() + '.tmp';
 try { fs.writeFileSync(temp, bytes); fs.renameSync(temp, file); }
 finally { fs.rmSync(temp, {force: true}); }
}
function age(file) {
 return Date.now() - (fs.statSync(file, {throwIfNoEntry:false})?.mtimeMs ?? 0);
}
// These gates are shared by the native helper processes using this cache.
async function acquireNetworkSlot(state, host) {
 const cap=hostCap(host);
 const federalImagery=host==='imagery.geoplatform.gov';
 const forestProvider=host==='prd-tnm.s3.amazonaws.com'||host==='index.nationalmap.gov'||host==='usgs-lidar-public.s3.amazonaws.com'||host==='lfps.usgs.gov'||host==='www.mrlc.gov'||host==='elevation.nationalmap.gov'||host==='basemap.nationalmap.gov'||host==='earth-search.aws.element84.com'||host==='gibs.earthdata.nasa.gov';
 const goes=/^noaa-goes(16|17|18|19)\.s3\.amazonaws\.com$/.test(host);
 const spacing=goes?180:(federalImagery||forestProvider)?1000:host==='aviationweather.gov'?60000:host==='mesonet.agron.iastate.edu'?61000:(host==='photon.komoot.io'||host==='api.panoramax.xyz')?1000:0;
 const stamp=path.join(state,host+'.last');
 for(let attempt=0;attempt<250;attempt++) {
  for(let i=0;i<cap;i++) {
   const slot=path.join(state,host+'.slot-'+i);
   try{fs.mkdirSync(slot)}catch(e){
    if(e.code!=='EEXIST')throw e;
    if(age(slot)>60000)fs.rmSync(slot,{recursive:true,force:true});
    continue;
   }
   const release=()=>fs.rmSync(slot,{recursive:true,force:true});
   try{
    if(spacing && age(stamp)<spacing){
     if(goes||federalImagery||forestProvider)await delay(spacing-age(stamp));
     else throw Error('Provider minimum request interval');
    }
    if(spacing)atomic(stamp,'1');
   }catch(e){release();throw e}
   return release;
  }
  await delay(40);
 }
 throw Error('Provider concurrency limit');
}
export function cacheReader(directory, onRequest, fetcher=fetch) {
 const state=path.join(directory,'.source-state');
 fs.mkdirSync(state,{recursive:true});
 return async function data(id,url,ttl,range=null,timeout=9000,byteLimit=null,options={}) {
  const file=path.join(directory,id);
  const lock=path.join(state,id+'.lock');
  const host=new URL(url).hostname;
  const cooldown=path.join(state,host+'.until'),failures=path.join(state,host+'.fails');
  // A failure of the host itself (not a missing tile or a range it lacks) counts toward its pause.
  const hostFailed=(retryUntil=0)=>{
   const n=(Number(read(failures)?.toString())||0)+1;
   try{atomic(failures,String(n))}catch{}
   const until=Math.max(Date.now()+backoffMs(n),retryUntil);
   try{atomic(cooldown,String(Math.max(until,Number(read(cooldown)?.toString())||0)))}catch{}
  };
  const fresh=()=>age(file)<ttl ? read(file) : undefined;
  let bytes=fresh();
  if(bytes) return bytes;
  for(let waits=0;;waits++) {
   try { fs.mkdirSync(lock); break; }
   catch(error) {
    if(error.code!=='EEXIST') throw error;
    bytes=read(file); if(bytes) return bytes;
    if(age(lock)>minute) { fs.rmSync(lock,{force:true,recursive:true}); continue; }
    if(waits>=100) throw Error('Map download already pending');
    await delay(100);
   }
  }
  let releaseSlot=()=>{};
  try {
   bytes=fresh(); if(bytes) return bytes;
   if(Number(read(cooldown)?.toString())>Date.now()) throw Error('Provider requested a pause');
   releaseSlot=await acquireNetworkSlot(state,host);
   if(Number(read(cooldown)?.toString())>Date.now())throw Error('Provider requested a pause');
   onRequest();
   const headers={'User-Agent':userAgent};
   if(range)headers.Range='bytes='+range[0]+'-'+range[1];
   if(options.ifMatch)headers['If-Match']=options.ifMatch;
   let response;
   try{response=await fetcher(url,{signal:AbortSignal.timeout(timeout),headers})}
   catch(error){
    // The network or the host (refused, reset, timed out), not a caller's own check on the reply.
    if(error?.name==='TimeoutError'||error?.name==='AbortError'||error?.name==='TypeError'||typeof error?.code==='string')hostFailed();
    throw error;
   }
   if(response.status===429||response.status===503) {
    const retry=response.headers.get('retry-after');
    const date=retry===null?NaN:(/^\d+$/.test(retry)?Date.now()+Number(retry)*1000:Date.parse(retry));
    hostFailed(Number.isFinite(date)?Math.max(Date.now()+1000,date):Date.now()+minute);
   }
   else if(response.status===403||response.status>=500)hostFailed();
   else if(response.ok&&read(failures))fs.rmSync(failures,{force:true});
   if(!response.ok) throw Error('Map service returned '+response.status);
   if(options.ifMatch && response.headers.get('etag')!==options.ifMatch){
    await response.body?.cancel();throw Error('Geographic product changed');
   }
   if(range && (response.status!==206 || !response.headers.get('content-range')?.startsWith('bytes '+range[0]+'-'+range[1]+'/'))) {
    await response.body?.cancel();throw Error('Requested geographic range unavailable');
   }
   if (byteLimit == null) {
    bytes=Buffer.from(await response.arrayBuffer());
   } else {
    if (Number(response.headers.get('content-length'))>byteLimit) {
     await response.body?.cancel();throw Error('Map response exceeds byte budget');
    }
    const chunks=[];let total=0;
    for await (const chunk of response.body) {
     total+=chunk.byteLength;
     if (total>byteLimit) throw Error('Map response exceeds byte budget');
     chunks.push(chunk);
    }
    bytes=Buffer.concat(chunks,total);
   }
   if(range && bytes.length!==range[1]-range[0]+1)throw Error('Truncated geographic range');
   if(!bytes.length) throw Error('Empty map response');
   if(options.includeETag){
    const tag=response.headers.get('etag');
    if(!tag||tag.length>128||/[\r\n]/.test(tag))throw Error('Missing geographic product identity');
    bytes=Buffer.concat([Buffer.from(tag+'\n'),bytes]);
   }
   atomic(file,bytes);return bytes;
  } catch(error) {
   bytes=read(file);if(bytes) return bytes;
   throw error;
  } finally { releaseSlot();fs.rmSync(lock,{force:true,recursive:true}); }
 };
}
