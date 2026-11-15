// One way in for the map data that is not fetched per tile from a community server
// (detail-staging/server-load.md, v2.0.4): get(source, z, x, y) returns the bytes of one tile of
// one source, from a data pack when there is one, else from a capped live fetch through the
// shared cache reader (atlas-cache.mjs: its cache, cooldowns and per-host caps).
//
// A pack is built once per release from bulk public files (OSM extract, LANDFIRE, NSI, Overture),
// hosted as plain files: <pack>/<source>/<z>/<x>/<y>.<ext>, on this computer (a folder) or on a
// bucket (a base URL, read with the same cache). SIMCRUISE_DATA_PACK names it; none: live only.
//
// Live fetches of a source are capped per day on this computer (its .source-state day counter),
// so one player can never put more than the cap on a public service.
import fs from 'node:fs';
import path from 'node:path';
import {cacheReader} from './atlas-cache.mjs';

const day=86400000;

/** Each source: its file extension in a pack, its live URL (or none: pack only), its cache life and daily cap. */
export const sources={
 'evt':{ext:'tif',ttl:365*day,cap:400,live:(z,x,y)=>{
  // LANDFIRE 2022 EVT codes for one z14 tile, 80 x 80 px nearest neighbour (about 30 m).
  const lon=v=>v/2**z*360-180,lat=v=>180/Math.PI*Math.atan(Math.sinh(Math.PI*(1-2*v/2**z)));
  const bbox=[lon(x),lat(y+1),lon(x+1),lat(y)].map(v=>v.toFixed(6)).join(',');
  return 'https://edcintl.cr.usgs.gov/arcgis/rest/services/Landfire_LF2022/LF2022_EVT_CONUS/ImageServer/exportImage?bbox='+bbox+
   '&bboxSR=4326&imageSR=4326&size=80,80&format=tiff&pixelType=S16&interpolation=RSP_NearestNeighbor&f=image';
 }},
 'nsi':{ext:'json',ttl:30*day,cap:200,live:null},          // pack only (from the public per-state files)
 'civil':{ext:'json',ttl:90*day,cap:0,live:null},          // OSM extract: roundabouts, cuttings, crossings ... (no Overpass)
 'places':{ext:'json',ttl:90*day,cap:0,live:null},         // OSM + Wikidata dump: building facts, furniture, roadside
};

function dayCount(state,source,bump){
 const file=path.join(state,'live-'+source+'.day');
 const today=Math.floor(Date.now()/day);
 let [d,n]=(()=>{try{return fs.readFileSync(file,'utf8').split(' ').map(Number)}catch{return [today,0]}})();
 if(d!==today){d=today;n=0}
 if(bump){n++;try{fs.writeFileSync(file,d+' '+n)}catch{}}
 return n;
}

/**
 * The data access for one cache folder. pack: a folder or base URL (default SIMCRUISE_DATA_PACK);
 * fetcher for the tests. get() resolves to {bytes, from: 'pack' | 'live'} or rejects
 * ('not in the pack', 'live cap reached', or the fetch's own error, with the stale copy kept by the reader).
 */
export function dataAccess(cache,{pack=process.env.SIMCRUISE_DATA_PACK||'',fetcher=fetch,onRequest=()=>{}}={}){
 const read=cacheReader(cache,onRequest,fetcher);
 const state=path.join(cache,'.source-state');
 return {
  async get(source,z,x,y){
   const s=sources[source];
   if(!s)throw Error('Unknown data source '+source);
   if(![z,x,y].every(Number.isInteger))throw Error('Invalid tile');
   const rel=source+'/'+z+'/'+x+'/'+y+'.'+s.ext;
   if(pack){
    if(/^https?:\/\//.test(pack)){
     try{return {bytes:await read('pack-'+rel.replaceAll('/','-'),pack.replace(/\/$/,'')+'/'+rel,s.ttl),from:'pack'}}
     catch(error){if(!s.live)throw error}
    }else{
     try{return {bytes:fs.readFileSync(path.join(pack,rel)),from:'pack'}}
     catch(error){if(error.code!=='ENOENT')throw error;if(!s.live)throw Error('Not in the pack: '+rel)}
    }
   }
   if(!s.live)throw Error('Not in the pack: '+rel);
   const id='live-'+rel.replaceAll('/','-');
   // A cached copy within its life costs nothing; only a real request counts against the day's cap.
   let fresh=null;
   try{const st=fs.statSync(path.join(cache,id));if(Date.now()-st.mtimeMs<s.ttl)fresh=fs.readFileSync(path.join(cache,id))}catch{}
   if(fresh)return {bytes:fresh,from:'live'};
   if(dayCount(state,source,false)>=s.cap)throw Error('Live cap reached for '+source);
   dayCount(state,source,true);
   return {bytes:await read(id,s.live(z,x,y),s.ttl),from:'live'};
  }
 };
}
