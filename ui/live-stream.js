// What a status reason means to the person at the screen. Pure, so it can be
// checked without a window. reason comes from live_stream_status: 'loader',
// 'network', 'protocol', 'stream', or '' while live.
const LIVE_REASONS={
  loader:{title:'Update the USB loader',text:'The XZ is on the network, but its USB loader has no screen service. Update the loader in Set up USB, then power cycle the XZ with both decks stopped.',fix:true},
  network:{title:'Cannot reach the XZ',text:'Check that this computer and the XZ are on the same network and that the address is right. XZ Mods keeps trying.'},
  protocol:{title:'The XZ answered unexpectedly',text:'Its USB loader may be from another XZ Mods version. Update the loader in Set up USB if this continues.',fix:true},
  stream:{title:'Screen stream interrupted',text:'Reconnecting automatically…'},
};
// The rate the picture actually arrives at. fps is the backend's rolling count
// of real captures and stays null until it has measured one; target_fps only
// says what the loader aims for, so it never stands in for a measurement.
function liveRate(result){
  const fps=result&&typeof result.fps==='number'&&isFinite(result.fps)?result.fps:null;
  const legacy=!result||![2,3].includes(result.protocol);
  const measured=fps===null?'':fps<1?'under 1 fps':`${fps<10?fps.toFixed(1).replace(/\.0$/,''):Math.round(fps)} fps`;
  if(legacy){
    // Loader v1 (or a backend without the field) sends about one picture a second.
    const rate=measured||'about 1 fps';
    return {badge:XZTemplate`Preview · ${rate} · not performance video`,
      note:XZText('This USB loader sends about one picture per second: fine for watching the screen, not smooth enough for a performance stream.')+(result&&result.protocol===1?XZText(' Update the USB loader for a smoother picture.'):''),
      text:rate};
  }
  if(fps===null)return {badge:XZText('Measuring frame rate…'),note:XZText('XZ Mods shows the frame rate it measures on this connection.'),text:XZText('measuring frame rate')};
  const short=typeof result.target_fps==='number'&&fps<result.target_fps*0.9;
  return {badge:XZTemplate`Live · ${measured}`,
    note:short?XZTemplate`The XZ is sending ${measured} on this connection. A busy network or computer lowers the rate; fast waveform motion may look uneven.`:XZText('Measured on this connection. A busy network or computer can lower the rate.'),
    text:measured};
}
const IDLE_NOTE='The frame rate depends on the USB loader and the network. XZ Mods shows the rate it measures once connected.';
function liveView(result){
  if(!result||result.phase==='stopped')return {live:false,title:XZText('No XZ screen yet'),text:XZText('The XZ screen appears here, 800 × 480, once it is connected.'),detail:'',fix:false,badge:'',note:XZText(IDLE_NOTE)};
  if(result.phase==='live'){
    const rate=liveRate(result);
    // An old loader still shows its picture; updating it is the one action, never a block.
    const legacy=result.protocol===1;
    const needs8=result.requested_color_bits===8&&result.protocol!==3;
    const mode=result.protocol===3?'Streaming 8-bit · 256 colors · full 800 × 480.':needs8?'8-bit needs an updated USB loader. Showing the working 16-bit stream.':'Streaming 16-bit · original colors · full 800 × 480.';
    return {live:true,title:'',text:XZTemplate`Live preview · 800 × 480 · ${rate.text} · ${result.capture_ms} ms capture · ${result.frames} frames`,detail:'',fix:legacy||needs8,reason:legacy?'legacy':needs8?'color-depth':'',mode,badge:rate.badge,note:rate.note};
  }
  const known=LIVE_REASONS[result.reason];
  if(known)return {live:false,title:XZText(known.title),text:XZText(known.text),detail:result.error||'',fix:!!known.fix,reason:result.reason,badge:'',note:XZText(IDLE_NOTE)};
  return {live:false,title:XZText('Connecting'),text:XZText('Connecting to the XZ screen…'),detail:result.error||'',fix:false,badge:'',note:XZText(IDLE_NOTE)};
}
if(typeof module!=='undefined')module.exports={liveView,liveRate,LIVE_REASONS};
else (() => {
  const screen=$('live-screen'),status=$('live-status'),wizard=$('live-wizard'),host=$('live-host');
  const color=$('live-color-bits');
  try {const saved=localStorage.getItem('xz-stream-color-bits');if(saved==='8'||saved==='16')color.value=saved;}catch{}
  color.onchange=()=>{try{localStorage.setItem('xz-stream-color-bits',color.value);}catch{}};
  let running=false,url='',generation=0,busy=false,detecting=false,loaderFocused=false;
  function controls(){ $('live-color-bits').disabled=running||busy;$('live-stop').disabled=!running;$('live-copy').disabled=!url;$('live-fullscreen').disabled=!running; }
  // Never pull focus out of a field the person is typing in.
  function typing(){const active=document.activeElement;return !!active&&(active.tagName==='INPUT'||active.tagName==='TEXTAREA'||active.isContentEditable);}
  function show(view){
    // The placeholder explains; the status line only names the state, so nothing repeats.
    status.textContent=view.live?view.text:view.title||view.text;
    $('live-color-status').textContent=view.mode||XZText('Full 800 × 480 in either mode. Color depth changes when you reconnect.');
    $('live-detail').textContent=view.detail;$('live-detail').classList.toggle('hidden',!view.detail||view.detail===view.text);
    // The placeholder covers the frame until a fresh picture arrives, so a stale screen never shows.
    $('live-placeholder').classList.toggle('hidden',view.live);
    $('live-placeholder-title').textContent=view.title||XZText('Connecting');$('live-placeholder-text').textContent=view.text;
    // The badge and note report the measured rate, so they hide until a picture is live.
    $('live-rate').textContent=view.badge;$('live-rate').classList.toggle('hidden',!view.badge);$('live-rate-note').textContent=view.note;
    // One primary action: while a fix is needed it outranks setup.
    $('live-loader-fix').classList.toggle('hidden',!view.fix);
    $('live-setup').classList.toggle('primary',!view.fix);$('live-setup').classList.toggle('secondary',!!view.fix);
    if(wizard.open)$('live-wizard-status').textContent=view.live?'Connected.':XZTemplate`${view.title&&view.title!=='Connecting'?view.title+'. ':''}${view.text}`;
    // A loader problem focuses its one fix, once per connection attempt.
    if(view.reason==='loader'&&!loaderFocused&&!typing()){
      loaderFocused=true;(wizard.open?$('live-loader-setup'):$('live-loader-fix')).focus();
    }
  }
  async function detect(){
    if(detecting)return;detecting=true;$('live-detect').disabled=true;$('live-wizard-status').textContent=XZText('Looking for your XZ on this network…');
    try {
      const hosts=await invoke('live_stream_detect');$('live-hosts').replaceChildren(...hosts.map(found=>{const option=document.createElement('option');option.value=found;return option;}));
      if(hosts.length===1&&!host.value.trim())host.value=hosts[0];
      $('live-wizard-status').textContent=hosts.length===1?XZText('Found your XZ. Press Connect.'):hosts.length?XZTemplate`Found ${hosts.length} XZs. Choose one, then press Connect.`:XZText('No XZ answered. Put this computer on the same network as the XZ and look again, or type the address shown on the XZ.');
      if(hosts.length===1&&!typing())$('live-connect').focus();
    } catch(error){$('live-wizard-status').textContent=String(error);}
    finally{detecting=false;$('live-detect').disabled=false;}
  }
  $('live-setup').onclick=()=>{
    wizard.showModal();
    if(host.value.trim()){$('live-wizard-status').textContent=XZText('Press Connect, or look again for another XZ.');$('live-connect').focus();}
    else{host.focus();detect();}
  };
  $('live-wizard-close').onclick=()=>wizard.close();
  $('live-detect').onclick=detect;
  const openLoader=()=>{if(wizard.open)wizard.close();showPage('setup');};
  $('live-loader-setup').onclick=openLoader;$('live-loader-fix').onclick=openLoader;
  $('live-connect').onclick=async()=>{
    if(busy)return;
    if(!host.value.trim()){$('live-wizard-status').textContent=XZText('Choose or type the XZ address first.');host.focus();return;}
    busy=true;$('live-connect').disabled=true;loaderFocused=false;
    try {
      if(running)await invoke('live_stream_stop');
      running=false;generation++;url='';screen.removeAttribute('src');$('live-url').value='';controls();show(liveView(null));
      const result=await invoke('live_stream_start',{host:host.value.trim(),colorBits:Number($('live-color-bits').value)});
      running=true;url=result.url;$('live-url').value=url;controls();
      show({live:false,title:XZText('Connecting'),text:XZText('Connecting to the XZ screen… This closes when the first picture arrives.'),detail:'',fix:false,badge:'',note:XZText(IDLE_NOTE)});
      poll(generation);
    } catch(error){$('live-wizard-status').textContent=String(error);status.textContent=String(error);}
    finally{busy=false;$('live-connect').disabled=false;controls();}
  };
  $('live-stop').onclick=async()=>{
    try{await invoke('live_stream_stop');running=false;generation++;url='';screen.removeAttribute('src');$('live-url').value='';controls();show(liveView(null));status.textContent=XZText('Screen stream stopped.');}
    catch(error){status.textContent=String(error);}
  };
  $('live-copy').onclick=async()=>{
    try{await navigator.clipboard.writeText(url);$('live-copy-status').textContent=XZText('Copied. In OBS, add a Browser source, paste this URL and set 800 × 480.');}
    catch{$('live-copy-status').textContent=XZText('Select and copy the URL above. In OBS, add a Browser source at 800 × 480.');$('live-url').select();}
  };
  $('live-fullscreen').onclick=()=>screen.requestFullscreen().catch(error=>{status.textContent=String(error);});
  async function poll(token){
    if(!running||token!==generation)return;
    try {
      const result=await invoke('live_stream_status');if(token!==generation)return;
      const view=liveView(result);
      if(view.live){if(!screen.getAttribute('src'))screen.src=url;if(wizard.open)wizard.close();loaderFocused=false;}
      if($('live-privacy').checked)$('live-privacy-status').textContent=result.privacy_covered?XZText('Logo covering the stream.'):view.live?XZText('Performance screen visible.'):XZText('Waiting for the XZ screen.');
      show(view);
    }catch(error){status.textContent=String(error);}
    if(running&&token===generation)setTimeout(()=>poll(token),1000);
  }
  const privacy=$('live-privacy'),privacyStatus=$('live-privacy-status');
  let savedPrivacy=false;
  privacy.onchange=async()=>{
    const enabled=privacy.checked;privacy.disabled=true;
    try {savedPrivacy=await invoke('live_stream_privacy',{enabled});privacy.checked=savedPrivacy;privacyStatus.textContent=savedPrivacy?XZText('Browsing cover on.'):XZText('Browsing cover off.');}
    catch(error){privacy.checked=savedPrivacy;privacyStatus.textContent=String(error);}
    finally{privacy.disabled=false;}
  };
  if(invoke)invoke('live_stream_privacy',{}).then(enabled=>{savedPrivacy=enabled;privacy.checked=enabled;privacy.disabled=false;}).catch(error=>{privacyStatus.textContent=String(error);privacy.disabled=false;});
  controls();
})();
