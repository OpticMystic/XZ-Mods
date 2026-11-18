// Drive the real Linux C wire fixture through the native Windows session and OBS socket.
// Start test_screen_server_wire with a port receipt, then the ignored browser_fixture
// with XZ_SCREEN_FIXTURE_COLOR_BITS=8. This test never contacts a controller.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
const receipt=process.argv[2];
assert(receipt,'Pass the native browser_fixture session receipt');
const {url}=JSON.parse(await readFile(receipt,'utf8'));
const address=new URL('frames',url);address.protocol='ws:';
const socket=new WebSocket(address);socket.binaryType='arraybuffer';
const queued=[];let waiting;
socket.onmessage=event=>{if(typeof event.data==='string')return;waiting?(waiting(event.data),waiting=null):queued.push(event.data);};
await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
async function next(){
 if(queued.length)return queued.shift();
 return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{waiting=null;reject(Error('No OBS frame'));},5000);waiting=data=>{clearTimeout(timer);resolve(data);};});
}
const colors=[0xf800,0x07e0,0x001f,0xffff,0xffe0,0x07ff,0xf81f,0];
function quantize(pixel){
 const r=pixel>>>13,g=(pixel>>>8)&7,b=(pixel>>>3)&3;
 return ((r*4+(r>>>1))<<11)|((g*8+g)<<5)|(b*8+b*2+(b>>>1));
}
let checked=0;
const framesToCheck=90;
try{
 for(let n=0;n<framesToCheck;n++){
  const data=await next(),header=new DataView(data);
  assert.equal(data.byteLength,768016);assert.equal(header.getUint32(0,true),0x56505a58);
  assert.equal(header.getUint16(8,true),800);assert.equal(header.getUint16(10,true),480);assert.equal(header.getUint32(12,true),3);
  const sequence=header.getUint32(4,true),tick=sequence+1,pixels=new Uint16Array(data,16);
  for(let y=0;y<480;y++)for(let x=0;x<800;x++){
   const expected=y>220&&y<260?quantize(tick&65535):colors[Math.floor((x+tick*7)/100)%8];
   if(pixels[y*800+x]!==expected)assert.equal(pixels[y*800+x],expected,`Frame ${sequence}, pixel ${x},${y}`);checked++;
  }
  const ack=new ArrayBuffer(4);new DataView(ack).setUint32(0,sequence,true);socket.send(ack);
 }
 const status=await(await fetch(new URL('status.json',url))).json();delete status.diagnostics;
 assert.equal(status.phase,'live');assert.equal(status.protocol,3);assert.equal(status.source_color_bits,8);
 const result={host_only:true,physical_controller:false,frames_checked:framesToCheck,pixels_checked:checked,status};
 await writeFile(receipt.replace(/\.json$/,'.verified.json'),JSON.stringify(result,null,2)+'\n');
 console.log(JSON.stringify(result,null,2));
}finally{socket.close();}
