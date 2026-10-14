// What a status reason means to the person at the screen. Pure, so it can be
// checked without a window. reason comes from live_stream_status: 'loader',
// 'network', 'protocol', 'stream', or '' while live.
const LIVE_REASONS={
  loader:{title:'Update the USB loader',text:'The XZ is on the network, but its USB loader has no screen service. Update the loader in Set up USB, then power cycle the XZ with both decks stopped.',fix:true},
  network:{title:'Cannot reach the XZ',text:'Check that this computer and the XZ are on the same network and that the address is right. XZ Mods keeps trying.'},
  protocol:{title:'The XZ answered unexpectedly',text:'Its USB loader may be from another XZ Mods version. Update the loader in Set up USB if this continues.',fix:true},
  stream:{title:'Screen stream interrupted',text:'Reconnecting automatically…'},
};
function liveView(result){
  if(!result||result.phase==='stopped')return {live:false,title:'No XZ screen yet',text:'The XZ screen appears here, 800 × 480, once it is connected.',detail:'',fix:false};
  if(result.phase==='live')return {live:true,title:'',text:`Live preview · 800 × 480 · about 1 frame per second · ${result.capture_ms} ms capture · ${result.frames} frames`,detail:'',fix:false};
  const known=LIVE_REASONS[result.reason];
  if(known)return {live:false,title:known.title,text:known.text,detail:result.error||'',fix:!!known.fix,reason:result.reason};
  return {live:false,title:'Connecting',text:'Connecting to the XZ screen…',detail:result.error||'',fix:false};
}
if(typeof module!=='undefined')module.exports={liveView,LIVE_REASONS};
else (() => {
  const screen=$('live-screen'),status=$('live-status'),wizard=$('live-wizard'),host=$('live-host');
  let running=false,url='',generation=0,busy=false,detecting=false,loaderFocused=false;
  function controls(){ $('live-stop').disabled=!running;$('live-copy').disabled=!url;$('live-fullscreen').disabled=!running; }
  // Never pull focus out of a field the person is typing in.
  function typing(){const active=document.activeElement;return !!active&&(active.tagName==='INPUT'||active.tagName==='TEXTAREA'||active.isContentEditable);}
  function show(view){
    // The placeholder explains; the status line only names the state, so nothing repeats.
    status.textContent=view.live?view.text:view.title||view.text;
    $('live-detail').textContent=view.detail;$('live-detail').classList.toggle('hidden',!view.detail||view.detail===view.text);
    // The placeholder covers the frame until a fresh picture arrives, so a stale screen never shows.
    $('live-placeholder').classList.toggle('hidden',view.live);
    $('live-placeholder-title').textContent=view.title||'Connecting';$('live-placeholder-text').textContent=view.text;
    // One primary action: while a fix is needed it outranks setup.
    $('live-loader-fix').classList.toggle('hidden',!view.fix);
    $('live-setup').classList.toggle('primary',!view.fix);$('live-setup').classList.toggle('secondary',!!view.fix);
    if(wizard.open)$('live-wizard-status').textContent=view.live?'Connected.':`${view.title&&view.title!=='Connecting'?view.title+'. ':''}${view.text}`;
    // A loader problem focuses its one fix, once per connection attempt.
    if(view.reason==='loader'&&!loaderFocused&&!typing()){
      loaderFocused=true;(wizard.open?$('live-loader-setup'):$('live-loader-fix')).focus();
    }
  }
  async function detect(){
    if(detecting)return;detecting=true;$('live-detect').disabled=true;$('live-wizard-status').textContent='Looking for your XZ on this network…';
    try {
      const hosts=await invoke('live_stream_detect');$('live-hosts').replaceChildren(...hosts.map(found=>{const option=document.createElement('option');option.value=found;return option;}));
      if(hosts.length===1&&!host.value.trim())host.value=hosts[0];
      $('live-wizard-status').textContent=hosts.length===1?'Found your XZ. Press Connect.':hosts.length?`Found ${hosts.length} XZs. Choose one, then press Connect.`:'No XZ answered. Put this computer on the same network as the XZ and look again, or type the address shown on the XZ.';
      if(hosts.length===1&&!typing())$('live-connect').focus();
    } catch(error){$('live-wizard-status').textContent=String(error);}
    finally{detecting=false;$('live-detect').disabled=false;}
  }
  $('live-setup').onclick=()=>{
    wizard.showModal();
    if(host.value.trim()){$('live-wizard-status').textContent='Press Connect, or look again for another XZ.';$('live-connect').focus();}
    else{host.focus();detect();}
  };
  $('live-wizard-close').onclick=()=>wizard.close();
  $('live-detect').onclick=detect;
  const openLoader=()=>{if(wizard.open)wizard.close();showPage('setup');};
  $('live-loader-setup').onclick=openLoader;$('live-loader-fix').onclick=openLoader;
  $('live-connect').onclick=async()=>{
    if(busy)return;
    if(!host.value.trim()){$('live-wizard-status').textContent='Choose or type the XZ address first.';host.focus();return;}
    busy=true;$('live-connect').disabled=true;loaderFocused=false;
    try {
      if(running)await invoke('live_stream_stop');
      running=false;generation++;url='';screen.removeAttribute('src');$('live-url').value='';controls();show(liveView(null));
      const result=await invoke('live_stream_start',{host:host.value.trim()});
      running=true;url=result.url;$('live-url').value=url;controls();
      show({live:false,title:'Connecting',text:'Connecting to the XZ screen… This closes when the first picture arrives.',detail:'',fix:false});
      poll(generation);
    } catch(error){$('live-wizard-status').textContent=String(error);status.textContent=String(error);}
    finally{busy=false;$('live-connect').disabled=false;}
  };
  $('live-stop').onclick=async()=>{
    try{await invoke('live_stream_stop');running=false;generation++;url='';screen.removeAttribute('src');$('live-url').value='';controls();show(liveView(null));status.textContent='Screen stream stopped.';}
    catch(error){status.textContent=String(error);}
  };
  $('live-copy').onclick=async()=>{
    try{await navigator.clipboard.writeText(url);$('live-copy-status').textContent='Copied. In OBS, add a Browser source, paste this URL and set 800 × 480.';}
    catch{$('live-copy-status').textContent='Select and copy the URL above. In OBS, add a Browser source at 800 × 480.';$('live-url').select();}
  };
  $('live-fullscreen').onclick=()=>screen.requestFullscreen().catch(error=>{status.textContent=String(error);});
  async function poll(token){
    if(!running||token!==generation)return;
    try {
      const result=await invoke('live_stream_status');if(token!==generation)return;
      const view=liveView(result);
      if(view.live){if(!screen.getAttribute('src'))screen.src=url;if(wizard.open)wizard.close();loaderFocused=false;}
      show(view);
    }catch(error){status.textContent=String(error);}
    if(running&&token===generation)setTimeout(()=>poll(token),1000);
  }
  controls();
})();
