const $=id=>document.getElementById(id);
const invoke=window.__TAURI__?.core?.invoke;
let activeJob=null,appUpdating=false,updateChecking=false,otaReady=false;
let stemLibraryStarted=false,stemLibraryStarting=false,stemLibrary=null,stemLibraryLoading=false;
// Work that found another job running waits here and runs once the current job ends.
const idleQueue=[];
// One waiter is released per idle moment: the first to start a job takes the slot and the rest wait for the next one.
function drainIdle(){
  if(!idleQueue.length||activeJob!==null||appUpdating)return;
  idleQueue.shift()();
  setTimeout(drainIdle);
}
function idle(){return activeJob===null&&!appUpdating&&!idleQueue.length?Promise.resolve():new Promise(resolve=>{idleQueue.push(resolve);setTimeout(drainIdle);});}
// A quiet background look for USBs must never cost a click: run() waits for it instead of refusing.
const yieldingMethods=['discover_usbs'];
let activeYields=false,cancelPending=false;
const batchLimit=100,batchFormat='xz-stem-batch/1';
// Selection is keyed by the authoritative PDB track ID and scoped to one USB identity (stemSelectionKey).
let stemSelection=new Map(),stemSelectionKey=null,stemBatch=null;
let stemPhase='idle',stemBusy=false,stemDrives=[],stemVolume=null,stemFilter='all',stemQuery='',stemSearchTimer=null,stemConfirm=null,stemEngine=null;
const stemOpen=new Set(),stemChecks=new Map(),stemFresh=new Set();
let stemRowsById=new Map(),eng=null,imp=null;
let usbInspection=null;
// Library repair: the inspection is the only basis for a repair or undo, and it is dropped whenever the chosen USB changes.
let repairInspection=null,repairDiscovery=null;
// Mod version: modInventory is the last list the backend returned and holds the saved choice. modDraft is the
// unsaved dropdown and checkbox state. USB writes read only the saved choice, through selectedVersionRequest().
let modInventory=null,modDraft=null,modLoadError='',modImportFolder='',modImportedId=null;
const modChannels={preview:'Preview',public:'Public release',release:'Public release',released:'Released',development:'Development build',dev:'Development build',imported:'Imported'};
const titles={setup:'USB',stems:'Stems','live-stream':'XZ Screen',tools:'Tools','dj-profile':'DJ Profile','library-repair':'Repair library','device-updates':'Network updates',advanced:'XZ Mods version',updates:'App updates',licenses:'About'};
// Pages opened from the Tools list. They keep Tools selected in the sidebar and show a back link.
const toolPages=new Set(['dj-profile','library-repair','device-updates','advanced','updates','licenses']);
// The single job panel moves next to the control that started the work. feedbackHome is that control;
// feedbackLive is true while a visible job runs. Library reads report in the library and leave the panel alone.
const jobPanel=$('job-panel'),quietMethods=['discover_usbs','discover_stem_usbs','browse_stem_library','plan_engine_setup','list_mod_versions','inspect_mod_settings'];
const feedbackGroups='p,label,.actions,.path-row,.st-foot,.section-heading,.columns';
let feedbackHome=null,feedbackLive=false;
const pageShown={setup:()=>window.setupWizard?.shown(),stems:()=>startStemLibrary(),'dj-profile':()=>window.djProfilePage?.show(),
  'library-repair':()=>{if(!repairDiscovery)repairDiscovery=discoverRepairDrives();},'device-updates':()=>$('ota-detect').click()};
function showPage(name){
  document.querySelectorAll('main>.page').forEach(page=>page.classList.toggle('hidden',page.id!==name));
  const section=toolPages.has(name)?'tools':name;
  document.querySelectorAll('nav button').forEach(item=>item.classList.toggle('selected',item.dataset.page===section));
  $('page-title').textContent=titles[name];
  $('back-to-tools').classList.toggle('hidden',!toolPages.has(name));
  if(!feedbackLive)jobPanel.classList.add('hidden');else{positionFeedback();revealFeedback();}
  pageShown[name]?.();
}
for(const button of document.querySelectorAll('nav button'))button.onclick=()=>showPage(button.dataset.page);
for(const row of document.querySelectorAll('.tool-row'))row.onclick=()=>showPage(row.dataset.open);
$('back-to-tools').onclick=()=>showPage('tools');
function required(ids,origin){for(const id of ids)if(!$(id).value.trim()){showError('Enter or choose the required '+({'volume':'USB folder',firmware:'firmware input',key:'boot key'}[id]||id)+' first.',origin);return false;}return true;}
function movePanel(insert){
  const focused=jobPanel.contains(document.activeElement)?document.activeElement:null;
  insert();
  document.querySelectorAll('tr.job-row').forEach(row=>{if(!row.contains(jobPanel))row.remove();});
  if(!jobPanel.classList.contains('hidden'))for(let details=jobPanel.parentElement?.closest('details');details;details=details.parentElement?.closest('details'))details.open=true;
  if(focused?.isConnected&&document.activeElement!==focused)focused.focus({preventScroll:true});
}
function homeShown(home){
  if(!home?.isConnected)return false;
  const dialog=home.closest('dialog');if(dialog)return dialog.open;
  return !home.closest('.page')?.classList.contains('hidden');
}
// Beside feedbackHome when its page is showing; otherwise at the top of the current page.
function positionFeedback(){
  if(homeShown(feedbackHome)){
    const home=feedbackHome,row=home.closest('#stem-tracks tr');
    if(row)return movePanel(()=>{
      let holder=row.nextElementSibling;
      if(!holder?.classList.contains('job-row')){holder=node('tr',undefined,'job-row');const cell=node('td');cell.colSpan=[...row.cells].reduce((span,item)=>span+item.colSpan,0);holder.append(cell);row.after(holder);}
      holder.firstElementChild.append(jobPanel);
    });
    let anchor=home,group;while((group=anchor.parentElement?.closest(feedbackGroups)))anchor=group;
    return movePanel(()=>anchor.after(jobPanel));
  }
  const page=document.querySelector('main>.page:not(.hidden)');if(!page)return;
  const lead=page.querySelector(':scope>.intro');
  movePanel(()=>lead?lead.after(jobPanel):page.prepend(jobPanel));
}
function revealFeedback(){if(!jobPanel.classList.contains('hidden')&&jobPanel.isConnected)jobPanel.scrollIntoView({block:'nearest'});}
function showFeedback(origin){feedbackHome=origin||null;jobPanel.classList.remove('hidden');positionFeedback();}
function showError(message,origin){jobPanel.classList.remove('is-cancelled');jobPanel.classList.add('is-error');$('job-title').textContent='Operation not completed';$('job-message').textContent=message;$('job-summary').textContent='';$('job-details').classList.add('hidden');$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');showFeedback(origin);revealFeedback();}
function busy(value){value=value||appUpdating;stemBusy=value;document.querySelectorAll('.pick,#build,#verify-inputs,#download-firmware,#inspect-usb,#update-usb,#refresh-usb,#edit-selected-usb,.restore-loader,#setup-games-usb,#ota-detect,#ota-start,[id^=download-],[id^=copy-]').forEach(button=>button.disabled=value||!invoke);document.querySelectorAll('.usb-setting,#experimental').forEach(input=>input.disabled=value);$('ota-start').disabled=value||!invoke||!otaReady;$('install-app-update').disabled=value||activeJob!==null;renderStems();updateRepairControls(value);updateModVersionControls(value);}
function verifySummary(result){
  const check=stemsApi.verify(result);
  return [t('verify.mixes',{ok:check.mixes}),check.pages?ST['verify.pagesAll']:'',check.identity?ST['verify.identity']:''].filter(Boolean).join(' · ')+'. Your USB was not changed.';
}
// A watch callback runs the job without the shared job panel and receives every status update; the caller shows progress and the outcome.
async function run(request,title,origin,watch){
  if(!invoke){showError('Open XZ Mods Builder to run this operation. This browser view is a preview.',origin);return;}
  if(activeJob!==null&&activeYields&&!appUpdating)await idle();
  if(activeJob!==null||appUpdating)return;
  const quiet=Boolean(watch)||quietMethods.includes(request.method);let settled=null;
  if(!quiet){jobPanel.classList.remove('is-error','is-cancelled');$('job-title').textContent=title;$('job-message').textContent='Starting…';$('job-details').classList.add('hidden');$('job-details').open=false;$('job-result').textContent='';$('job-summary').textContent='';$('job-progress').removeAttribute('value');$('job-progress').classList.remove('hidden');$('cancel').classList.remove('hidden');$('cancel').disabled=false;feedbackLive=true;showFeedback(origin);revealFeedback();}
  activeYields=yieldingMethods.includes(request.method);
  // A yielding job leaves every button enabled: a click on one waits for it (above) instead.
  if(!activeYields)busy(true);
  // Claimed before the first await, so another run() started in the same tick finds the slot taken.
  activeJob='starting';cancelPending=false;
  try{
    activeJob=await invoke('start_job',{request});
    if(cancelPending)cancelActiveJob();
    while(true){
      const state=await invoke('job_status',{id:activeJob});
      if(quiet){watch?.(state);if(!state.running)return state;await new Promise(resolve=>setTimeout(resolve,350));continue;}
      $('job-message').textContent=state.message||state.error||(state.ok?'Completed':'Working…');
      if(!state.running){
        const cancelled=Boolean(state.cancelled||state.result?.cancelled===true);
        $('job-title').textContent=state.ok?'Completed':cancelled?'Cancelled':'Operation not completed';
        settled=state.ok?null:cancelled?'is-cancelled':'is-error';
        if(state.result){
          $('job-result').textContent=JSON.stringify(state.result,null,2);$('job-details').classList.remove('hidden');
          $('job-summary').textContent=state.result.format==='xz-mods-loader-updated/1'?'Loader updated. Previous loader: '+state.result.backup:state.result.format==='xz-mods-loader-restored/1'?'Previous loader restored.':state.result.format==='xz-mods-settings-saved/1'?'Settings saved to USB and checked.':state.result.image?'Verified image: '+state.result.image+(state.result.requires_copy_to_usb_root?' · Copy autoexec.bin to the root of a FAT/FAT32 USB.':''):
            state.result.format==='overcue-stems/4'?verifySummary(state.result):
            state.result.format==='overcue-index/1'?ST['imp.done.title']+'.':
            state.result.report_path?'Report saved: '+state.result.report_path:'';
          if(state.result.game_setup || state.result.format==='xz-games-usb/1'){
            const setup=state.result.game_setup||state.result;
            const names=setup.games.filter(game=>game.ok).map(game=>game.name).join(', ');
            $('job-summary').textContent=(state.result.image?'Loader ready. ':'')+(names?'Game files on USB: '+names+'. ':'')+(setup.failed_count?setup.failed_count+' download(s) failed. Retry the failed games on the USB page. ':'')+'My House still needs the native GZDoom port.';
          }else if(state.result.format==='xz-game-installed/1')$('job-summary').textContent=state.result.name+' copied to USB and verified.';
          else if(state.result.format==='xz-game-downloaded/1')$('job-summary').textContent=state.result.name+' downloaded and verified.';
          else if(state.result.format==='xz-library-inspected/1')$('job-summary').textContent='Check finished. The results are below.';
          else if(state.result.format==='xz-library-repaired/1'){$('job-title').textContent='Repair written · not yet tested in Rekordbox';$('job-summary').textContent=state.result.summary||'';}
          else if(state.result.format==='xz-library-restored/1'){$('job-title').textContent='Repair undone';$('job-summary').textContent=state.result.summary||'';}
          else if(state.result.format==='xz-library-report/1')$('job-summary').textContent='Report saved: '+state.result.report_path;
          if(state.result.mod_version?.id){const summary=$('job-summary').textContent.trim();$('job-summary').textContent=(summary&&!/[.!?]$/.test(summary)?summary+'.':summary)+(summary?' ':'')+'Mod version: '+modVersionText(state.result.mod_version)+'.';}
          const adjusted=state.result.settings_adjustment;
          if(adjusted?.backup){const summary=$('job-summary').textContent.trim();$('job-summary').textContent=(summary&&!/[.!?]$/.test(summary)?summary+'.':summary)+(summary?' ':'')+'USB settings adjusted for this version. Original settings backed up: '+adjusted.backup+'.';}
        }
        return state;
      }
      await new Promise(resolve=>setTimeout(resolve,350));
    }
  }catch(error){if(quiet)return {ok:false,running:false,error:String(error)};settled='is-error';showError(String(error),origin);}
  finally{
    activeJob=null;activeYields=false;busy(false);
    if(idleQueue.length)setTimeout(drainIdle);
    if(!quiet){feedbackLive=false;$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');if(settled){jobPanel.classList.add(settled);revealFeedback();}}
  }
}
for(const button of document.querySelectorAll('.pick'))button.onclick=async()=>{
  try{const path=await invoke('pick_path',{kind:button.dataset.kind});if(path)$(button.dataset.target).value=path;}
  catch(error){showError(String(error),button);}
};
async function cancelActiveJob(){
  if(activeJob===null)return;
  if(stemBatch?.running){stemBatch.cancelRequested=true;renderStems();}
  $('cancel').disabled=true;
  // The job has no id until start_job answers; run() sends the cancel as soon as it does.
  if(activeJob==='starting'){cancelPending=true;$('job-message').textContent='Cancelling safely…';return;}
  try{await invoke('cancel_job',{id:activeJob});$('job-message').textContent='Cancelling safely…';}
  catch(error){$('cancel').disabled=false;if(stemBatch?.running){stemBatch.cancelRequested=false;renderStems();}$('job-message').textContent='Could not cancel: '+String(error);}
}
$('cancel').onclick=cancelActiveJob;
const gameLabels={doom:'Doom shareware',chex:'Chex Quest',freedoom:'Freedoom Phase 2'};
for(const game of Object.keys(gameLabels)){
  const download=$('download-'+game),copy=$('copy-'+game+'-usb');
  download.onclick=()=>run({method:'download_game',game},'Downloading '+gameLabels[game],download);
  copy.onclick=async()=>{
    if(!invoke){showError('Open the installed XZ Mods app to copy games to USB.',copy);return;}
    try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await run({method:'install_game_usb',game,volume},'Downloading and copying '+gameLabels[game],copy);}
    catch(error){showError(String(error),copy);}
  };
}
$('setup-games-usb').onclick=async()=>{
  const origin=$('setup-games-usb');
  if(!invoke){showError('Open the installed XZ Mods app to set up games on USB.',origin);return;}
  try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await run({method:'prepare_games_usb',volume},'Setting up games on USB',origin);}
  catch(error){showError(String(error),origin);}
};
$('ota-detect').onclick=async()=>{
  const response=await run({method:'ota_networks'},'Detecting XZ network adapters',$('ota-detect'));
  if(response?.ok){const select=$('ota-network');select.replaceChildren();for(const address of response.result.addresses){const option=document.createElement('option');option.value=address;option.textContent=address;select.append(option);}
    $('ota-status').textContent=response.result.ready?'Choose the XZ network adapter, then start the update server.':response.result.reason||'Update the loader on this USB from the USB page.';
    otaReady=!!response.result.ready;$('ota-start').disabled=!otaReady;$('ota-start').classList.toggle('primary',response.result.ready);$('ota-start').classList.toggle('secondary',!response.result.ready);
    $('ota-usb-setup').classList.toggle('hidden',response.result.ready);
  }
};
$('ota-usb-setup').onclick=()=>showPage('setup');
$('ota-start').onclick=async()=>{
  const origin=$('ota-start'),address=$('ota-network').value;if(!address){showError('Detect adapters and choose the XZ network adapter first.',origin);return;}
  $('ota-status').textContent='Starting on '+address+'. On the XZ, open Extras → Network updates.';
  await run({method:'serve_runtime_updates',address},'Serving signed XZ updates',origin);
};
$('verify-inputs').onclick=async()=>{const origin=$('verify-inputs');if(!required(['firmware'],origin))return;const result=await run({method:'inspect_firmware',firmware:$('firmware').value},'Checking firmware',origin);if(result?.ok)$('input-result').textContent=result.result.key_verified_against_input?'XZ 1.26 firmware and boot support verified.':'XZ 1.26 application verified. Choose the official firmware ZIP to verify complete boot support.';};
$('build').onclick=async()=>{
  const origin=$('build');
  if(!required(['volume','firmware'],origin))return;
  if(!$('experimental').checked){showError('Check the testing reminder before preparing a USB.',origin);return;}
  const version=selectedVersionRequest(origin);if(!version)return;
  const volume=$('volume').value,settings=await confirmModSettings(volume,version,origin);if(!settings)return;
  run({method:'build_usb',volume,firmware:$('firmware').value,experimental:true,...version,...settings},'Preparing the USB loader',origin);
};
$('download-firmware').onclick=async()=>{const result=await run({method:'download_firmware'},'Getting the official firmware',$('download-firmware'));if(result?.ok)$('firmware').value=result.result.firmware_path;};
if(!invoke){$('browser-note').classList.remove('hidden');busy(false);}
for(const link of document.querySelectorAll('a[target=_blank]'))link.onclick=async event=>{if(invoke){event.preventDefault();try{await invoke('open_link',{url:link.getAttribute('href')});}catch(error){showError(String(error),link);}}};

function node(tag,text,className){const item=document.createElement(tag);if(text!==undefined)item.textContent=text;if(className)item.className=className;return item;}
function renderUsb(inspection){
  usbInspection=inspection;$('usb-update-details').classList.remove('hidden');
  $('usb-selected-path').textContent=inspection.volume;
  $('usb-loader-status').textContent=inspection.loader.exists?'Existing loader: '+(inspection.loader.bytes/1048576).toFixed(1)+' MB.':'No loader found. Install XZ Mods from the USB page.';
  // Only a version recorded after a write is shown. Older loaders stay unknown; never assume the saved choice.
  const recorded=inspection.recorded_mod_version?.id?inspection.recorded_mod_version:null;
  $('usb-recorded-version').textContent=!inspection.loader.exists?'':recorded?'Mod version on this USB: '+modVersionText(recorded)+'.':'Mod version on this USB: unknown. Loaders written before XZ Mods recorded versions do not say which build they hold.';
  $('usb-loader-note').textContent=inspection.requires_copy_to_usb_root?'This is a prepared folder. Copy the updated autoexec.bin to the USB root afterward.':recorded?'The old loader is backed up before replacement.':'The old loader is backed up before replacement. Its installed version cannot be identified from this file.';
  updateModVersionControls();
  const backups=$('loader-backups');backups.replaceChildren();
  if(inspection.backups.length){
    backups.append(node('h3','Restore a previous loader'));
    const select=node('select');select.className='usb-setting';
    for(const backup of inspection.backups){const option=node('option',new Date(backup.created).toLocaleString()+' - '+(backup.previous.bytes/1048576).toFixed(1)+' MB');option.value=backup.id;select.append(option);}
    const button=node('button','Restore selected backup','secondary restore-loader');
    // Feedback goes after the backups list: renderUsb replaces its contents on success.
    button.onclick=async()=>{const result=await run({method:'restore_usb_loader',volume:inspection.volume,expected_identity:usbInspection.identity,expected_loader:usbInspection.loader,transaction_id:select.value},'Restoring the previous loader',backups);if(result?.ok)renderUsb(result.result.inspection);};
    backups.append(select,button);
  }
}
async function inspectUsb(volume,origin){const result=await run({method:'inspect_usb',volume},'Reading USB loader and settings',origin);if(result?.ok)renderUsb(result.result);}
$('inspect-usb').onclick=async()=>{const origin=$('inspect-usb');try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await inspectUsb(volume,origin);}catch(error){showError(String(error),origin);}};
$('refresh-usb').onclick=()=>{if(usbInspection)inspectUsb(usbInspection.volume,$('refresh-usb'));};
$('update-usb').onclick=async()=>{
  if(!usbInspection?.loader.exists)return;
  const version=selectedVersionRequest($('update-usb'));if(!version)return;
  const inspection=usbInspection,settings=await confirmModSettings(inspection.volume,version,$('update-usb'));if(!settings)return;
  if(usbInspection!==inspection){showError('The USB was read again while you were deciding. Nothing was written. Choose Back up and update again.',$('update-usb'));return;}
  const request={method:'update_usb',volume:inspection.volume,expected_identity:inspection.identity,expected_loader:inspection.loader,experimental:true,...version,...settings};
  if($('firmware').value.trim())request.firmware=$('firmware').value.trim();
  const result=await run(request,'Backing up and updating the USB loader',$('update-usb'));if(result?.ok)renderUsb(result.result.inspection);
};
$('edit-selected-usb').onclick=()=>{if(usbInspection)openSetupFor(usbInspection);};

function renderReleaseNotes(holder,notes,version){
  const entry=node('div',undefined,'release-entry');if(version)entry.append(node('h2','Version '+version));
  for(const [key,title] of [['features','New features'],['fixes','Bug fixes'],['changes','Other changes']]){
    if(!notes[key]?.length)continue;entry.append(node('h3',title));const list=node('ul');for(const text of notes[key])list.append(node('li',text));entry.append(list);
  }
  holder.append(entry);
}
function renderAppUpdate(status){
  $('update-status').textContent=status.message;
  $('app-update-banner').classList.toggle('hidden',status.phase!=='available');
  if(status.phase==='available')$('app-update-banner-text').textContent='XZ Mods '+status.latest_version+' is available.';
  $('install-app-update').classList.toggle('hidden',status.phase!=='available');
  $('install-app-update').disabled=activeJob!==null||appUpdating;
  $('check-app-update').disabled=!invoke||appUpdating||updateChecking;
  const downloading=status.phase==='downloading';$('update-progress').classList.toggle('hidden',!downloading);
  if(status.total){$('update-progress').max=status.total;$('update-progress').value=status.downloaded||0;}else $('update-progress').removeAttribute('value');
  $('update-size').textContent=downloading&&status.downloaded?((status.downloaded/1048576).toFixed(1)+' MB downloaded'):'';
  if(status.notes){$('available-notes').replaceChildren();renderReleaseNotes($('available-notes'),status.notes,status.latest_version);$('available-notes').classList.remove('hidden');}
}
$('about-updates').onclick=()=>showPage('updates');
$('review-app-update').onclick=()=>showPage('updates');
$('open-licenses').onclick=async()=>{try{await invoke('open_licenses');}catch(error){showError(String(error),$('open-licenses'));}};
async function checkForAppUpdates(){
  if(updateChecking||appUpdating)return;updateChecking=true;$('available-notes').classList.add('hidden');renderAppUpdate({phase:'checking',message:'Checking for updates...'});
  try{renderAppUpdate(await invoke('check_app_update'));}catch(error){renderAppUpdate({phase:'error',message:String(error)});}finally{updateChecking=false;$('check-app-update').disabled=!invoke||appUpdating;}
  // The update check also looks for news on sent reports (my-reports.js); it never blocks updates.
  window.myReports?.check('update');
}
$('check-app-update').onclick=checkForAppUpdates;
$('install-app-update').onclick=async()=>{
  if(activeJob!==null||appUpdating)return;appUpdating=true;busy(true);renderAppUpdate({phase:'downloading',message:'Downloading the installer...'});
  const timer=setInterval(async()=>{try{renderAppUpdate(await invoke('app_update_status'));}catch{}},400);
  try{await invoke('install_app_update');}catch(error){try{renderAppUpdate(await invoke('app_update_status'));}catch{renderAppUpdate({phase:'error',message:String(error)});}}
  finally{clearInterval(timer);appUpdating=false;busy(false);$('check-app-update').disabled=!invoke;setTimeout(drainIdle);}
};
async function loadAppInformation(){
  if(!invoke){$('check-app-update').disabled=true;$('open-licenses').disabled=true;return;}
  try{
    const info=await invoke('app_information');document.querySelectorAll('[data-app-version]').forEach(item=>item.textContent=info.version);
    // The Mac build ships no signed updater files, so in-app update cannot work there; .win-only/.mac-only follow this.
    document.documentElement.dataset.platform=info.platform;
    document.documentElement.dataset.updateChannel=info.update_channel||'stable';
    $('update-channel-note').textContent=(info.update_channel==='beta'?'Beta channel. Checks for beta updates automatically at launch.':'Stable channel. Checks for updates automatically at launch.');
    if(info.release_page)$('app-downloads').href=info.release_page;
    if(info.portable_url)$('portable-downloads').href=info.portable_url;
    $('update-install-note').textContent=info.installed?'The Windows installer updates the app and reopens it when finished.':'You are using the portable app. Updating here installs the Windows version; it does not replace this portable folder. After installing, open XZ Mods from the desktop shortcut. To stay portable, download the current ZIP below and replace the old folder.';
    $('release-notes').replaceChildren();for(const release of info.release_notes.releases)renderReleaseNotes($('release-notes'),release,release.version);
    if(info.show_release_notes){showPage('updates');$('updated-message').classList.remove('hidden');await invoke('acknowledge_release_notes');}
    if(info.platform!=='macos')void checkForAppUpdates();
  }catch(error){$('update-status').textContent=String(error);}
}
loadAppInformation();

// Prepare stems. Backend fields reach the page only through stemsApi, so a renamed field is a one-line change there.
const GiB=1073741824,MiB=1048576,INDEX_LIMIT=4*MiB,INDEX_ENTRY_BYTES=1200,TIGHT_FLOOR=2*GiB;
const ST={
  'bar.tracks':'{count} tracks · {ready} ready','bar.free':'{free} free of {total}','bar.usb.chooseFolder':'Choose a folder...','bar.usb.findAgain':'Look for USBs again',
  'estimate.none':'No tracks selected · {missing} tracks need stems','estimate.fits':'{n} tracks selected · about {bytes} · {remaining} free after','estimate.one':'1 track selected · about {bytes} · {remaining} free after',
  'estimate.tight':'{n} tracks selected · about {bytes} · only {remaining} free after','estimate.over':'{n} tracks selected · about {bytes} · {short} more than this USB has free',
  'estimate.indexFull':'{n} tracks selected · the stem index on this USB has room for {room} more','estimate.limit':'{n} tracks selected (most per batch) · about {bytes} · {remaining} free after',
  'estimate.range':'{low} to {high}','estimate.learned':'Estimate learned from {k} tracks prepared on this computer','estimate.first':'First estimate. It gets closer after a few tracks are prepared.',
  'action.prepareNone':'Prepare stems','action.prepareN':'Prepare {n} tracks','action.prepareOne':'Prepare 1 track','action.prepareFit':'Prepare {n} that fit','action.setupEngine':'Set up stem engine',
  'action.cancel':'Cancel','action.retryFailed':'Retry {n} failed','action.prepareRemaining':'Prepare {n} remaining','action.continue':'Continue','action.chooseFewer':'Choose fewer tracks','action.import':'Import stems',
  'engine.ready':'Engine: {preset} on {device}','engine.readyCpu':'Engine: {preset} on this processor','engine.change':'Change','engine.missing':'Stem engine not set up yet · a few minutes, once',
  'engine.unsupportedHost':'Making stems needs a Windows computer or an Apple silicon Mac. You can still import stems here.',
  'search.placeholder':'Search title or artist','filter.all':'All','filter.missing':'Missing','filter.ready':'Ready','filter.cant':'Can\'t prepare','filter.selected':'Selected','pager':'Showing {from} to {to} of {total}',
  'pill.missing':'Missing','pill.ready':'Ready','pill.verified':'Verified','pill.new':'New','pill.damaged':'Damaged','pill.unavailable':'No audio file','pill.unsupported':'Not yet','pill.error':'Can\'t read',
  'pill.queued':'Queued','pill.running':'Preparing {pct}%','pill.failed':'Failed','pill.cancelled':'Cancelled','pill.notStarted':'Not started',
  'sub.unavailable':'Re-export from Rekordbox','sub.unsupported':'{format} support is coming','sub.reused':'Reused existing stems','sub.damaged':'{bad} of 7 mixes failed the check',
  'size.estimateTitle':'Estimated, between {low} and {high}','size.unsupportedTitle':'What it would need once {format} is supported',
  'stage.decode':'Reading the track','stage.load-model':'Loading the stem engine','stage.separate-vocals':'Separating vocals','stage.separate-drums':'Separating drums','stage.postprocess':'Balancing the parts',
  'stage.render':'Mixing the 7 stem mixes','stage.pack':'Writing to the USB','stage.verify':'Checking the result',
  'progress.line':'Preparing {i} of {n} · {title} · {stage} · {pct}% · {eta}','eta.minutes':'about {m} min left','eta.hours':'about {h} h {m} min left','eta.underMinute':'under a minute left','eta.firstRun':'Timing this computer, {elapsed} so far',
  'done.all':'Done in {time} · {n} prepared · {bytes} written','done.someFailed':'Done in {time} · {ok} prepared · {failed} failed · {bytes} written','done.cancelled':'Cancelled · {ok} prepared and kept · {left} not started',
  'done.unplugged':'The USB was disconnected. {ok} tracks are prepared and nothing was left half-written. Plug it back in to continue.','done.full':'The USB is full. {ok} tracks are prepared; {left} did not fit.',
  'done.wrongUsb':'This is a different USB. Plug in {usb} to retry.','done.loaderOld':'To play these on the XZ, update the loader on this USB.','done.updateLoader':'Update loader',
  'drawer.source':'From {format}. About {bytes} on this USB, between {low} and {high}.','drawer.mp3':'MP3s are decoded and lined up with the player before separating, so the stems stay in time with the track.',
  'drawer.byXz':'Prepared by XZ Mods on {date} · {preset} · {bytes}','drawer.byOther':'Prepared outside XZ Mods · {bytes}','drawer.unchecked':'Not checked yet. Verify reads every audio page and does not change your USB.',
  'sub.regenerate':'Prepare again · MP3 timing',
  'drawer.regenerate':'Made from an MP3 before XZ Mods 0.3.0-preview.2, so these stems start 25 ms early against the player. The XDJ-XZ lines them up live; the stem waveforms and other players do not. Prepare again fixes it.',
  'drawer.compat':'Also works with stems prepared by OverCue Desktop.','drawer.unavailable':'The audio file for this track is not on the USB. Export it to this USB again from Rekordbox, then check again.',
  'drawer.unsupported':'{format} tracks cannot be prepared yet. Support is planned. This track would need about {bytes}.','drawer.running':'{stage}. Step {k} of 8, started {elapsed} ago.',
  'drawer.replaceAsk':'Replace the stems for {title}? The current stems stay until the new ones pass their check.','drawer.replace':'Replace','drawer.keep':'Keep',
  'drawer.verify':'Verify','drawer.verifyAgain':'Verify again','drawer.importReplace':'Import stems to replace','drawer.prepareAgain':'Prepare again','drawer.importElsewhere':'Import stems made elsewhere',
  'drawer.checkAgain':'Check again','drawer.retryTrack':'Retry this track',
  'verify.mixes':'{ok} of 7 stem mixes','verify.pages':'{n} audio pages','verify.pagesAll':'Every audio page','verify.identity':'Matches this track','verify.timing':'In time ({r} match)',
  'empty.noUsb.title':'Connect a Rekordbox USB','empty.noUsb.body':'Plug in the USB you export to from Rekordbox. XZ Mods finds it and lists its tracks.','empty.noUsb.action':'Connect a USB',
  'empty.noLib.title':'No Rekordbox library on {usb}','empty.noLib.body':'Export tracks to this USB from Rekordbox, then check again.','empty.noLib.action':'Check again','empty.noLib.other':'Choose another USB',
  'empty.badLib.title':'This USB\'s library can\'t be read','empty.badLib.body':'XZ Mods can check it and repair common damage.','empty.badLib.action':'Check and repair library',
  'empty.locked':'This USB is write-protected. Switch off its lock, then look again.','empty.empty.title':'This Rekordbox library is empty','empty.empty.body':'Export tracks to the USB from Rekordbox, then look again.','empty.lookAgain':'Look again',
  'eng.steps':['Check','Choose','Install','Test'],
  'eng.gpu.title':'Graphics card found','eng.gpu.body':'XZ Mods will prepare stems on this graphics card. That is the fastest way, usually about a minute per track.','eng.vram':'{gb} GB video memory',
  'eng.mps.title':'Apple silicon found','eng.mps.body':'XZ Mods will prepare stems on the built-in graphics. Nothing extra to install for the graphics part.',
  'eng.cpu.title':'No graphics card to use','eng.cpu.body':'XZ Mods will prepare stems on the processor. The stems come out the same, it just takes longer: plan on several minutes per track. Batches can run while you do other things.',
  'eng.badGpu.title':'This graphics card can\'t be used yet','eng.badGpu.card':'The stem engine supports NVIDIA cards on Windows','eng.badGpu.body':'XZ Mods will prepare stems on the processor ({cpu}). The stems come out the same, it just takes longer.',
  'eng.choose.title':'Choose quality','eng.preset.balanced':'Balanced · best quality','eng.preset.balanced.line':'Clean vocals and tight drums.','eng.preset.fast':'Fast',
  'eng.preset.fast.line':'Good separation in about half the time. Handy for big batches.','eng.preset.light':'Light','eng.preset.light.line':'For computers without a graphics card and with little memory. Vocals bleed more.',
  'eng.preset.time':'About {n} minutes per track on this computer.','eng.recommended':'Recommended','eng.choose.note':'You can change this later with Change next to the engine name. Tracks already prepared stay as they are.',
  'eng.install.title':'Download and install','eng.install.body':'XZ Mods downloads {bytes} to this computer, once. Your USB is not touched.','eng.install.runtime':'Graphics runtime','eng.install.models':'{preset} models',
  'eng.install.space':'Space on {drive}','eng.install.verify':'Every file is checked against its known fingerprint before it is used.','eng.install.action':'Download and install',
  'eng.installing.title':'Installing','eng.installing.check':'Check the files','eng.installing.note':'If you cancel, finished downloads are kept and setup picks up where it stopped.',
  'eng.test.title':'Testing on 30 seconds of your music','eng.test.body':'Separating {from} to {to} of {title}. Nothing is written to the USB.','eng.test.sample':'Separating a 30 second sample.',
  'eng.ready.title':'Stem engine ready','eng.ready.body':'30 seconds took {s} s. A 5-minute track will take about {estimate}. Have a listen:',
  'eng.ready.noSel':'30 seconds took {s} s. A 5-minute track will take about {estimate}. Select tracks on the page, then choose Prepare.','eng.done':'Done',
  'eng.fail.space.title':'Not enough space on this computer','eng.fail.space.bold':'Setup needs {need} on drive {drive}, and {free} is free.','eng.fail.space.line':'Free up {short}, for example by emptying the Recycle Bin, then check again.',
  'eng.fail.net.title':'The download stopped','eng.fail.net.bold':'The connection dropped at {done} of {total}.','eng.fail.net.line':'What was downloaded is kept. Resume when the internet is back.','eng.fail.net.action':'Resume download',
  'eng.fail.hash.title':'A downloaded file failed its check','eng.fail.hash.bold':'The {part} did not match its fingerprint.','eng.fail.hash.line':'XZ Mods deleted it so it can\'t be used. Downloading it again usually fixes this.','eng.fail.hash.action':'Download it again',
  'eng.fail.driver.title':'Graphics driver too old','eng.fail.driver.bold':'The graphics engine needs NVIDIA driver {min} or newer. This computer has {have}.','eng.fail.driver.line':'Use the processor now, or update the driver and check again for full speed.',
  'eng.fail.driver.action':'Use the processor','eng.fail.driver.open':'Open NVIDIA drivers',
  'eng.fail.vram.title':'The graphics card ran out of memory','eng.fail.vram.bold':'{preset} needs about {need} of video memory. This card has {free} free.','eng.fail.vram.line':'Fast uses less memory and still sounds good. XZ Mods already has it installed.','eng.fail.vram.action':'Switch to Fast and test',
  'eng.fail.test.title':'The test did not finish','eng.fail.test.action':'Try again','eng.fail.report':'Save a report','eng.checkAgain':'Check again',
  'imp.steps':['Add files','Check','Write'],'imp.title':'Import stems','imp.for':'For {title}','imp.which':'Which track are these for?','imp.changeTrack':'Change track',
  'imp.drop':'Drop stem files or a ZIP here','imp.dropHint':'Suno, Demucs, UVR or any set of WAV, FLAC, MP3 or AIFF files','imp.choose':'Choose files',
  'imp.slot.vocals':'Vocals','imp.slot.drums':'Drums','imp.slot.harmonics':'Harmonics','imp.slot.empty':'Nothing yet','imp.slot.drumsFromInst':'Separated from the instrumental','imp.slot.harmFromInst':'The instrumental, minus the drums',
  'imp.col.file':'File','imp.col.to':'Goes to','imp.col.sure':'How sure','imp.conf.high':'Matched by name','imp.conf.mid':'Best guess','imp.conf.low':'Choose','imp.conf.mix':'Used to check timing',
  'imp.to.choose':'Choose...','imp.to.leaveOut':'Leave out','imp.auto.title':'Everything has a place','imp.review.title':'{n} files need a place',
  'imp.review.tip':'Play a file to hear what it is. Its name could not tell XZ Mods.','imp.review.action':'Place {n} files to continue',
  'imp.aligned':'In time with the track on your USB. Shifted {ms} ms and matched the level.','imp.alignedMp3':'In time with the track on your USB. Shifted {ms} ms to undo the MP3 start delay.',
  'imp.fitShort':'The stems end {s} s early; XZ Mods fills the gap with silence.','imp.fitLong':'The stems run {s} s past the track; XZ Mods trims the end.','imp.residual':'Leftover difference {db} dB',
  'imp.two.ready':'Drums will be separated from the instrumental on this computer while writing, about {s} seconds.','imp.two.setup.title':'Drums need the stem engine',
  'imp.two.setup.body':'This is a vocals and instrumental set. To give the XZ a separate drums button, XZ Mods pulls the drums out of the instrumental on this computer. That needs the stem engine, which is a one-time setup.',
  'imp.two.resume':'After setup, this import continues where you left it.',
  'imp.mis.title':'These stems don\'t line up','imp.mis.body':'They don\'t match the track on your USB closely enough. Even after shifting and levelling, a clear difference is left ({db} dB). This usually means a different edit, tempo or master of the song. On the XZ they would drift or double.',
  'imp.mis.action':'Choose other files','imp.mis.anyway':'Write anyway','imp.wrong.title':'These stems are from a different song','imp.wrong.body':'The stems don\'t sound like this track at all. Pick the track they belong to, or choose other files.',
  'imp.wrong.action':'Choose a different track','imp.write.title':'Ready to write','imp.write.size':'about {bytes} · {remaining} free after','imp.write.sizeReplace':'about {bytes}, replacing {old}',
  'imp.write.body':'XZ Mods writes the 7 stem mixes the XZ plays, then checks them. Your stem files and the track itself stay as they are.','imp.write.replaceBody':'The current stems for this track are replaced only after the new ones pass their check.',
  'imp.write.action':'Write stems to USB','imp.write.replace':'Replace stems on USB','imp.writing.title':'Writing stems','imp.writing.align':'Lining up and levelling',
  'imp.done.title':'Stems written and checked','imp.done.body':'{title} is ready for the XZ. {bytes} written.','imp.done.another':'Import another',
  'imp.fail.space.title':'Not enough space on {usb}','imp.fail.space.body':'These stems need about {need} and {free} is free.','imp.fail.usb.title':'The USB was disconnected','imp.fail.usb.bold':'Nothing was left half-written.',
  'imp.fail.usb.line':'Plug {usb} back in and write again. Your files and choices are kept.','imp.fail.usb.action':'Write again'
};
const STAGES=[['decode',3],['load-model',5],['separate-vocals',40],['separate-drums',30],['postprocess',4],['render',8],['pack',7],['verify',3]];
const PRESETS={balanced:{name:'Balanced',models:'Mel-Band RoFormer vocals, HT Demucs drums'},fast:{name:'Fast',models:'HT Demucs'},light:{name:'Light',models:'Open-Unmix HQ'}};
const FORMATS={wav:'WAV',flac:'FLAC',mp3:'MP3',aif:'AIFF',aiff:'AIFF',m4a:'AAC',aac:'AAC',mp4:'AAC',ogg:'OGG'};
const DONE_STATES=new Set(['prepared','reused','failed','cancelled']);
const FILTERS={all:()=>true,missing:track=>track.status==='missing',ready:track=>track.status==='present',cant:track=>['unsupported','unavailable','error'].includes(track.status)};
const FILTER_STATUS={missing:'missing',ready:'present',cant:'cant'};
// Row state -> pill, label, whether the row can be selected, and the sub-label tone.
const ROW_STATES={
  missing:{pill:'missing',word:'pill.missing',select:true},ready:{pill:'ready',word:'pill.ready',done:true},verified:{pill:'ready',word:'pill.ready',tag:'pill.verified',done:true},
  damaged:{pill:'damaged',word:'pill.damaged',select:true,tone:'bad'},unavailable:{pill:'unavailable',word:'pill.unavailable',tone:'warn'},unsupported:{pill:'unsupported',word:'pill.unsupported'},
  error:{pill:'error',word:'pill.error',tone:'bad'},queued:{pill:'queued',word:'pill.queued',select:true,locked:true},running:{pill:'running',word:'pill.running',select:true,locked:true,tone:'run'},
  new:{pill:'ready',word:'pill.ready',tag:'pill.new',done:true},reused:{pill:'ready',word:'pill.ready',done:true},failed:{pill:'failed',word:'pill.failed',select:true,tone:'bad'},
  cancelled:{pill:'cancelled',word:'pill.cancelled',select:true},notStarted:{pill:'cancelled',word:'pill.notStarted',select:true}
};

const stemsApi={
  methods:{discover:'discover_stem_usbs',browse:'browse_stem_library',batch:'generate_library_stem_batch',single:'generate_library_stems',plan:'plan_stem_import',
    write:'import_grouped_stems',verify:'inspect_overcue',enginePlan:'plan_engine_setup',engineSetup:'setup_engine',engineReport:'save_engine_report'},
  library(data){
    return {raw:data,key:libraryKey(data),volume:data.volume,identity:data.identity,total:data.total,matched:data.matched,offset:data.offset,pageSize:data.page_size,
      free:data.free_bytes,size:data.total_bytes,indexBytes:data.index_bytes||0,stemsBytes:data.summary?.stems_bytes??null,counts:data.summary?.counts||null,
      observations:data.size_model?.learned?data.size_model.observations:0,indexError:data.index_error||null,readOnly:data.read_only===true,loaderOk:data.loader_ok,
      tracks:data.tracks.map(stemsApi.track)};
  },
  track(row){
    const estimate=row.estimate||{};
    return {id:String(row.id),title:row.title||'Track '+row.id,artist:row.artist||'',duration:row.duration||0,filePath:row.file_path,source:row.source||null,
      status:row.stem_status,detail:row.detail||'',canGenerate:Boolean(row.can_generate),canVerify:Boolean(row.can_verify),format:formatOf(row.file_path),
      sampleRate:row.sample_rate||null,bitrate:row.bitrate||null,estimate:{bytes:estimate.bytes??row.estimated_bytes??0,low:estimate.low_bytes??0,high:estimate.high_bytes??0},
      storedBytes:row.stored_bytes??null,producer:row.producer||null,preparedAt:row.prepared_at||null,preset:row.preset||null,
      timeline:row.timeline||null,needsRegeneration:row.needs_regeneration===true};
  },
  noLibrary(state){return state.error_code==='no_library'||/^No Rekordbox Device Library/.test(state.error||'');},
  progress(state){
    const snapshot=state.result?.format===batchFormat?state.result:state.batch;
    return {items:(snapshot?.items||[]).map(item=>({id:String(item.id),state:item.state,stage:item.stage||'',message:item.message||'',error:item.error||'',code:item.error_code||null,pct:item.pct??null})),
      pct:state.pct??null,eta:state.eta_seconds??snapshot?.eta_seconds??null};
  },
  batchEnd(state){
    const result=state?.result?.format===batchFormat?state.result:null;
    return {complete:Boolean(result),cancelled:Boolean(result?.cancelled===true||state?.cancelled),aborted:Boolean(result?.aborted),code:result?.error_code||state?.error_code||null,error:result?.error||state?.error||''};
  },
  didNotFit(item){return item.code==='usb_full'||/not enough usb space/i.test(item.error||'');},
  verify(result){
    const check={mixes:result.verified_mixes??0,pages:result.all_pages_verified===true,pageCount:result.verified_pages??null,identity:result.source_identity_verified===true,correlation:result.alignment_correlation??null};
    return {...check,ok:check.mixes===7&&check.pages&&check.identity};
  },
  engine(plan){
    const device=plan.device||'cpu',presets=(plan.presets||[]).map(preset=>typeof preset==='string'?{id:preset}:{id:preset.id||preset.name,secondsPerTrack:preset.seconds_per_track??null,installed:preset.installed===true,downloadBytes:preset.download_bytes??null});
    return {supported:plan.supported!==false,device,deviceName:plan.device_name||'',vram:plan.vram_bytes||0,cpuName:plan.cpu_name||'',cpuCores:plan.cpu_cores||0,ram:plan.ram_bytes||0,
      unsupportedGpu:plan.unsupported_gpu_name||'',recommended:plan.recommended||(device==='cpu'?'fast':'balanced'),presets:presets.length?presets:Object.keys(PRESETS).map(id=>({id})),
      download:plan.download_bytes_estimate??null,parts:plan.parts||[],drive:plan.data_drive||'',driveFree:plan.data_free_bytes??null,installed:plan.installed_preset||null,testSeconds:plan.test_seconds??null};
  },
  setupProgress(state){return {stage:state.stage||'',pct:state.pct??null,done:state.done_bytes??null,total:state.total_bytes??null,eta:state.eta_seconds??null};},
  setupResult(result){return {preset:result.preset,testSeconds:result.test?.seconds??null};},
  failure(state){return {code:state.error_code||state.result?.error_code||null,detail:state.error_detail||state.result?.error_detail||{},error:state.error||'',hint:state.error_hint||''};},
  importPlan(plan){
    const alignment=plan.alignment&&{offsetMs:plan.alignment.offset_ms??0,residualDb:plan.alignment.residual_db??null,verdict:plan.alignment.verdict};
    return {files:plan.files.map(file=>({name:file.name,path:file.path,group:file.group??null,label:file.label||null,confidence:file.confidence??0,origin:file.origin||'name',seconds:file.seconds??null})),
      alignment,fitLength:plan.fit_length===true,needsDrumSplit:plan.needs_drum_split===true,trackSeconds:(plan.track_frames||0)/44100,drumSplitSeconds:plan.drum_split_seconds??null,request:plan.import_request||{}};
  },
  importDone(result){return {bytes:result.stored_bytes??null,check:result.verified_mixes===undefined?null:stemsApi.verify(result)};},
  importFailure(state){
    const failure=stemsApi.failure(state);
    if(!failure.code&&/not enough usb space/i.test(failure.error))failure.code='usb_full';
    return failure;
  }
};


function t(key,vars={}){return ST[key].replace(/\{(\w+)\}/g,(match,name)=>name in vars?String(vars[name]):match);}
function one(text){return text.replace(/\b1 (track|file)s\b/g,'1 $1').replace('1 file need ','1 file needs ');}
function formatOf(path){const ext=String(path||'').split('.').pop().toLowerCase();return FORMATS[ext]||ext.toUpperCase();}
function gb(bytes){return (bytes/GiB).toFixed(1)+' GB';}
function mb(bytes){return Math.round(bytes/MiB)+' MB';}
function wholeGb(bytes){return Math.round(bytes/GiB)+' GB';}
function clock(seconds){seconds=Math.max(0,Math.round(seconds||0));return Math.floor(seconds/60)+':'+String(seconds%60).padStart(2,'0');}
function spanText(seconds){const minutes=Math.round(seconds/60);return seconds<60?Math.max(1,Math.round(seconds))+' s':minutes<60?minutes+' min':Math.floor(minutes/60)+' h '+minutes%60+' min';}
function etaText(seconds){
  if(seconds<60)return ST['eta.underMinute'];
  let minutes=Math.round(seconds/60);if(minutes>=10)minutes=Math.round(minutes/5)*5;
  return minutes>=60?t('eta.hours',{h:Math.floor(minutes/60),m:minutes%60}):t('eta.minutes',{m:minutes});
}
function ofText(done,total){return total>=GiB?(done/GiB).toFixed(1)+' of '+gb(total):Math.round(done/MiB)+' of '+mb(total);}
function minus(value){return (value<0?'−':'')+Math.abs(Math.round(value));}
function presetName(id){return PRESETS[id]?.name||id||'';}
function stemWorkLocked(){return stemBusy||activeJob!==null||appUpdating||!invoke;}
function engineReady(){return Boolean(stemEngine?.supported&&stemEngine.installed);}
function stemsSupported(){return stemEngine?.supported!==false;}
function driveName(volume){
  const drive=stemDrives.find(item=>item.volume===volume),short=String(volume||'').replace(/[\\/]+$/,'');
  return drive?.label&&drive.label!==volume&&drive.label!==short?drive.label+' ('+short+')':short;
}
function el(tag,props,...children){
  const item=document.createElement(tag);
  for(const [key,value] of Object.entries(props||{})){
    if(value===undefined||value===null||value===false)continue;
    if(key==='class')item.className=value;
    else if(key==='text')item.textContent=value;
    // The app's CSP has no 'unsafe-inline' for styles: a style attribute is dropped, CSSOM writes are allowed.
    else if(key==='style')item.style.cssText=value;
    else if(key==='dataset'){for(const [name,data] of Object.entries(value))if(data!==undefined&&data!==null)item.dataset[name]=data;}
    else if(key.startsWith('on'))item[key]=value;
    else item.setAttribute(key,value===true?'':value);
  }
  item.append(...children.flat().filter(child=>child!==null&&child!==undefined&&child!==false&&child!==''));
  return item;
}
// Splits a ' · ' separated string into parts with separator dots; tones style each part ('b' makes it bold).
function dotted(text,tones=[]){
  return text.split(' · ').flatMap((part,index)=>[...(index?[el('span',{class:'st-dot',text:'·'})]:[]),tones[index]==='b'?el('b',{text:part}):tones[index]?el('span',{class:tones[index],text:part}):part]);
}
// A template filled with '\u0000' where the bold value goes: emphasis(t('imp.for',{title:'\u0000'}),track.title).
function emphasis(text,value){const [before,after]=text.split('\u0000');return [before,el('b',{text:String(value)}),after];}
function libraryKey(data){return data?data.volume+'\n'+JSON.stringify(data.identity):null;}
function plural(count,word){return count+' '+word+(count===1?'':'s');}
function plainButton(label,action,options={}){
  return el('button',{type:'button',class:options.primary?'primary':options.text?'text-button':'secondary',id:options.id,text:label,disabled:options.disabled,title:options.title,dataset:{key:options.key||label},onclick:action});
}

function selectionSummary(){
  const lib=stemLibrary,items=[...stemSelection.values()],n=items.length;
  const bytes=items.reduce((sum,item)=>sum+item.estimate.bytes,0),low=items.reduce((sum,item)=>sum+item.estimate.low,0),high=items.reduce((sum,item)=>sum+item.estimate.high,0);
  const remaining=lib.free-bytes,room=Math.max(0,Math.floor((INDEX_LIMIT-lib.indexBytes)/INDEX_ENTRY_BYTES));
  const fit=!n?'none':room<n?'index':remaining<0?'over':remaining<Math.max(TIGHT_FLOOR,lib.size*0.05)?'tight':'ok';
  let fitCount=n;
  if(fit==='index'||fit==='over'){let used=0;fitCount=0;for(const item of items){if(fitCount>=room||used+item.estimate.bytes>lib.free)break;used+=item.estimate.bytes;fitCount++;}}
  return {items,n,bytes,low,high,remaining,room,fit,fitCount};
}
function prepareSelection(){const sel=selectionSummary();startStemBatch(sel.items.slice(0,sel.fitCount));}
function dismissEnding(){if(stemBatch&&!stemBatch.running)stemBatch.dismissed=true;}

function rowState(track){
  const key=stemSelectionKey+'\n'+track.id,check=stemChecks.get(key),item=stemBatch?.key===stemSelectionKey?stemBatch.byId.get(track.id):null;
  if(track.status==='present'&&check&&!check.ok)return 'damaged';
  if(track.status==='present'&&stemFresh.has(key))return 'new';
  if(track.status==='present'&&check)return 'verified';
  if(item){
    if(item.state==='running')return stemBatch.running?'running':'cancelled';
    if(item.state==='queued')return stemBatch.running?'queued':'notStarted';
    if(item.state==='prepared')return 'new';
    if(item.state in ROW_STATES)return item.state;
  }
  if(track.status==='present')return 'ready';
  return ROW_STATES[track.status]?track.status:'error';
}
function canSelect(track,state){return stemsSupported()&&Boolean(ROW_STATES[state].select)&&(state!=='missing'||track.canGenerate);}
function batchItem(id){return stemBatch?.key===stemSelectionKey?stemBatch.byId.get(id):null;}
function stageLabel(item){return ST['stage.'+item?.stage]||item?.message||'';}
function trackPct(id){return Math.round((stemBatch?.fractions.get(id)||0)*100);}
function sourceText(track){return [track.format+(track.bitrate?' '+Math.round(track.bitrate/1000)+' kbps':''),track.sampleRate?track.sampleRate/1000+' kHz':''].filter(Boolean).join(', ');}
function dateText(seconds){return new Date(seconds*1000).toLocaleDateString('en-GB',{day:'numeric',month:'short',year:'numeric'});}

function stemMeter(lib,sel,withSelection){
  const width=bytes=>(Math.max(0,bytes)/lib.size*100).toFixed(2)+'%';
  const fresh=stemBatch?.key===lib.key&&!stemBatch.refreshed?stemBatch.items.filter(item=>item.state==='prepared').reduce((sum,item)=>sum+item.estimate.bytes,0):0;
  const stems=(lib.stemsBytes??0)+fresh,music=lib.size-lib.free-(lib.stemsBytes??0);
  const segment=(kind,size,title)=>{const part=el('span',{class:'st-seg-'+kind,title});part.style.width=typeof size==='string'?size:width(size);return part;};
  const meter=el('div',{class:'st-meter',role:'img','aria-label':'USB space: '+gb(lib.free)+' free of '+gb(lib.size)},
    segment('music',music,'Music and other files '+gb(music)),lib.stemsBytes===null?null:segment('stems',stems,'Stems already on the USB '+gb(stems)));
  if(withSelection&&sel.n){
    if(sel.fit==='over')meter.append(segment('sel','calc('+width(lib.free)+' - 6px)','This selection, about '+gb(sel.bytes)),segment('over','6px','Does not fit'));
    else meter.append(segment('sel',sel.bytes,'This selection, about '+gb(sel.bytes)),segment('range',sel.high-sel.bytes,'Could be up to '+gb(sel.high)));
  }
  return meter;
}
function estimateLine(sel,lib){
  const line=el('p',{class:'st-estimate',dataset:{fit:sel.fit}});
  if(!sel.n){line.append(...dotted(lib.counts?t('estimate.none',{missing:lib.counts.missing}):t('estimate.none').split(' · ')[0],['b']));return line;}
  const vars={n:sel.n,bytes:gb(sel.bytes),remaining:gb(sel.remaining),short:gb(-sel.remaining),room:sel.room};
  const key=sel.fit==='index'?'estimate.indexFull':sel.fit==='over'?'estimate.over':sel.n>=batchLimit?'estimate.limit':sel.fit==='tight'?'estimate.tight':sel.n===1?'estimate.one':'estimate.fits';
  const parts=one(t(key,vars)).split(' · ').length;
  line.append(...dotted(one(t(key,vars)),['b',...Array(Math.max(0,parts-2)).fill(''),'st-after']));
  if(sel.fit!=='index'){
    const low=gb(sel.low),high=gb(sel.high);
    line.append(el('span',{class:'st-range',title:lib.observations?t('estimate.learned',{k:lib.observations}):ST['estimate.first'],text:t('estimate.range',{low:low.replace(' GB',''),high})}));
  }
  return line;
}
function progressLine(batch){
  const current=batch.items.find(item=>item.state==='running')||batch.items.find(item=>item.state==='queued');
  const finished=batch.items.filter(item=>DONE_STATES.has(item.state)).length,wrap=el('div',{class:'st-progress'});
  const eta=batch.eta!==null?etaText(batch.eta):t('eta.firstRun',{elapsed:clock((Date.now()-batch.started)/1000)});
  const parts=[[t('progress.line',{i:Math.min(finished+1,batch.items.length),n:batch.items.length,title:'',stage:'',pct:'',eta:''}).split(' · ')[0],'b'],[current?.title,''],[stageLabel(current),'st-stage'],
    [current?.state==='running'?trackPct(current.id)+'%':'','st-stage'],[eta,'st-eta']].filter(([text])=>text);
  const progress=el('progress',{max:'1','aria-label':'Batch progress'});progress.value=batch.floor;
  wrap.append(el('p',{class:'st-progress-line',role:'status','aria-live':'polite'},...dotted(parts.map(([text])=>text).join(' · '),parts.map(([,tone])=>tone))),progress);
  return wrap;
}
function engineLine(){
  if(!stemEngine)return null;
  if(!stemEngine.supported)return el('span',{class:'st-engine',text:ST['engine.unsupportedHost']});
  if(!engineReady())return el('span',{class:'st-engine',dataset:{state:'missing'},text:ST['engine.missing']});
  const cpu=stemEngine.device==='cpu',device=stemEngine.deviceName.replace(/^NVIDIA (GeForce )?/,'');
  return el('span',{class:'st-engine'},...emphasis(t(cpu?'engine.readyCpu':'engine.ready',{preset:'\u0000',device}),presetName(stemEngine.installed)),' · ',
    plainButton(ST['engine.change'],()=>openEngineWizard('choose'),{text:true,key:'engine-change',title:'Opens setup at Choose quality',disabled:stemWorkLocked()}));
}
function selectPrimary(sel){
  if(stemEngine&&!engineReady())return {label:ST['action.setupEngine'],action:()=>openEngineWizard('check',sel.n?prepareSelection:null,{label:sel.n?preparePrimary(sel).label:null})};
  return preparePrimary(sel);
}
function preparePrimary(sel){
  if(!sel.n)return {label:ST['action.prepareNone'],disabled:true};
  if(sel.fit==='over'||sel.fit==='index')return {label:t('action.prepareFit',{n:sel.fitCount}),disabled:!sel.fitCount,action:prepareSelection};
  return {label:sel.n===1?ST['action.prepareOne']:t('action.prepareN',{n:sel.n}),action:prepareSelection};
}
function endingModel(batch,sel){
  const ending=batch.ending,line=el('p',{class:'st-summary',role:'status'}),failed=batch.items.filter(item=>item.state==='failed');
  let primary=null;
  if(ending.kind==='all'){line.append(...dotted(t('done.all',{time:ending.time,n:ending.ok,bytes:gb(ending.bytes)}),['b','ok']));primary={label:ST['action.prepareNone'],disabled:true};}
  else if(ending.kind==='someFailed'){line.append(...dotted(t('done.someFailed',{time:ending.time,ok:ending.ok,failed:ending.failed,bytes:gb(ending.bytes)}),['b','ok','bad']));primary={label:t('action.retryFailed',{n:ending.failed}),action:()=>startStemBatch(failed)};}
  else if(ending.kind==='cancelled'){line.append(...dotted(t('done.cancelled',ending),['b','ok','warn']));primary={label:t('action.prepareRemaining',{n:ending.left}),disabled:!stemSelection.size,action:()=>startStemBatch([...stemSelection.values()])};}
  else if(ending.kind==='unplugged'){line.append(el('span',{class:'warn',text:t('done.unplugged',ending)}));primary={label:ST['action.continue'],action:continueStemBatch};}
  else if(ending.kind==='full'){line.append(el('span',{class:'warn',text:t('done.full',ending)}));primary={label:ST['action.chooseFewer'],action:()=>{for(const item of failed)if(stemsApi.didNotFit(item))stemSelection.delete(item.id);dismissEnding();renderStems();}};}
  else if(ending.kind==='wrongUsb')line.append(el('span',{class:'bad',text:t('done.wrongUsb',{usb:driveName(batch.volume)})}));
  else{line.append(el('span',{class:'bad',text:ending.error}));primary=selectPrimary(sel);}
  if((ending.kind==='all'||ending.kind==='someFailed')&&stemLibrary.loaderOk===false)line.append(' ',ST['done.loaderOld'],' ',plainButton(ST['done.updateLoader'],()=>showPage('setup'),{text:true,key:'update-loader'}));
  return {line,meterSelection:false,primary};
}
function stemBarModel(){
  const lib=stemLibrary,sel=selectionSummary(),batch=stemBatch?.key===lib.key?stemBatch:null;
  if(!stemsSupported())return {line:el('p',{class:'st-estimate',text:lib.counts?t('estimate.none',{missing:lib.counts.missing}).split(' · ')[1]:''}),primary:{label:ST['action.import'],action:()=>openImportWizard(null)}};
  if(batch?.running)return {line:progressLine(batch),primary:{label:ST['action.cancel'],secondary:true,disabled:batch.cancelRequested,action:cancelActiveJob}};
  if(batch?.ending&&!batch.dismissed)return endingModel(batch,sel);
  if(lib.readOnly)return {line:el('p',{class:'st-estimate',dataset:{fit:'over'}},el('span',{class:'st-after',text:ST['empty.locked']})),primary:{label:ST['empty.lookAgain'],action:()=>browseStemLibrary(lib.volume,lib.offset)}};
  return {line:estimateLine(sel,lib),meterSelection:true,primary:selectPrimary(sel)};
}
function renderStemBar(){
  const lib=stemLibrary,model=stemBarModel(),running=Boolean(stemBatch?.running),locked=stemWorkLocked();
  const select=el('select',{'aria-label':'Rekordbox USB',dataset:{key:'drive'},disabled:locked},
    ...stemDrives.map(drive=>el('option',{value:drive.volume,text:driveName(drive.volume)})),el('option',{value:'__folder',text:ST['bar.usb.chooseFolder']}),el('option',{value:'__find',text:ST['bar.usb.findAgain']}));
  select.value=stemVolume;
  select.onchange=()=>{const value=select.value;select.value=stemVolume;if(value==='__folder')chooseStemFolder(select);else if(value==='__find')discoverStemDrives();else{stemVolume=value;resetStemView();browseStemLibrary(value);}};
  const counts=lib.counts?t('bar.tracks',{count:lib.total,ready:lib.counts.present}):lib.total+' tracks';
  const free=t('bar.free',{free:'\u0000',total:gb(lib.size)}).split('\u0000');
  const primary=model.primary&&plainButton(model.primary.label,model.primary.action,{primary:!model.primary.secondary,id:'st-primary',key:'primary',disabled:Boolean(model.primary.disabled)||(locked&&!running)});
  $('st-bar').replaceChildren(
    el('div',{class:'st-bar-main'},el('div',{class:'st-drive'},select,el('span',{class:'st-count',text:counts}),el('span',{class:'st-free'},free[0],el('b',{text:gb(lib.free)}),free[1])),
      stemMeter(lib,selectionSummary(),model.meterSelection),model.line),
    el('div',{class:'st-act'},primary,engineLine()));
}
function renderStemTabs(){
  const lib=stemLibrary,counts=lib.counts,locked=stemWorkLocked();
  if(stemFilter==='selected'&&!stemSelection.size)stemFilter='all';
  const tabs=[['all',ST['filter.all'],lib.total],['missing',ST['filter.missing'],counts?.missing],['ready',ST['filter.ready'],counts?.present],
    ['cant',ST['filter.cant'],counts&&counts.unsupported+counts.unavailable+counts.error]];
  if(stemSelection.size)tabs.push(['selected',ST['filter.selected'],stemSelection.size]);
  $('st-tabs').replaceChildren(...tabs.map(([id,label,count])=>el('button',{type:'button','aria-pressed':String(stemFilter===id),disabled:id!=='selected'&&locked,dataset:{key:'tab:'+id,filter:id}},label,typeof count==='number'?el('b',{text:String(count)}):null)));
  $('st-import').disabled=locked;
}
function visibleStemTracks(){
  if(stemFilter!=='selected')return stemLibrary.tracks.filter(FILTERS[stemFilter]);
  const query=stemQuery.toLowerCase();
  return [...stemSelection.values()].filter(track=>!query||(track.title+' '+track.artist).toLowerCase().includes(query));
}
function sizeCell(track,state){
  const estimate=track.estimate,cell=(text,kind,title)=>el('td',{class:'st-size',dataset:{kind},title,text});
  if(ROW_STATES[state].done||state==='damaged')return track.storedBytes!==null?cell(mb(track.storedBytes)):cell('≈ '+mb(estimate.bytes),'estimate');
  if(state==='unsupported')return cell('≈ '+mb(estimate.bytes),'none',t('size.unsupportedTitle',{format:track.format}));
  if(state==='unavailable'||state==='error')return cell('–','none');
  return cell('≈ '+mb(estimate.bytes),'estimate',t('size.estimateTitle',{low:mb(estimate.low),high:mb(estimate.high)}));
}
function rowSub(track,state){
  const item=batchItem(track.id),check=stemChecks.get(stemSelectionKey+'\n'+track.id);
  const text={damaged:check&&check.mixes<7?t('sub.damaged',{bad:7-check.mixes}):'The check failed',unavailable:ST['sub.unavailable'],unsupported:t('sub.unsupported',{format:track.format}),
    error:track.detail,running:stageLabel(item),reused:ST['sub.reused'],failed:item?.error||'',missing:track.canGenerate?'':track.detail}[state];
  if(!text&&ROW_STATES[state].done&&track.needsRegeneration)return el('span',{class:'st-sub',dataset:{tone:'warn'},text:ST['sub.regenerate']});
  return text?el('span',{class:'st-sub',dataset:{tone:ROW_STATES[state].tone},text}):null;
}
function verifyChips(check){
  return el('ul',{class:'st-verify'},el('li',{dataset:{ok:String(check.mixes===7)},text:t('verify.mixes',{ok:check.mixes})}),
    el('li',{dataset:{ok:String(check.pages)},text:check.pageCount?t('verify.pages',{n:check.pageCount.toLocaleString('en-US')}):ST['verify.pagesAll']}),
    el('li',{dataset:{ok:String(check.identity)},text:ST['verify.identity']}),check.correlation===null?null:el('li',{text:t('verify.timing',{r:check.correlation.toFixed(3)})}));
}
function provenance(track){
  const bytes=track.storedBytes===null?'–':mb(track.storedBytes);
  if(track.producer==='xz-mods')return track.preparedAt&&track.preset?t('drawer.byXz',{date:dateText(track.preparedAt),preset:presetName(track.preset),bytes}):
    [track.preparedAt?'Prepared by XZ Mods on '+dateText(track.preparedAt):'Prepared by XZ Mods',presetName(track.preset),bytes].filter(Boolean).join(' · ');
  return track.producer?t('drawer.byOther',{bytes}):'Prepared · '+bytes;
}
function stemDrawer(track,state){
  const locked=stemWorkLocked(),item=batchItem(track.id),check=stemChecks.get(stemSelectionKey+'\n'+track.id),estimate=track.estimate,facts=[],actions=[];
  const path=el('p',{class:'st-path',text:track.filePath.replace(/^\//,'')}),action=(key,name)=>plainButton(ST[key],null,{key:name+':'+track.id,disabled:locked});
  const withAction=(button,name)=>{button.dataset.action=name;return button;};
  if(state==='missing'||state==='queued'||state==='cancelled'||state==='notStarted'){
    facts.push(path,el('p',{text:t('drawer.source',{format:sourceText(track),bytes:mb(estimate.bytes),low:mb(estimate.low),high:mb(estimate.high)})}),track.format==='MP3'?el('p',{text:ST['drawer.mp3']}):null);
    if(state==='missing')actions.push(withAction(action('drawer.importElsewhere','import'),'import'));
  }else if(ROW_STATES[state].done||state==='damaged'){
    facts.push(el('p',{text:provenance(track)}),check?verifyChips(check):el('p',{text:ST['drawer.unchecked']}),el('p',{class:'st-compat',text:ST['drawer.compat']}));
    if(track.needsRegeneration)facts.splice(1,0,el('p',{class:'st-regen',text:ST['drawer.regenerate']}));
    if(track.canVerify&&state!=='damaged')actions.push(withAction(action(check?'drawer.verifyAgain':'drawer.verify','verify'),'verify'));
    actions.push(withAction(action('drawer.importReplace','import'),'import'));
    if(track.canGenerate&&stemsSupported())actions.push(withAction(action('drawer.prepareAgain','ask'),'ask'));
  }else if(state==='unavailable'){facts.push(el('p',{text:ST['drawer.unavailable']}),path);actions.push(withAction(action('drawer.checkAgain','check'),'check'));}
  else if(state==='unsupported')facts.push(el('p',{text:t('drawer.unsupported',{format:track.format,bytes:mb(estimate.bytes)})}),path);
  else if(state==='error'){facts.push(el('p',{text:track.detail}),path);actions.push(withAction(action('drawer.checkAgain','check'),'check'));}
  else if(state==='running'){
    const step=STAGES.findIndex(([id])=>id===item.stage)+1,label=stageLabel(item);
    facts.push(el('p',{},...(step?emphasis(t('drawer.running',{stage:'\u0000',k:step,elapsed:clock((Date.now()-(stemBatch.since.get(track.id)||Date.now()))/1000)}),label):[label])),path);
  }else if(state==='failed'){facts.push(el('p',{},el('b',{class:'st-reason',text:item.error})),path);actions.push(withAction(action('drawer.retryTrack','retry'),'retry'));}
  if(stemConfirm===track.id)facts.push(el('div',{class:'st-confirm'},t('drawer.replaceAsk',{title:track.title}),
    withAction(plainButton(ST['drawer.replace'],null,{key:'replace:'+track.id,disabled:locked}),'replace'),withAction(plainButton(ST['drawer.keep'],null,{key:'keep:'+track.id}),'keep')));
  return el('tr',{class:'st-drawer',dataset:{for:track.id}},el('td',{colspan:'7'},el('div',{class:'st-drawer-grid'},el('div',{},...facts),el('div',{class:'actions'},...actions))));
}
function stemRow(track){
  const state=rowState(track),spec=ROW_STATES[state],open=stemOpen.has(track.id),selectable=canSelect(track,state),picked=selectable&&stemSelection.has(track.id);
  const box=selectable?el('label',{class:'st-check'},el('input',{type:'checkbox',checked:picked,'aria-label':'Select '+track.title,dataset:{key:'pick:'+track.id,pick:track.id},
    disabled:Boolean(spec.locked)||stemWorkLocked()||(!picked&&stemSelection.size>=batchLimit)})):spec.done?el('span',{class:'st-nocheck','aria-hidden':'true',text:'✓'}):null;
  const pill=el('span',{class:'st-pill',dataset:{s:spec.pill}},state==='running'?t('pill.running',{pct:trackPct(track.id)}):ST[spec.word],spec.tag?el('span',{class:'st-tag',text:ST[spec.tag]}):null);
  if(state==='running')pill.style.setProperty('--p',trackPct(track.id)+'%');
  const row=el('tr',{class:'st-row'+(picked?' picked':'')+(open?' is-open':''),dataset:{id:track.id}},
    el('td',{},box),el('td',{},el('span',{class:'st-title',text:track.title}),el('span',{class:'st-artist',text:track.artist||'Unknown artist'})),
    el('td',{},el('span',{class:'st-fmt','data-dim':state==='unsupported'?'':null,text:track.format})),el('td',{class:'st-len',text:clock(track.duration)}),
    el('td',{},pill,rowSub(track,state)),sizeCell(track,state),
    el('td',{class:'st-open'},el('button',{type:'button',class:'st-open-button','aria-expanded':String(open),'aria-label':'Details for '+track.title,dataset:{key:'open:'+track.id,open:track.id}},el('span',{'aria-hidden':'true',text:'›'}))));
  return open?[row,stemDrawer(track,state)]:[row];
}
function renderStemTable(){
  const tracks=visibleStemTracks(),lib=stemLibrary,body=$('stem-tracks'),locked=stemWorkLocked();
  stemRowsById=new Map(tracks.map(track=>[track.id,track]));
  body.replaceChildren(...(tracks.length?tracks.flatMap(stemRow):[el('tr',{},el('td',{colspan:'7',class:'st-none',text:'No tracks match your search.'}))]));
  const missing=tracks.filter(track=>rowState(track)==='missing'&&canSelect(track,'missing')),picked=missing.filter(track=>stemSelection.has(track.id)).length;
  const all=$('st-select-all');all.checked=Boolean(missing.length)&&picked===missing.length;all.indeterminate=picked>0&&picked<missing.length;
  all.disabled=locked||!missing.length;$('st-select-all-label').classList.toggle('hidden',!stemsSupported());
  const paged=stemFilter!=='selected';$('st-pager-text').textContent=paged&&lib.matched?t('pager',{from:lib.offset+1,to:Math.min(lib.offset+lib.pageSize,lib.matched),total:lib.matched}):'';
  $('stem-previous').classList.toggle('hidden',!paged);$('stem-next').classList.toggle('hidden',!paged);
  $('stem-previous').disabled=locked||lib.offset===0;$('stem-next').disabled=locked||lib.offset+lib.pageSize>=lib.matched;
}
function renderStemEmpty(){
  const name=driveName(stemVolume),view={
    nousb:[ST['empty.noUsb.title'],ST['empty.noUsb.body'],[ST['empty.noUsb.action'],openStemConnect]],
    nolib:[t('empty.noLib.title',{usb:name}),ST['empty.noLib.body'],[ST['empty.noLib.action'],()=>browseStemLibrary(stemVolume)],[ST['empty.noLib.other'],openStemConnect]],
    badlib:[ST['empty.badLib.title'],ST['empty.badLib.body'],[ST['empty.badLib.action'],()=>openLibraryRepair(stemVolume)]],
    empty:[ST['empty.empty.title'],ST['empty.empty.body'],[ST['empty.lookAgain'],()=>browseStemLibrary(stemVolume)]]
  }[stemPhase],locked=stemWorkLocked();
  $('st-empty').replaceChildren(el('h2',{text:view[0]}),el('p',{text:view[1]}),plainButton(view[2][0],view[2][1],{primary:true,key:'empty-primary',disabled:locked}),
    ...(view[3]?[plainButton(view[3][0],view[3][1],{text:true,key:'empty-other',disabled:locked})]:[]));
}
function renderStems(){
  if(stemPhase==='idle')return;
  const focused=document.activeElement?.closest?.('#stems')?document.activeElement.dataset.key:null;
  const home=feedbackHome?.dataset?.key&&(!feedbackHome.isConnected||feedbackHome.closest('#stems'))?feedbackHome.dataset.key:null;
  const blocked=['nousb','nolib','badlib','empty'].includes(stemPhase),page=$('st-page');
  $('st-empty').classList.toggle('hidden',!blocked);page.classList.toggle('hidden',blocked);page.dataset.phase=stemPhase;
  if(blocked)renderStemEmpty();
  else if(stemPhase==='looking')$('st-bar').replaceChildren(el('p',{class:'st-estimate',text:'Looking for Rekordbox USBs...'}));
  else{renderStemBar();renderStemTabs();renderStemTable();page.style.setProperty('--st-bar-h',$('st-bar').offsetHeight+'px');}
  const find=key=>key&&$('stems').querySelector('[data-key="'+CSS.escape(key)+'"]');
  if(home&&!feedbackHome.isConnected){feedbackHome=find(home)||$('st-bar');positionFeedback();}
  const target=find(focused);if(target&&document.activeElement!==target)target.focus({preventScroll:true});
  for(const id of ['stem-connect-find','stem-connect-choose'])$(id).disabled=stemWorkLocked();
  $('stem-connect-check').disabled=stemWorkLocked()||!stemVolume;
}
function renderStemLibrary(data){stemDrives=stemDrives.some(drive=>drive.volume===data.volume)?stemDrives:[...stemDrives,{volume:data.volume,label:data.volume}];stemVolume=data.volume;applyStemLibrary(stemsApi.library(data));renderStems();}
function applyStemLibrary(lib){
  if(lib.key!==stemSelectionKey){stemSelection.clear();stemOpen.clear();stemConfirm=null;stemSelectionKey=lib.key;}
  for(const track of lib.tracks)if(stemSelection.has(track.id))stemSelection.set(track.id,track);
  stemLibrary=lib;stemPhase=lib.indexError?'badlib':lib.total===0?'empty':'ready';
  if(stemBatch&&!stemBatch.running&&stemBatch.key===lib.key){stemBatch.refreshed=true;if(stemBatch.ending?.kind==='wrongUsb')stemBatch.ending.kind='unplugged';}
}
function resetStemView(){stemFilter='all';stemQuery='';$('stem-search').value='';stemOpen.clear();stemConfirm=null;}
async function browseStemLibrary(volume,offset=0){
  if(!volume||stemLibraryLoading||activeJob!==null||appUpdating)return;
  stemLibraryLoading=true;const query=stemQuery,request={method:stemsApi.methods.browse,volume,offset:Math.max(0,offset),query};
  if(FILTER_STATUS[stemFilter])request.status=FILTER_STATUS[stemFilter];
  try{
    const result=await run(request,'Reading USB music');
    if(!result)return;
    stemVolume=volume;
    if(result.ok)applyStemLibrary(stemsApi.library(result.result));else{stemLibrary=null;stemPhase=stemsApi.noLibrary(result)?'nolib':'badlib';}
  }finally{stemLibraryLoading=false;renderStems();}
  if(query!==stemQuery&&stemLibrary&&stemFilter!=='selected')await browseStemLibrary(stemLibrary.volume,0);
}
async function loadStemEngine(){
  const result=await run({method:stemsApi.methods.enginePlan},'Checking this computer');
  if(result)stemEngine=result.ok?stemsApi.engine(result.result):{...stemsApi.engine({}),error:result.error||''};
  return result;
}
async function discoverStemDrives(){
  if(!invoke)return;
  if(!stemLibrary){stemPhase='looking';renderStems();}
  const result=await run({method:stemsApi.methods.discover},'Finding Rekordbox USBs');
  if(!result)return;
  stemLibraryStarted=true;
  const found=result.ok?result.result.drives:[],chosen=stemDrives.filter(drive=>drive.chosen&&!found.some(item=>item.volume===drive.volume));
  stemDrives=[...found,...chosen];
  if(!stemDrives.some(drive=>drive.volume===stemVolume))stemVolume=stemDrives[0]?.volume||null;
  if(!stemEngine)await loadStemEngine();
  if(stemVolume)await browseStemLibrary(stemVolume);else{stemLibrary=null;stemPhase='nousb';renderStems();}
  return found.length;
}
// The first visit lists USBs. If another job (such as the startup mod version read) was running, it waits and tries again.
async function startStemLibrary(){
  if(stemLibraryStarted||stemLibraryStarting||!invoke)return;
  stemLibraryStarting=true;let listed;
  try{listed=await discoverStemDrives();}finally{stemLibraryStarting=false;}
  if(listed===undefined&&!$('stems').classList.contains('hidden')&&!idleQueue.includes(startStemLibrary))idleQueue.push(startStemLibrary);
}
async function chooseStemFolder(origin){
  try{
    const volume=await invoke('pick_path',{kind:'folder'});if(!volume)return null;
    if(!stemDrives.some(drive=>drive.volume===volume))stemDrives.push({volume,label:volume,chosen:true});
    stemVolume=volume;resetStemView();await browseStemLibrary(volume);return volume;
  }catch(error){showError(String(error),origin);return null;}
}

async function startStemBatch(entries){
  const lib=stemLibrary;
  if(!lib||stemWorkLocked()||stemBatch?.running)return;
  if(!engineReady()){openEngineWizard('check',()=>startStemBatch(entries),{label:entries.length===1?ST['action.prepareOne']:t('action.prepareN',{n:entries.length})});return;}
  const items=[],seen=new Set();
  for(const entry of entries){if(seen.has(entry.id)||items.length>=batchLimit)continue;seen.add(entry.id);items.push({id:entry.id,title:entry.title,artist:entry.artist,duration:entry.duration,estimate:entry.estimate,state:'queued',stage:'',message:'',error:'',code:null});}
  if(!items.length)return;
  stemBatch={key:lib.key,volume:lib.volume,items,byId:new Map(items.map(item=>[item.id,item])),running:true,cancelRequested:false,refreshed:false,dismissed:false,ending:null,
    started:Date.now(),stemsBefore:lib.stemsBytes,floor:0,eta:null,etaAt:0,firstId:null,since:new Map(),fractions:new Map()};
  renderStems();
  const batch=stemBatch,state=await run({method:stemsApi.methods.batch,volume:lib.volume,expected_identity:lib.identity,track_ids:items.map(item=>item.id),preset:stemEngine.installed},
    'Preparing stems for '+plural(items.length,'track'),$('st-primary'),applyStemBatchState);
  if(!state){stemBatch=null;renderStems();return;}
  applyStemBatchState(state);batch.running=false;batch.cancelRequested=false;
  if(stemSelectionKey===batch.key)for(const item of batch.items)if(item.state==='prepared'||item.state==='reused')stemSelection.delete(item.id);
  batch.ending={...batchEnding(batch,stemsApi.batchEnd(state)),time:spanText((Date.now()-batch.started)/1000),
    bytes:batch.items.filter(item=>item.state==='prepared').reduce((sum,item)=>sum+item.estimate.bytes,0)};
  renderStems();
  if(stemLibrary?.key!==batch.key)return;
  await browseStemLibrary(batch.volume,stemLibrary.offset);
  if(stemLibrary?.key===batch.key&&stemLibrary.stemsBytes!==null&&batch.stemsBefore!==null){batch.ending.bytes=Math.max(0,stemLibrary.stemsBytes-batch.stemsBefore);renderStems();}
}
function batchEnding(batch,end){
  const count=state=>batch.items.filter(item=>item.state===state).length,ok=count('prepared')+count('reused');
  const misfits=batch.items.filter(item=>item.state==='failed'&&stemsApi.didNotFit(item)).length;
  if(!end.complete&&!end.cancelled)return {kind:'error',error:end.error||'The batch ended without a result. Look again to see which tracks were prepared.'};
  if(end.code==='usb_changed')return {kind:'wrongUsb',ok};
  if(end.aborted)return {kind:'unplugged',ok};
  if(end.cancelled)return {kind:'cancelled',ok,left:count('queued')+count('cancelled')+count('running')};
  if(misfits)return {kind:'full',ok,left:misfits};
  if(count('failed'))return {kind:'someFailed',ok,failed:count('failed')};
  return {kind:'all',ok};
}
function stageFraction(stage,pct,first){
  const index=STAGES.findIndex(([id])=>id===stage);if(index<0)return null;
  const weights=STAGES.map(([id,weight])=>id==='load-model'&&!first?0:weight),total=weights.reduce((sum,weight)=>sum+weight,0);
  return (weights.slice(0,index).reduce((sum,weight)=>sum+weight,0)+weights[index]*Math.min(100,Math.max(0,pct||0))/100)/total;
}
// Remaining time for the whole batch: the backend ETA when it sends one, else the engine test or the tracks finished so far.
function batchEtaSeconds(batch,reported){
  if(reported!==null)return reported;
  const share=item=>(item.duration||0)*(1-(batch.fractions.get(item.id)||0));
  const left=batch.items.filter(item=>item.state==='queued'||item.state==='running').reduce((sum,item)=>sum+share(item),0);
  if(stemEngine?.testSeconds)return left*stemEngine.testSeconds/30;
  const done=batch.items.filter(item=>item.state==='prepared'||item.state==='running').reduce((sum,item)=>sum+(item.duration||0)*(batch.fractions.get(item.id)||0),0);
  return batch.items.some(item=>item.state==='prepared')&&done?left*(Date.now()-batch.started)/1000/done:null;
}
function applyStemBatchState(state){
  const batch=stemBatch;if(!batch?.running)return;
  const progress=stemsApi.progress(state);
  for(const update of progress.items){
    const item=batch.byId.get(update.id);if(!item)continue;
    if(update.state==='running'&&item.state!=='running'){batch.since.set(item.id,Date.now());batch.firstId??=item.id;}
    Object.assign(item,{state:update.state,stage:update.stage,message:update.message,error:update.error,code:update.code});
    const fraction=DONE_STATES.has(item.state)?1:item.state==='running'?stageFraction(item.stage,update.pct??progress.pct,item.id===batch.firstId):null;
    if(fraction!==null)batch.fractions.set(item.id,Math.max(batch.fractions.get(item.id)||0,fraction));
  }
  const finished=batch.items.filter(item=>DONE_STATES.has(item.state)).length,current=batch.items.find(item=>item.state==='running');
  batch.floor=Math.max(batch.floor,(finished+(current?batch.fractions.get(current.id)||0:0))/batch.items.length);
  const seconds=batchEtaSeconds(batch,progress.eta),now=Date.now();
  if(seconds===null)batch.eta=null;
  else if(batch.eta===null||now-batch.etaAt>=15000||Math.abs(seconds-batch.eta)>batch.eta*0.1){batch.eta=seconds;batch.etaAt=now;}
  renderStems();
}
async function continueStemBatch(){
  const batch=stemBatch,remaining=batch.items.filter(item=>item.state!=='prepared'&&item.state!=='reused');
  const result=await run({method:stemsApi.methods.browse,volume:batch.volume,offset:0,query:''},'Reading USB music');
  if(!result?.ok)return;
  stemVolume=batch.volume;resetStemView();applyStemLibrary(stemsApi.library(result.result));
  if(stemLibrary.key===batch.key)await startStemBatch(remaining);else{batch.ending={...batch.ending,kind:'wrongUsb'};batch.dismissed=false;renderStems();}
}
function toggleStemSelection(track,checked){
  if(stemWorkLocked()||!track)return;
  if(checked&&stemSelection.size<batchLimit)stemSelection.set(track.id,track);else if(!checked)stemSelection.delete(track.id);
  dismissEnding();renderStems();
}
async function verifyStemTrack(track,origin){
  const result=await run({method:stemsApi.methods.verify,source:track.source},'Verifying stems: '+track.title,origin);
  if(result?.ok)stemChecks.set(stemSelectionKey+'\n'+track.id,stemsApi.verify(result.result));
  renderStems();
}
async function prepareStemsAgain(track,origin){
  if(!engineReady()){openEngineWizard('check',()=>prepareStemsAgain(track,origin),{label:ST['drawer.prepareAgain']});return;}
  const lib=stemLibrary,result=await run({method:stemsApi.methods.single,volume:lib.volume,expected_identity:lib.identity,file_path:track.filePath,preset:stemEngine.installed},'Preparing stems: '+track.title,origin);
  if(!result?.ok)return;
  stemConfirm=null;stemChecks.delete(lib.key+'\n'+track.id);stemFresh.add(lib.key+'\n'+track.id);
  await browseStemLibrary(lib.volume,lib.offset);
}
const stemActions={
  import:track=>openImportWizard(track),verify:verifyStemTrack,check:()=>browseStemLibrary(stemLibrary.volume,stemLibrary.offset),
  ask:track=>{stemConfirm=track.id;renderStems();},keep:()=>{stemConfirm=null;renderStems();},replace:prepareStemsAgain,
  retry:track=>{const item=batchItem(track.id);if(item)startStemBatch([item]);}
};
$('stem-tracks').onclick=event=>{
  const button=event.target.closest('[data-action]');
  if(button){const track=stemRowsById.get(button.closest('tr').dataset.for);if(track&&!button.disabled)stemActions[button.dataset.action](track,button);return;}
  if(event.target.closest('label,input,tr.st-drawer'))return;
  const row=event.target.closest('tr.st-row');if(!row)return;
  const id=row.dataset.id;if(stemOpen.has(id))stemOpen.delete(id);else stemOpen.add(id);
  if(stemConfirm===id)stemConfirm=null;renderStems();
};
$('stem-tracks').onchange=event=>{const box=event.target.closest('[data-pick]');if(box)toggleStemSelection(stemRowsById.get(box.dataset.pick),box.checked);};
$('st-select-all').onchange=()=>{
  if(stemWorkLocked())return renderStems();
  const tracks=visibleStemTracks().filter(track=>rowState(track)==='missing'&&canSelect(track,'missing'));
  if(tracks.length&&tracks.every(track=>stemSelection.has(track.id)))for(const track of tracks)stemSelection.delete(track.id);
  else for(const track of tracks){if(stemSelection.size>=batchLimit)break;stemSelection.set(track.id,track);}
  dismissEnding();renderStems();
};
$('st-tabs').onclick=event=>{
  const tab=event.target.closest('[data-filter]');if(!tab||tab.disabled||stemFilter===tab.dataset.filter)return;
  stemFilter=tab.dataset.filter;stemOpen.clear();
  if(stemFilter==='selected')renderStems();else browseStemLibrary(stemLibrary.volume,0);
};
$('stem-search').oninput=()=>{
  clearTimeout(stemSearchTimer);
  stemSearchTimer=setTimeout(()=>{stemQuery=$('stem-search').value.trim();if(stemFilter==='selected')renderStems();else if(stemLibrary)browseStemLibrary(stemLibrary.volume,0);},300);
};
$('stem-previous').onclick=()=>{if(stemLibrary)browseStemLibrary(stemLibrary.volume,stemLibrary.offset-stemLibrary.pageSize);};
$('stem-next').onclick=()=>{if(stemLibrary)browseStemLibrary(stemLibrary.volume,stemLibrary.offset+stemLibrary.pageSize);};
$('st-import').onclick=()=>openImportWizard(null);

// Connect a USB: find, choose, check. Same shape as the repair page's connect wizard.
function openStemConnect(){
  $('stem-connect-found').textContent=stemVolume?'Selected: '+driveName(stemVolume):'Nothing selected yet.';
  $('stem-connect-result').textContent='';$('stem-connect').showModal();renderStems();
}
function stemConnectResult(){
  if(stemPhase==='ready'){$('stem-connect').close();return;}
  $('stem-connect-result').textContent={nousb:ST['empty.noUsb.body'],nolib:t('empty.noLib.title',{usb:driveName(stemVolume)})+'. '+ST['empty.noLib.body'],badlib:ST['empty.badLib.title']+'. '+ST['empty.badLib.body'],empty:ST['empty.empty.body']}[stemPhase]||'';
}
$('stem-connect-find').onclick=async()=>{
  const count=await discoverStemDrives();if(count===undefined)return;
  $('stem-connect-found').textContent=count?plural(count,'Rekordbox USB')+' found. Selected: '+driveName(stemVolume):'No Rekordbox USB found yet. Check that it is plugged in and shows on this computer, then find it again, or choose a folder.';
  stemConnectResult();
};
$('stem-connect-choose').onclick=async()=>{const volume=await chooseStemFolder($('stem-connect-choose'));if(!volume)return;$('stem-connect-found').textContent='Selected: '+driveName(volume);stemConnectResult();};
$('stem-connect-check').onclick=async()=>{await browseStemLibrary(stemVolume);stemConnectResult();};
$('stem-connect-close').onclick=()=>$('stem-connect').close();

// Wizards share one frame: a step strip, a title, the body and a footer with one primary.
function stepStrip(labels,current,state='now'){
  return el('ol',{class:'st-steps'},...labels.map((label,index)=>el('li',{dataset:{s:index<current?'done':index===current?state:undefined},text:(index+1)+' '+label})));
}
function renderWizard(dialog,labels,view,titleId){
  const focused=dialog.contains(document.activeElement)?document.activeElement.dataset.key:null;
  dialog.replaceChildren(stepStrip(labels,view.index,view.state),el('h2',{id:titleId,text:view.title}),...view.body.filter(Boolean),el('div',{class:'st-foot'},...view.foot));
  const target=focused&&dialog.querySelector('[data-key="'+CSS.escape(focused)+'"]');if(target)target.focus({preventScroll:true});
}
function stageList(stages,active,progress){
  return el('ul',{class:'st-stages'},...stages.map(([label,detail],index)=>{
    const now=index===active,bar=now&&progress!==null?el('progress',{max:'1'}):null;if(bar)bar.value=progress;
    return el('li',{dataset:{s:index<active?'done':now?'now':undefined}},label,el('em',{text:detail||''}),bar);
  }));
}
function keepWizardOpen(event){if(activeJob!==null)event.preventDefault();}
// Feedback started in a wizard moves to the page when the wizard closes.
function releaseWizardFeedback(dialog){if(!dialog.contains(jobPanel))return;feedbackHome=$('st-bar');positionFeedback();if(feedbackLive)revealFeedback();}
for(const dialog of [$('st-engine'),$('st-import-wiz')]){dialog.addEventListener('cancel',keepWizardOpen);dialog.addEventListener('close',()=>releaseWizardFeedback(dialog));}

function firstSelectedTrack(){return [...stemSelection.values()].find(track=>track.source)||null;}
function testWindow(track){const from=Math.min(45,Math.max(0,(track.duration||30)-30));return {from,to:from+30};}
function openEngineWizard(step,then=null,{label=null,importPending=false}={}){
  eng={step:'check',start:step,then,label,importPending,preset:null,device:null,failure:null,failStep:0,progress:null,result:null,checking:false,track:firstSelectedTrack()};
  $('st-engine').showModal();
  if(step==='choose'&&stemEngine&&!stemEngine.error){eng.preset=stemEngine.installed||stemEngine.recommended;eng.step='choose';renderEngineWizard();}
  else checkEngine();
}
async function checkEngine(){
  eng.step='check';eng.failure=null;eng.checking=true;renderEngineWizard();
  const result=await loadStemEngine();if(!eng)return;eng.checking=false;
  if(!result||!result.ok){eng.failure={...stemsApi.failure(result||{error:'Another task is still running. Wait for it to finish, then check again.'}),code:'check'};eng.failStep=0;eng.step='fail';}
  else{eng.preset=eng.preset||stemEngine.installed||stemEngine.recommended;if(eng.start==='choose')eng.step='choose';}
  renderEngineWizard();renderStems();
}
function enginePreset(id){return stemEngine.presets.find(preset=>preset.id===id)||{id};}
function engineNeedsDownload(){const preset=enginePreset(eng.preset);return !(preset.installed||stemEngine.installed===eng.preset)&&(preset.downloadBytes??stemEngine.download)!==0;}
async function runEngineSetup(){
  eng.step='installing';eng.failure=null;eng.progress=null;renderEngineWizard();
  const request={method:stemsApi.methods.engineSetup,preset:eng.preset};
  if(eng.device)request.device=eng.device;
  if(eng.track){request.test_source=eng.track.source;request.test_start=testWindow(eng.track).from;}
  const state=await run(request,'Setting up the stem engine',$('st-eng-primary'),update=>{
    if(!eng)return;
    eng.progress=stemsApi.setupProgress(update);
    if(['test','load-model','separate-vocals','separate-drums'].includes(eng.progress.stage))eng.step='test';
    renderEngineWizard();
  });
  if(!eng)return renderStems();
  if(state?.ok){eng.result=stemsApi.setupResult(state.result);stemEngine={...stemEngine,installed:eng.result.preset||eng.preset,device:eng.device||stemEngine.device,testSeconds:eng.result.testSeconds??stemEngine.testSeconds,error:''};eng.step='ready';}
  else if(state?.cancelled)eng.step='install';
  else{eng.failure=stemsApi.failure(state||{error:'Another task is still running. Wait for it to finish, then try again.'});eng.failStep=eng.step==='test'||eng.failure.code==='vram'?3:2;eng.step='fail';}
  renderEngineWizard();renderStems();
}
function closeEngineWizard(){$('st-engine').close();}
$('st-engine').addEventListener('close',()=>{
  const pending=eng?.importPending;eng=null;
  if(pending&&imp){$('st-import-wiz').showModal();renderImportWizard();}
});
function engineCard(){
  const plan=stemEngine,card=(icon,name,detail,warn)=>el('div',{class:'st-found',dataset:{tone:warn?'warn':undefined}},el('span',{class:'st-ico','aria-hidden':'true',text:icon}),el('strong',{text:name}),el('span',{text:detail}));
  const cpu=plan.cpuName||'This computer\'s processor',cpuDetail=[plan.cpuCores?plan.cpuCores+' cores':'',plan.ram?wholeGb(plan.ram)+' memory':''].filter(Boolean).join(', ');
  if(plan.device==='cuda')return [ST['eng.gpu.title'],card('▦',plan.deviceName,t('eng.vram',{gb:Math.round(plan.vram/GiB)})),ST['eng.gpu.body']];
  if(plan.device==='mps')return [ST['eng.mps.title'],card('▦',plan.deviceName,'Built-in graphics'+(plan.ram?', '+wholeGb(plan.ram)+' shared memory':'')),ST['eng.mps.body']];
  if(plan.unsupportedGpu)return [ST['eng.badGpu.title'],card('▦',plan.unsupportedGpu,ST['eng.badGpu.card'],true),t('eng.badGpu.body',{cpu})];
  return [ST['eng.cpu.title'],card('⚙',cpu,cpuDetail,true),ST['eng.cpu.body']];
}
function engineFailureView(){
  const failure=eng.failure,detail=failure.detail,close=plainButton('Close',closeEngineWizard,{key:'eng-close'}),primary=(label,action)=>plainButton(label,action,{primary:true,id:'st-eng-primary',key:'eng-primary'});
  const view=(title,bold,line,actions)=>({index:eng.failStep,state:'bad',title,body:[el('div',{class:'st-fail'},el('strong',{text:bold}),line?el('p',{text:line}):null)],foot:actions});
  const retry=()=>runEngineSetup();
  switch(failure.code){
    case 'disk_space':return view(ST['eng.fail.space.title'],t('eng.fail.space.bold',{need:gb(detail.need_bytes||0),drive:detail.drive||'',free:gb(detail.free_bytes||0)}),
      t('eng.fail.space.line',{short:gb(Math.max(0,(detail.need_bytes||0)-(detail.free_bytes||0)))}),[close,primary(ST['eng.checkAgain'],async()=>{eng.start='install';await checkEngine();if(eng.step!=='fail'){eng.step='install';renderEngineWizard();}})]);
    case 'network':return view(ST['eng.fail.net.title'],t('eng.fail.net.bold',{done:gb(detail.done_bytes||0).replace(' GB',''),total:gb(detail.total_bytes||0)}),ST['eng.fail.net.line'],[close,primary(ST['eng.fail.net.action'],retry)]);
    case 'checksum':return view(ST['eng.fail.hash.title'],t('eng.fail.hash.bold',{part:detail.part||'file'}),ST['eng.fail.hash.line'],[close,primary(ST['eng.fail.hash.action'],retry)]);
    case 'driver':return view(ST['eng.fail.driver.title'],t('eng.fail.driver.bold',{min:detail.min_driver||'',have:detail.driver||''}),ST['eng.fail.driver.line'],[
      plainButton(ST['eng.fail.driver.open'],()=>invoke('open_link',{url:'https://www.nvidia.com/Download/index.aspx'}).catch(error=>showError(String(error),$('st-eng-primary'))),{key:'eng-driver'}),
      plainButton(ST['eng.checkAgain'],checkEngine,{key:'eng-check'}),primary(ST['eng.fail.driver.action'],()=>{eng.device='cpu';runEngineSetup();})]);
    case 'vram':return view(ST['eng.fail.vram.title'],t('eng.fail.vram.bold',{preset:presetName(eng.preset),need:wholeGb(detail.need_bytes||0),free:wholeGb(detail.free_bytes||0)}),ST['eng.fail.vram.line'],
      [close,primary(ST['eng.fail.vram.action'],()=>{eng.preset='fast';runEngineSetup();})]);
    default:{
      const check=failure.code==='check';
      return view(ST['eng.fail.test.title'],failure.error||'The stem engine stopped without a reason.',failure.hint,[
        plainButton(ST['eng.fail.report'],saveEngineReport,{key:'eng-report',id:'st-eng-report'}),primary(check?ST['eng.checkAgain']:ST['eng.fail.test.action'],check?checkEngine:retry)]);
    }
  }
}
async function saveEngineReport(){await run({method:stemsApi.methods.engineReport},'Saving a report',$('st-eng-report'));}
function engineView(){
  const plan=stemEngine,next=(label,action,disabled)=>plainButton(label,action,{primary:true,id:'st-eng-primary',key:'eng-primary',disabled}),cancel=plainButton(ST['action.cancel'],eng.step==='check'?closeEngineWizard:cancelActiveJob,{key:'eng-cancel'});
  const progress=eng.progress||{};
  switch(eng.step){
    case 'check':{
      if(eng.checking||!plan)return {index:0,title:'Check this computer',body:[],foot:[cancel,next('Next',null,true)]};
      const [title,card,body]=engineCard();
      return {index:0,title,body:[card,el('p',{text:body})],foot:[cancel,next('Next',()=>{eng.step='choose';renderEngineWizard();})]};
    }
    case 'choose':{
      const presets=el('div',{class:'st-presets',role:'radiogroup','aria-label':'Quality'},...plan.presets.filter(preset=>PRESETS[preset.id]).map(preset=>{
        const minutes=preset.secondsPerTrack?Math.max(1,Math.round(preset.secondsPerTrack/60)):null;
        const line=[ST['eng.preset.'+preset.id+'.line'],minutes?t('eng.preset.time',{n:minutes}).replace('1 minutes','1 minute'):''].filter(Boolean).join(' ');
        return el('label',{class:'st-preset'},el('input',{type:'radio',name:'st-preset',value:preset.id,checked:eng.preset===preset.id,dataset:{key:'preset:'+preset.id},onchange:()=>{eng.preset=preset.id;}}),
          el('strong',{text:ST['eng.preset.'+preset.id]}),preset.id===plan.recommended?el('span',{class:'st-rec',text:ST['eng.recommended']}):null,el('span',{text:line}),el('small',{text:PRESETS[preset.id].models}));
      }));
      return {index:1,title:ST['eng.choose.title'],body:[presets,el('p',{class:'fine',text:ST['eng.choose.note']})],
        foot:[plainButton('Back',()=>{eng.step='check';renderEngineWizard();},{key:'eng-back'}),next('Next',()=>{if(engineNeedsDownload()){eng.step='install';renderEngineWizard();}else runEngineSetup();})]};
    }
    case 'install':{
      const preset=enginePreset(eng.preset),bytes=preset.downloadBytes??plan.download,parts=el('dl',{class:'st-kv'});
      for(const part of plan.parts)parts.append(el('dt',{text:part.id==='runtime'?ST['eng.install.runtime']:t('eng.install.models',{preset:presetName(eng.preset)})}),el('dd',{text:part.bytes>=GiB?gb(part.bytes):mb(part.bytes)}));
      if(plan.drive&&plan.driveFree!==null)parts.append(el('dt',{text:t('eng.install.space',{drive:plan.drive})}),el('dd',{text:Math.round(plan.driveFree/GiB)+' GB free'}));
      return {index:2,title:ST['eng.install.title'],body:[bytes?el('p',{},...emphasis(t('eng.install.body',{bytes:'\u0000'}),gb(bytes))):null,parts.children.length?parts:null,el('p',{class:'fine',text:ST['eng.install.verify']})],
        foot:[plainButton('Back',()=>{eng.step='choose';renderEngineWizard();},{key:'eng-back'}),next(ST['eng.install.action'],runEngineSetup)]};
    }
    case 'installing':{
      const active=['runtime','models','check'].indexOf({download:'models',verify:'check'}[progress.stage]||progress.stage),size=part=>plan.parts.find(item=>item.id===part)?.bytes;
      const detail=index=>index===active&&progress.total?ofText(progress.done||0,progress.total)+(progress.eta!==null?' · '+etaText(progress.eta):''):index<active&&size(['runtime','models'][index])?gb(size(['runtime','models'][index])):'';
      return {index:2,title:ST['eng.installing.title'],body:[stageList([[ST['eng.install.runtime'],detail(0)],[t('eng.install.models',{preset:presetName(eng.preset)}),detail(1)],[ST['eng.installing.check'],detail(2)]],Math.max(0,active),progress.pct===null||progress.pct===undefined?null:progress.pct/100),
        el('p',{class:'fine',text:ST['eng.installing.note']})],foot:[cancel,next('Next',null,true)]};
    }
    case 'test':{
      const window=eng.track&&testWindow(eng.track),active=Math.max(0,['load-model','separate-vocals','separate-drums'].indexOf(progress.stage));
      return {index:3,title:ST['eng.test.title'],body:[el('p',{},...(eng.track?emphasis(t('eng.test.body',{from:clock(window.from),to:clock(window.to),title:'\u0000'}),eng.track.title):[ST['eng.test.sample']])),
        stageList([[ST['stage.load-model']],[ST['stage.separate-vocals']],[ST['stage.separate-drums']]],active,progress.pct===null||progress.pct===undefined?null:progress.pct/100)],foot:[cancel,next('Next',null,true)]};
    }
    case 'ready':{
      const seconds=eng.result?.testSeconds,estimate=seconds?(seconds*10<90?'a minute':Math.round(seconds*10/60)+' minutes'):'';
      const text=eng.label?t('eng.ready.body',{s:'\u0000',estimate}).replace(' Have a listen:',''):t('eng.ready.noSel',{s:'\u0000',estimate});
      const device=stemEngine.device==='cpu'?'this processor':stemEngine.deviceName||'this computer';
      const finish=()=>{const then=eng.then;closeEngineWizard();then?.();};
      return {index:4,title:ST['eng.ready.title'],body:[seconds?el('p',{},...emphasis(text,seconds.toFixed(1))):null,el('p',{class:'fine',text:presetName(stemEngine.installed)+' on '+device})],
        foot:[eng.label?plainButton('Close',closeEngineWizard,{key:'eng-close'}):null,next(eng.label||ST['eng.done'],finish)]};
    }
    default:return engineFailureView();
  }
}
function renderEngineWizard(){if(eng)renderWizard($('st-engine'),ST['eng.steps'],engineView(),'st-engine-title');}

function openImportWizard(track){
  imp={step:'add',track,files:[],plan:null,groups:[],weak:false,failure:null,planError:'',progress:null,result:null,query:'',matches:[],stageIndex:0};
  $('st-import-wiz').showModal();renderImportWizard();
}
function addImportFiles(paths){
  for(const path of paths)if(imp.files.length<128&&!imp.files.some(item=>item.toLowerCase()===path.toLowerCase()))imp.files.push(path);
  imp.planError='';renderImportWizard();
}
async function pickImportFiles(){try{addImportFiles(await invoke('pick_stem_files'));}catch(error){showError(String(error),$('st-imp-primary'));}}
function importDropLabel(){
  const zip=imp.files.find(path=>/\.zip$/i.test(path)),name=path=>path.split(/[\\/]/).pop();
  return zip?name(zip):imp.files.length===1?name(imp.files[0]):imp.files[0].split(/[\\/]/).slice(-2,-1)[0]+' · '+imp.files.length+' files';
}
function importTrackLine(){
  const track=imp.track;
  if(!track)return null;
  return el('div',{class:'st-target'},...emphasis(t('imp.for',{title:'\u0000'}),track.title),el('small',{text:[track.artist,clock(track.duration)].filter(Boolean).join(' · ')}),
    imp.step==='add'?plainButton(ST['imp.changeTrack'],()=>{imp.track=null;imp.plan=null;renderImportWizard();},{text:true,key:'imp-change'}):null);
}
function importMatches(){
  const query=imp.query.toLowerCase(),pool=new Map([...(stemLibrary?.tracks||[]),...imp.matches].map(track=>[track.id,track]));
  return [...pool.values()].filter(track=>!query||(track.title+' '+track.artist).toLowerCase().includes(query)).slice(0,6).map(track=>
    plainButton(track.title+(track.artist?' · '+track.artist:''),()=>{imp.track=track;renderImportWizard();},{key:'imp-match:'+track.id}));
}
async function searchImportTracks(){
  const result=await run({method:stemsApi.methods.browse,volume:stemLibrary.volume,offset:0,query:imp.query},'Reading USB music');
  if(result?.ok&&imp){imp.matches=stemsApi.library(result.result).tracks;$('st-imp-matches')?.replaceChildren(...importMatches());}
}
function importPicker(){
  const input=el('input',{type:'search',placeholder:ST['search.placeholder'],value:imp.query,dataset:{key:'imp-find'},'aria-label':ST['imp.which']});
  input.oninput=()=>{imp.query=input.value;$('st-imp-matches').replaceChildren(...importMatches());};
  input.onkeydown=event=>{if(event.key==='Enter')searchImportTracks();};
  return [el('div',{class:'st-target'},el('b',{text:ST['imp.which']})),el('label',{class:'st-search'},input),el('div',{class:'st-matches',id:'st-imp-matches'},...importMatches())];
}
function unplacedFiles(){return imp.groups.filter(group=>group===null).length;}
function importOutcome(){
  const plan=imp.plan,verdict=plan.alignment?.verdict;
  if(verdict==='mismatch')return 'wrong';
  if(verdict==='weak'&&!imp.weak)return 'mis';
  if(unplacedFiles())return 'review';
  if(plan.needsDrumSplit)return !stemsSupported()?'twoUnsupported':engineReady()?'twoReady':'twoSetup';
  return 'auto';
}
function importSlots(){
  const plan=imp.plan,names=group=>plan.files.filter((file,index)=>imp.groups[index]===group).map(file=>file.label||file.name);
  return el('div',{class:'st-slots'},...['vocals','drums','harmonics'].map(group=>{
    const fromInstrumental=plan.needsDrumSplit&&group!=='vocals'&&!names(group).length,list=fromInstrumental?[ST[group==='drums'?'imp.slot.drumsFromInst':'imp.slot.harmFromInst']]:names(group);
    return el('div',{class:'st-slot',dataset:{s:fromInstrumental?(engineReady()?undefined:'engine'):list.length?undefined:'empty'}},el('h3',{text:ST['imp.slot.'+group]}),el('p',{text:list.length?list.join(', '):ST['imp.slot.empty']}));
  }));
}
function confidenceWord(file,group){
  if(file.group==='mix'&&group==='mix')return [ST['imp.conf.mix'],'info'];
  if(group!==file.group)return ['',undefined];
  return file.confidence>=0.9?[ST['imp.conf.high']]:file.confidence>=0.6?[ST['imp.conf.mid'],'mid']:[ST['imp.conf.low'],'low'];
}
function importFilesTable(){
  const rows=imp.plan.files.map((file,index)=>{
    const group=imp.groups[index],select=el('select',{'aria-label':ST['imp.col.to']+': '+file.name,dataset:{key:'imp-group:'+index}},
      group===null?el('option',{value:'',text:ST['imp.to.choose']}):null,...['vocals','drums','harmonics'].map(id=>el('option',{value:id,text:ST['imp.slot.'+id]})),
      file.group==='instrumental'?el('option',{value:'instrumental',text:file.label||'Instrumental'}):null,el('option',{value:'mix',text:ST['imp.to.leaveOut']}));
    select.value=group??'';select.onchange=()=>{imp.groups[index]=select.value||null;renderImportWizard();};
    const [word,tone]=confidenceWord(file,group);
    return el('tr',{'data-need':group===null?'':null},el('td',{text:file.name,title:file.name}),el('td',{},select),el('td',{},word?el('span',{class:'st-conf',dataset:{c:tone},text:word}):null));
  });
  return el('div',{class:'st-scroll'},el('table',{class:'st-files'},el('colgroup',{},el('col'),el('col',{class:'f-to'}),el('col',{class:'f-conf'})),
    el('thead',{},el('tr',{},el('th',{text:ST['imp.col.file']}),el('th',{text:ST['imp.col.to']}),el('th',{text:ST['imp.col.sure']}))),el('tbody',{},...rows)));
}
function leadNote(text,tone){const [lead,...rest]=text.split('. ');return el('div',{class:'st-note',dataset:{tone}},el('strong',{text:lead+(rest.length?'.':'')}),rest.length?' '+rest.join('. '):null);}
function alignmentNote(){
  const plan=imp.plan,alignment=plan.alignment;if(!alignment||alignment.verdict==='mismatch'||alignment.verdict==='weak'&&!imp.weak)return null;
  const mp3=plan.files.some((file,index)=>/\.mp3$/i.test(file.name)&&imp.groups[index]!=='mix'),stems=plan.files.filter((file,index)=>imp.groups[index]!=='mix'&&file.seconds!==null).map(file=>file.seconds);
  const gap=plan.fitLength&&stems.length?plan.trackSeconds-Math.max(...stems):0;
  const fit=Math.abs(gap)>=0.05?' '+t(gap>0?'imp.fitShort':'imp.fitLong',{s:Math.abs(gap).toFixed(1)}):'';
  const note=leadNote(t(mp3?'imp.alignedMp3':'imp.aligned',{ms:Math.round(Math.abs(alignment.offsetMs))}),'ok');if(fit)note.append(fit);
  return note;
}
function importCheckView(){
  const plan=imp.plan,outcome=importOutcome(),back=plainButton('Back',()=>{imp.step='add';renderImportWizard();},{key:'imp-back'}),next=(label,action,disabled)=>plainButton(label,action,{primary:true,id:'st-imp-primary',key:'imp-primary',disabled});
  const otherFiles=()=>{imp.files=[];imp.plan=null;imp.weak=false;imp.step='add';renderImportWizard();};
  const residual=plan.alignment?.residualDb!==null&&plan.alignment?.residualDb!==undefined?el('span',{class:'st-foot-left',text:t('imp.residual',{db:minus(plan.alignment.residualDb)})}):null;
  const toWrite=()=>{imp.step='write';renderImportWizard();};
  switch(outcome){
    case 'wrong':return {index:1,state:'bad',title:ST['imp.wrong.title'],body:[importTrackLine(),leadNote(ST['imp.wrong.body'],'bad')],
      foot:[plainButton(ST['imp.mis.action'],otherFiles,{key:'imp-other'}),next(ST['imp.wrong.action'],()=>{imp.track=null;imp.plan=null;imp.step='add';renderImportWizard();})]};
    case 'mis':return {index:1,state:'bad',title:ST['imp.mis.title'],body:[importSlots(),leadNote(t('imp.mis.body',{db:minus(plan.alignment.residualDb??0)}),'bad')],
      foot:[plainButton(ST['imp.mis.anyway'],()=>{imp.weak=true;toWrite();},{key:'imp-anyway'}),next(ST['imp.mis.action'],otherFiles)]};
    case 'review':{const n=unplacedFiles();return {index:1,title:one(t('imp.review.title',{n})),body:[importSlots(),importFilesTable(),el('p',{class:'fine',text:ST['imp.review.tip']})],foot:[back,next(one(t('imp.review.action',{n})),null,true)]};}
    case 'twoSetup':return {index:1,title:ST['imp.two.setup.title'],body:[importSlots(),leadNote(ST['imp.two.setup.body'],'setup'),alignmentNote(),el('p',{class:'fine',text:ST['imp.two.resume']})],
      foot:[back,next(ST['action.setupEngine'],()=>{openEngineWizard('check',null,{importPending:true});$('st-import-wiz').close();})]};
    case 'twoUnsupported':return {index:1,title:ST['imp.two.setup.title'],body:[importSlots(),el('div',{class:'st-note',dataset:{tone:'warn'},text:ST['engine.unsupportedHost']})],foot:[back,plainButton(ST['imp.mis.action'],otherFiles,{key:'imp-other'})]};
    default:{
      const seconds=plan.drumSplitSeconds??(stemEngine?.testSeconds&&plan.trackSeconds?Math.round(stemEngine.testSeconds*plan.trackSeconds/30):null);
      const split=outcome==='twoReady'?leadNote(seconds?t('imp.two.ready',{s:seconds}):t('imp.two.ready',{s:''}).replace(', about  seconds',''),undefined):null;
      return {index:1,title:ST['imp.auto.title'],body:[importSlots(),plan.needsDrumSplit?null:importFilesTable(),split,alignmentNote()],foot:[residual,back,next('Next',toWrite)]};
    }
  }
}
function importWriteRequest(){
  const plan=imp.plan,lib=stemLibrary,files=plan.files.map((file,index)=>({path:file.path,group:imp.groups[index]})).filter(file=>file.group&&file.group!=='mix');
  return {method:stemsApi.methods.write,...plan.request,files,volume:lib.volume,expected_identity:lib.identity,file_path:imp.track.filePath,title:imp.track.title,fit_length:plan.fitLength,accept_weak_alignment:imp.weak};
}
async function planImport(){
  imp.step='planning';imp.planError='';imp.progress=null;renderImportWizard();
  const lib=stemLibrary,track=imp.track;
  const state=await run({method:stemsApi.methods.plan,volume:lib.volume,expected_identity:lib.identity,file_path:track.filePath,title:track.title,files:imp.files.map(path=>({path}))},
    'Checking stems',$('st-imp-primary'),update=>{if(imp){imp.progress=update;renderImportWizard();}});
  if(!imp)return;
  if(state?.ok){imp.plan=stemsApi.importPlan(state.result);imp.groups=imp.plan.files.map(file=>file.group);imp.weak=false;imp.step='check';}
  else{imp.step='add';imp.planError=state?.cancelled?'':state?.error||'Another task is still running. Wait for it to finish, then try again.';}
  renderImportWizard();
}
async function writeImport(){
  imp.step='writing';imp.failure=null;imp.stageIndex=0;renderImportWizard();
  const track=imp.track,key=stemLibrary.key+'\n'+track.id,stages={setup:0,import:0,align:0,split:1,render:1,page:2,pack:2,publish:2,write:2,verify:3};
  const state=await run(importWriteRequest(),'Writing stems: '+track.title,$('st-imp-primary'),update=>{if(!imp)return;imp.stageIndex=Math.max(imp.stageIndex,stages[update.stage]??imp.stageIndex);imp.progress=update;renderImportWizard();});
  if(!imp)return;
  if(state?.ok){
    imp.result=stemsApi.importDone(state.result);imp.step='done';stemFresh.add(key);stemChecks.delete(key);
    if(imp.result.check)stemChecks.set(key,imp.result.check);
    await browseStemLibrary(stemLibrary.volume,stemLibrary.offset);
    imp.result.bytes??=stemLibrary?.tracks.find(item=>item.id===track.id)?.storedBytes??null;
  }else if(state?.cancelled)imp.step='write';
  else{imp.failure=stemsApi.importFailure(state||{error:'Another task is still running. Wait for it to finish, then try again.'});imp.step='fail';}
  renderImportWizard();
}
function importView(){
  const close=plainButton(ST['action.cancel'],()=>$('st-import-wiz').close(),{key:'imp-close'}),next=(label,action,disabled)=>plainButton(label,action,{primary:true,id:'st-imp-primary',key:'imp-primary',disabled});
  const track=imp.track,usb=driveName(stemLibrary.volume);
  switch(imp.step){
    case 'add':{
      const drop=el('div',{class:'st-drop',tabindex:'0',role:'button','aria-label':'Add stem files or a ZIP',dataset:{key:'imp-drop'},onclick:pickImportFiles,onkeydown:event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();pickImportFiles();}}},
        el('strong',{text:ST['imp.drop']}),el('span',{text:ST['imp.dropHint']}),plainButton(ST['imp.choose'],event=>{event.stopPropagation();pickImportFiles();},{key:'imp-choose'}));
      return {index:0,title:ST['imp.title'],body:[imp.files.length?el('div',{class:'st-note',dataset:{tone:'ok'}},el('strong',{text:importDropLabel()})):null,...(track?[importTrackLine()]:imp.files.length?importPicker():[]),
        imp.files.length?null:drop,imp.planError?el('div',{class:'st-fail'},el('strong',{text:imp.planError})):null],foot:[close,next('Next',planImport,!imp.files.length||!track||stemWorkLocked())]};
    }
    case 'planning':return {index:1,title:ST['imp.title'],body:[importTrackLine(),stageList([[ST['imp.writing.align'],imp.progress?.message||'']],0,null)],foot:[plainButton(ST['action.cancel'],cancelActiveJob,{key:'imp-cancel'}),next('Next',null,true)]};
    case 'check':return importCheckView();
    case 'write':{
      const bytes=track.estimate.bytes,replacing=track.status==='present';
      return {index:2,title:ST['imp.write.title'],body:[el('dl',{class:'st-kv'},el('dt',{text:'Track'}),el('dd',{text:[track.title,track.artist].filter(Boolean).join(' · ')}),el('dt',{text:'USB'}),el('dd',{text:usb}),
        el('dt',{text:'Size'}),el('dd',{text:replacing&&track.storedBytes!==null?t('imp.write.sizeReplace',{bytes:mb(bytes),old:mb(track.storedBytes)}):t('imp.write.size',{bytes:mb(bytes),remaining:gb(stemLibrary.free-bytes)})})),
        el('p',{text:ST[replacing?'imp.write.replaceBody':'imp.write.body']})],
        foot:[plainButton('Back',()=>{imp.step='check';imp.weak=false;renderImportWizard();},{key:'imp-back'}),next(ST[replacing?'imp.write.replace':'imp.write.action'],writeImport,stemWorkLocked())]};
    }
    case 'writing':{
      const pct=imp.progress?.pct;
      return {index:2,title:ST['imp.writing.title'],body:[stageList([[ST['imp.writing.align']],[ST['stage.render']],[ST['stage.pack']],[ST['stage.verify']]].map(([label],index)=>[label,index===imp.stageIndex&&pct!==null&&pct!==undefined?Math.round(pct)+'%':'']),imp.stageIndex,pct===null||pct===undefined?null:pct/100)],
        foot:[plainButton(ST['action.cancel'],cancelActiveJob,{key:'imp-cancel'}),next(ST['eng.done'],null,true)]};
    }
    case 'done':{
      const result=imp.result;
      return {index:3,title:ST['imp.done.title'],body:[result.check?verifyChips(result.check):null,el('p',{},...emphasis(t('imp.done.body',{title:'\u0000',bytes:mb(result.bytes??track.estimate.bytes)}),track.title))],
        foot:[plainButton(ST['imp.done.another'],()=>openImportWizard(null),{key:'imp-another'}),next(ST['eng.done'],()=>$('st-import-wiz').close())]};
    }
    default:{
      const failure=imp.failure,again=next(ST['imp.fail.usb.action'],writeImport,stemWorkLocked()),closeButton=plainButton('Close',()=>$('st-import-wiz').close(),{key:'imp-close'});
      const fail=(title,bold,line,primary)=>({index:2,state:'bad',title,body:[el('div',{class:'st-fail'},el('strong',{text:bold}),line?el('p',{text:line}):null)],foot:[closeButton,primary]});
      if(failure.code==='usb_full')return fail(t('imp.fail.space.title',{usb}),t('imp.fail.space.body',{need:mb(track.estimate.bytes),free:mb(stemLibrary.free)}),'',
        next(ST['eng.checkAgain'],async()=>{await browseStemLibrary(stemLibrary.volume,stemLibrary.offset);if(imp){imp.step='write';renderImportWizard();}}));
      if(failure.code==='usb_missing')return fail(ST['imp.fail.usb.title'],ST['imp.fail.usb.bold'],t('imp.fail.usb.line',{usb}),again);
      return fail(ST['imp.title'],failure.error||'The stems were not written.',failure.hint,again);
    }
  }
}
function renderImportWizard(){if(imp&&$('st-import-wiz').open)renderWizard($('st-import-wiz'),ST['imp.steps'],importView(),'st-import-title');}
$('st-import-wiz').addEventListener('close',()=>{if(!eng?.importPending)imp=null;});
if(invoke)window.__TAURI__.webviewWindow?.getCurrentWebviewWindow().onDragDropEvent(event=>{
  const payload=event.payload,wizard=$('st-import-wiz');
  wizard.querySelector('.st-drop')?.classList.toggle('drag-over',payload.type==='over');
  if(payload.type!=='drop'||activeJob!==null||appUpdating)return;
  if(wizard.open&&imp?.step==='add')addImportFiles(payload.paths);
  else if(!wizard.open&&!$('stems').classList.contains('hidden')&&stemPhase==='ready'){openImportWizard(null);addImportFiles(payload.paths);}
}).catch(()=>{});

// Library repair. Checking only reads. Repair and undo send back the identity and file hashes from the check
// they were planned on, so the backend refuses if the library changed in between.
const repairVerdicts={
  healthy:{badge:'No damage found',text:'No structural damage found in the checked tables. Rekordbox and player acceptance still need testing.'},
  repairable:{badge:'Repair available',text:'Some playback history pages are cut off by blank pages. XZ Mods can reconnect the intact pages. History entries that are already missing cannot be recovered.'},
  not_repairable:{badge:'Cannot repair here',text:'This library has damage outside the supported unused-page and playback-history repairs. Save a report to keep the details.'},
  no_library:{badge:'No library',text:'No Rekordbox library was found here. Choose the USB root or a folder that contains PIONEER.'}
};
const repairCountLabels=[['tracks','Tracks'],['playlists','Playlists'],['stem_sets','Stem sets']];
function repairVerdictText(data){return (data?.verdict==='repairable'&&data.repair?.description)||repairVerdicts[data?.verdict]?.text||'XZ Mods could not classify this library. Save a report to keep the details.';}
function repairWritable(){return Boolean(repairInspection)&&!repairInspection.rekordbox_running;}
function restorableBackups(data){return (data?.backups||[]).filter(backup=>backup&&backup.restorable===true);}
function selectedRepairBackup(){return restorableBackups(repairInspection).find(item=>item.id===$('repair-backups').value);}
function formatBytes(bytes){return bytes>=1048576?(bytes/1048576).toFixed(1)+' MB':bytes>=1024?(bytes/1024).toFixed(1)+' KB':bytes+' bytes';}
function formatCreated(value){const date=new Date(value);return isNaN(date)?String(value):date.toLocaleString();}
function updateRepairControls(locked=false){
  locked=locked||activeJob!==null||appUpdating||!invoke;
  document.querySelectorAll('.repair-control').forEach(control=>control.disabled=locked);
  const volume=$('repair-drive').value,data=repairInspection,none=!$('repair-drive').options.length;
  $('inspect-library').classList.toggle('hidden',none);$('repair-connect-open').classList.toggle('hidden',!none);
  $('inspect-library').disabled=locked||!volume;$('repair-connect-check').disabled=locked||!volume;
  $('recheck-library').disabled=locked||!data;$('save-library-report').disabled=locked||!data;
  $('repair-library').classList.toggle('hidden',Boolean(data?.rekordbox_running));
  $('repair-library').disabled=locked||!repairWritable()||data.verdict!=='repairable'||!data.repair;
  $('restore-library-backup').disabled=locked||!repairWritable()||!selectedRepairBackup();
  $('repair-undo-go').disabled=locked||!repairWritable()||!selectedRepairBackup();
}
function clearRepair(){
  repairInspection=null;
  for(const id of ['repair-inspection','repair-plan','repair-outcome','repair-undo'])$(id).classList.add('hidden');
  $('repair-report-path').textContent='';$('repair-backups').replaceChildren();
  updateRepairControls();
}
function renderRepairInspection(data){
  if(data?.format!=='xz-library-inspected/1'){clearRepair();showError('The library check returned a result this app cannot read. Check again before changing this USB.',$('repair-drive-status'));return false;}
  repairInspection=data;$('repair-inspection').classList.remove('hidden');
  $('repair-volume').textContent=data.volume;
  const verdict=repairVerdicts[data.verdict],badge=$('repair-verdict-badge');
  badge.textContent=verdict?.badge||'Unknown';badge.dataset.verdict=verdict?data.verdict:'unknown';
  $('repair-verdict').textContent=repairVerdictText(data);
  const counts=$('repair-counts');counts.replaceChildren();
  if(data.counts&&data.verdict!=='no_library')for(const [key,label] of repairCountLabels)if(Number.isFinite(data.counts[key])){const chip=node('li');chip.append(node('strong',String(data.counts[key])),document.createTextNode(label));counts.append(chip);}
  counts.classList.toggle('hidden',!counts.children.length);
  const findings=$('repair-findings');findings.replaceChildren();
  for(const finding of data.findings||[]){const item=node('li',finding.summary||finding.code);if(finding.code)item.append(node('small',finding.code));findings.append(item);}
  findings.classList.toggle('hidden',!findings.children.length);
  const files=$('repair-files');files.replaceChildren();
  for(const [path,info] of Object.entries(data.expected_files||{})){
    const item=node('li');item.append(node('strong',path),node('span',info?.exists?formatBytes(info.bytes):'Not found'));
    if(info?.sha256)item.append(node('small','SHA-256 '+info.sha256));files.append(item);
  }
  const plan=data.verdict==='repairable'&&data.repair,restorable=restorableBackups(data);
  $('repair-rekordbox-block').classList.toggle('hidden',!data.rekordbox_running||!(plan||restorable.length));
  $('repair-no-library').classList.toggle('hidden',data.verdict!=='no_library');
  $('repair-plan').classList.toggle('hidden',!plan);
  if(plan){
    $('repair-plan-title').textContent=data.repair.title||'Reconnect playback history pages';
    for(const [id,list] of [['repair-changes',data.repair.changes],['repair-keeps',data.repair.keeps]]){const holder=$(id);holder.replaceChildren();for(const text of list||[])holder.append(node('li',text));}
    $('repair-history-warning').textContent=data.repair.history_warning||'History entries that are already missing cannot be recovered.';
    $('repair-backup-root').textContent=data.backup_root?'The current library files are backed up to '+data.backup_root+' before anything is written.':'';
  }
  const select=$('repair-backups'),previous=select.value;select.replaceChildren();
  for(const backup of restorable){const option=node('option','Repair on '+formatCreated(backup.created));option.value=backup.id;select.append(option);}
  if(restorable.some(backup=>backup.id===previous))select.value=previous;
  $('repair-undo').classList.toggle('hidden',!restorable.length);
  const others=(data.backups||[]).length-restorable.length;
  $('repair-backup-other').textContent=others?plural(others,'other backup')+' cannot be undone from here. '+(others===1?'It is':'They are')+' kept'+(data.backup_root?' in '+data.backup_root:'')+'.':'';
  $('repair-backup-folder').textContent=selectedRepairBackup()?.folder||'';
  updateRepairControls();return true;
}
function renderRepairOutcome(kind,result){
  const panel=$('repair-outcome');panel.classList.remove('hidden');
  $('repair-outcome-title').textContent=kind==='repair'?'Repair written':'Repair undone';
  $('repair-outcome-summary').textContent=result.summary||'';
  $('repair-outcome-backup').textContent=result.backup?.folder?(kind==='repair'?'Backup of the library before this repair: ':'Backup of the library replaced during undo: ')+result.backup.folder:'';
  const rows=$('repair-compare');rows.replaceChildren();const changed=[],warnings=[];
  if(kind==='repair'&&result.before&&result.after)for(const [key,label] of repairCountLabels){
    const before=result.before[key],after=result.after[key];if(before===undefined&&after===undefined)continue;
    const row=node('tr');row.append(node('td',label),node('td',String(before??'—')),node('td',String(after??'—')));
    if(before!==after){row.classList.add('changed');changed.push(label+' '+(before??'—')+' → '+(after??'—'));}rows.append(row);
  }
  panel.querySelector('.repair-compare-wrap').classList.toggle('hidden',!rows.children.length);
  if(kind==='repair'){
    if(!rows.children.length)warnings.push('The repair did not report library counts, so they could not be compared.');
    if(changed.length)warnings.push('Library counts changed: '+changed.join(', ')+'. This repair should not change them. Undo this repair and save a report.');
    if(result.inspection?.verdict!=='healthy')warnings.push('The check after the repair reports: '+repairVerdictText(result.inspection));
  }
  $('repair-outcome-warning').textContent=warnings.join(' ');$('repair-outcome-warning').classList.toggle('hidden',!warnings.length);
  $('repair-outcome-note').textContent=kind==='restore'?'The library is back to how it was before that repair, including the history damage.':result.rekordbox_verified===true?'':'Rekordbox has not checked this repair. Open the USB in Rekordbox and load tracks on the XZ before you use it in a set.';
  // The control that started the work may now be hidden, so the feedback joins the outcome.
  feedbackHome=$('repair-outcome-summary');positionFeedback();panel.scrollIntoView({behavior:'smooth',block:'nearest'});
}
function addRepairFolderOption(volume){const option=node('option',volume);option.value=volume;option.dataset.chosen='1';$('repair-drive').append(option);}
function selectRepairVolume(volume){
  const select=$('repair-drive');
  if(![...select.options].some(option=>option.value===volume))addRepairFolderOption(volume);
  select.value=volume;if(repairInspection?.volume!==volume)clearRepair();
  $('repair-drive-status').textContent='Ready to check '+volume+'.';updateRepairControls();
}
async function discoverRepairDrives(){
  if(!invoke){updateRepairControls();return;}
  const result=await run({method:'discover_stem_usbs'},'Finding Rekordbox USBs');
  if(!result){repairDiscovery=null;return;}
  if(!result.ok){$('repair-drive-status').textContent=result.error||'Rekordbox USBs could not be listed. Reconnect the USB, then choose Find USBs.';updateRepairControls();return;}
  const select=$('repair-drive'),previous=select.value,chosen=[...select.options].filter(option=>option.dataset.chosen).map(option=>option.value);
  select.replaceChildren();
  for(const drive of result.result.drives){const option=node('option',drive.label+' · '+drive.volume);option.value=drive.volume;select.append(option);}
  for(const volume of chosen)if(![...select.options].some(option=>option.value===volume))addRepairFolderOption(volume);
  if([...select.options].some(option=>option.value===previous))select.value=previous;
  if(select.value!==repairInspection?.volume)clearRepair();
  const count=result.result.drives.length;
  $('repair-drive-status').textContent=count?plural(count,'Rekordbox USB')+' found. Choose one, then check its library.':select.options.length?'No Rekordbox USB found. You can check the chosen folder.':'No Rekordbox USB found. Connect the USB, then find it again.';
  updateRepairControls();return count;
}
async function pickRepairFolder(origin){
  try{const volume=await invoke('pick_path',{kind:'folder'});if(volume){selectRepairVolume(volume);return volume;}}
  catch(error){showError(String(error),origin);}
  return null;
}
async function inspectLibrary(origin){
  const volume=$('repair-drive').value;if(!volume){openRepairConnect();return null;}
  const result=await run({method:'inspect_device_library',volume},'Checking the Rekordbox library',origin);
  // A result for a USB that is no longer selected is stale.
  if(!result?.ok||$('repair-drive').value!==volume)return null;
  return renderRepairInspection(result.result)?result.result:null;
}
$('repair-drive').onchange=()=>selectRepairVolume($('repair-drive').value);
$('choose-repair-drive').onclick=()=>pickRepairFolder($('choose-repair-drive'));
$('find-repair-drives').onclick=()=>{repairDiscovery=discoverRepairDrives();};
$('inspect-library').onclick=()=>inspectLibrary($('inspect-library'));
$('recheck-library').onclick=()=>inspectLibrary($('recheck-library'));
$('repair-backups').onchange=()=>{$('repair-backup-folder').textContent=selectedRepairBackup()?.folder||'';updateRepairControls();};
$('save-library-report').onclick=async()=>{
  const data=repairInspection;if(!data)return;
  const result=await run({method:'save_device_library_report',volume:data.volume},'Saving a library report',$('save-library-report'));
  if(result?.ok&&result.result?.report_path&&repairInspection===data)$('repair-report-path').textContent='Report saved to '+result.result.report_path;
};
$('repair-library').onclick=async()=>{
  const data=repairInspection;if(!data||data.verdict!=='repairable'||!data.repair||data.rekordbox_running)return;
  const origin=$('repair-library');
  const result=await run({method:'repair_device_library',volume:data.volume,expected_identity:data.identity,expected_files:data.expected_files,repair_id:data.repair.id},'Backing up and repairing the library',origin);
  if(!result?.ok)return;
  if(result.result?.format!=='xz-library-repaired/1'){showError('The repair returned a result this app cannot read. Check the library again before you use this USB.',origin);return;}
  renderRepairInspection(result.result.inspection);renderRepairOutcome('repair',result.result);
};
$('restore-library-backup').onclick=()=>{
  const backup=selectedRepairBackup();if(!backup||!repairWritable())return;
  $('repair-undo-text').textContent='This puts back the library files saved before the repair on '+formatCreated(backup.created)+'.';
  $('repair-undo-confirm').showModal();updateRepairControls();
};
$('repair-undo-cancel').onclick=()=>$('repair-undo-confirm').close();
$('repair-undo-go').onclick=async()=>{
  const data=repairInspection,backup=selectedRepairBackup();$('repair-undo-confirm').close();
  if(!backup||!repairWritable())return;
  const origin=$('restore-library-backup');
  const result=await run({method:'restore_device_library',volume:data.volume,expected_identity:data.identity,expected_files:data.expected_files,backup_id:backup.id},'Undoing the repair',origin);
  if(!result?.ok)return;
  if(result.result?.format!=='xz-library-restored/1'){showError('The undo returned a result this app cannot read. Check the library again before you use this USB.',origin);return;}
  renderRepairInspection(result.result.inspection);renderRepairOutcome('restore',result.result);
};
// Setup wizard for the no-USB and no-library states: connect, choose, check.
function openRepairConnect(){
  const volume=$('repair-drive').value;
  $('repair-connect-found').textContent=volume?'Selected: '+volume:'Nothing selected yet.';
  $('repair-connect-result').textContent='';$('repair-connect').showModal();updateRepairControls();
}
$('repair-connect-open').onclick=openRepairConnect;
$('repair-choose-other').onclick=openRepairConnect;
$('repair-connect-find').onclick=async()=>{
  repairDiscovery=discoverRepairDrives();const count=await repairDiscovery;if(count===undefined)return;
  const volume=$('repair-drive').value;
  $('repair-connect-found').textContent=count?plural(count,'Rekordbox USB')+' found. Selected: '+volume:'No Rekordbox USB found yet. Check that it is plugged in and shows on this computer, then find it again, or choose a folder.'+(volume?' Selected: '+volume:'');
};
$('repair-connect-choose').onclick=async()=>{const volume=await pickRepairFolder($('repair-connect-choose'));if(volume)$('repair-connect-found').textContent='Selected: '+volume;};
$('repair-connect-check').onclick=async()=>{
  const data=await inspectLibrary($('repair-connect-check'));if(!data)return;
  if(data.verdict==='no_library'){$('repair-connect-result').textContent=repairVerdicts.no_library.text;return;}
  $('repair-connect').close();$('repair-inspection').scrollIntoView({behavior:'smooth',block:'nearest'});
};
$('repair-connect-close').onclick=()=>$('repair-connect').close();
// Rekordbox blocks writes: close it, then check again.
$('repair-prepare-open').onclick=()=>{$('repair-prepare-result').textContent='';$('repair-prepare').showModal();updateRepairControls();};
$('repair-prepare-recheck').onclick=async()=>{
  const data=await inspectLibrary($('repair-prepare-recheck'));if(!data)return;
  if(data.rekordbox_running){$('repair-prepare-result').textContent='Rekordbox is still open. Quit it, wait a few seconds, then check again.';return;}
  $('repair-prepare').close();
  const next=data.verdict==='repairable'&&data.repair?$('repair-library'):!$('repair-undo').classList.contains('hidden')?$('restore-library-backup'):null;
  if(next){next.scrollIntoView({behavior:'smooth',block:'nearest'});next.focus({preventScroll:true});}
};
$('repair-prepare-close').onclick=()=>$('repair-prepare').close();
// Feedback started in a repair dialog moves to the page when the dialog closes.
for(const dialog of document.querySelectorAll('.repair-dialog'))dialog.addEventListener('close',()=>{
  if(!dialog.contains(jobPanel))return;
  feedbackHome=repairInspection?$('repair-verdict'):$('repair-drive-status');positionFeedback();if(feedbackLive)revealFeedback();
});
// The stems page sends an unreadable library here with its USB chosen.
async function openLibraryRepair(volume){
  showPage('library-repair');await repairDiscovery;
  if(!volume)return;selectRepairVolume(volume);
  if(activeJob===null&&!appUpdating&&invoke)await inspectLibrary($('inspect-library'));
};
updateRepairControls();

// Mod version. The list, save and import calls return the same inventory. Saving only records the choice on this
// computer; the next loader built or updated uses it. The backend writes the exact archived build and refuses unknown ones.
function shortHash(hash){return hash?String(hash).slice(0,12):'unknown';}
function channelText(channel){return modChannels[channel]||channel||'Unknown channel';}
function pinText(pinned){return pinned?'kept on the USB':'network updates can change it';}
// For recorded and written versions: label, pin state when known, short runtime hash.
function modVersionText(version){return (version.label||version.id)+(typeof version.pinned==='boolean'?', '+pinText(version.pinned):'')+' · runtime '+shortHash(version.runtime_sha256);}
function modVersionById(id){return modInventory?.versions.find(version=>version.id===id)||null;}
function savedModVersion(){return modInventory?modVersionById(modInventory.selected.id):null;}
function savedPinned(){return modInventory?.selected.pinned!==false;}
function modDraftState(){
  if(modDraft)return modDraft;
  const saved=modInventory.selected,fallback=modVersionById(saved.id)?saved.id:modVersionById(modInventory.bundled_id)?modInventory.bundled_id:modInventory.versions[0].id;
  return {id:fallback,pinned:savedPinned()};
}
function modVersionsReady(){return Boolean(modInventory?.versions.length);}
function modDirty(){if(!modVersionsReady())return false;const draft=modDraftState();return draft.id!==modInventory.selected.id||draft.pinned!==savedPinned();}
// The only source of mod_version_id for prepare_usb, build_usb and update_usb: the saved choice, never the dropdown.
function selectedVersionRequest(origin){
  const saved=savedModVersion();
  if(!saved){showError('Choose the XZ Mods build in Tools › XZ Mods version before writing a loader.',origin);return null;}
  if(modDirty()){showError('You changed the mod version but did not save it. Choose Save choice or Undo change in Tools › XZ Mods version, then try again.',origin);return null;}
  return {mod_version_id:saved.id,pin_mod_version:savedPinned()};
}
// True while the saved choice on this computer is still exactly the one a request was built from.
function sameSavedVersion(version){return savedModVersion()?.id===version.mod_version_id&&savedPinned()===version.pin_mod_version&&!modDirty();}
// Settings compatibility. Older mod versions read fewer settings and refuse a newer settings file. Before every
// loader write the backend reports whether this USB's settings need converting for the saved version. Compatible
// settings go straight to the write. A needed conversion is sent as adapt_mod_settings only after the user accepts
// that exact plan for that USB and version. Resolves to the extra request fields, or null when nothing may be written.
let modSettingsPrompt=null;
async function confirmModSettings(volume,version,origin){
  const result=await run({method:'inspect_mod_settings',volume},'Checking USB settings',origin);
  if(!result){if(invoke)showError('Another task is still running. Wait for it to finish, then try again. Nothing was written.',origin);return null;}
  if(!result.ok){showError('The USB settings could not be checked, so nothing was written. '+(result.error||'Reconnect the USB and try again.'),origin);return null;}
  const data=result.result,change=data?.settings_change;
  if(data?.format!=='xz-mod-settings/1'||data.mod_version_id!==version.mod_version_id||!sameSavedVersion(version)){showError('The settings check does not match the saved mod version. Nothing was written. Try again.',origin);return null;}
  if(change===null)return {};
  if(change?.required!==true){showError('XZ Mods returned a settings check this app cannot read. Nothing was written. Check again, or reinstall the app.',origin);return null;}
  const saved=savedModVersion(),dialog=$('mod-settings-confirm'),fields=$('mod-settings-fields');
  $('mod-settings-summary').textContent=change.summary||'The settings on this USB were saved by a newer mod version. '+(saved.label||saved.id)+' cannot read them as they are.';
  fields.replaceChildren();
  for(const [heading,keys] of [['Removed, this version does not have them',change.removed_fields],['Reset to this version\'s default',change.reset_fields]]){
    if(!Array.isArray(keys)||!keys.length)continue;
    const list=node('ul');for(const key of keys){const item=node('li');item.append(node('code',String(key)));list.append(item);}
    fields.append(node('h3',heading),list);
  }
  $('mod-settings-target').textContent='USB: '+volume+' · mod version '+(saved.label||saved.id);
  return new Promise(resolve=>{
    modSettingsPrompt={version,origin,settle(accepted){
      if(modSettingsPrompt!==this)return;
      modSettingsPrompt=null;if(dialog.open)dialog.close();
      if(!accepted)showError('Nothing was written. The USB settings were left as they are.',origin);
      resolve(accepted?{adapt_mod_settings:true,expected_mod_settings:change.expected}:null);
    }};
    dialog.showModal();updateModVersionControls();$('mod-settings-go').focus();
  });
}
$('mod-settings-go').onclick=()=>{
  const prompt=modSettingsPrompt;if(!prompt||activeJob!==null||appUpdating)return;
  if(!sameSavedVersion(prompt.version)){prompt.settle(false);showError('The saved mod version changed, so this settings plan no longer applies. Nothing was written. Start the write again.',prompt.origin);return;}
  prompt.settle(true);
};
$('mod-settings-cancel').onclick=()=>modSettingsPrompt?.settle(false);
// Escape and any other close count as Cancel.
$('mod-settings-confirm').addEventListener('close',()=>modSettingsPrompt?.settle(false));
function applyModInventory(data){
  const valid=data?.format==='xz-mod-versions/1'&&Array.isArray(data.versions)&&data.selected&&typeof data.selected==='object';
  modInventory=valid?data:null;modLoadError=valid?'':'XZ Mods returned a mod version list this app cannot read. Check again, or reinstall the app.';
  if(modDraft&&!modVersionById(modDraft.id))modDraft=null;
  // A changed saved choice voids a settings plan that is waiting for an answer.
  if(modSettingsPrompt&&!sameSavedVersion(modSettingsPrompt.version)){const origin=modSettingsPrompt.origin;modSettingsPrompt.settle(false);showError('The saved mod version changed, so this settings plan no longer applies. Nothing was written. Start the write again.',origin);}
  renderModVersions();return valid;
}
function updateModVersionControls(locked=false){
  locked=locked||activeJob!==null||appUpdating||!invoke;
  document.querySelectorAll('.mod-control').forEach(control=>control.disabled=locked);
  const ready=Boolean(savedModVersion()),dirty=modDirty();
  $('mod-version-pin').disabled=locked||!modVersionsReady()||modVersionById(modDraftState().id)?.network_updates_supported===false;
  $('mod-version-usb-setup').disabled=locked;
  $('save-mod-version').disabled=locked||!dirty;
  $('revert-mod-version').classList.toggle('hidden',!dirty||!ready);
  $('mod-wizard-import').disabled=locked||!modImportFolder;
  $('mod-wizard-done').disabled=locked||!modVersionsReady();
  // Writes need a saved, available version. A dirty selection is refused with an explanation when clicked.
  $('build').disabled=locked||!ready;
  $('update-usb').disabled=locked||!ready||!usbInspection?.loader.exists;
}
function renderModVersions(){
  const ready=modVersionsReady(),saved=savedModVersion();
  $('mod-version-setup').classList.toggle('hidden',ready);$('mod-version-editor').classList.toggle('hidden',!ready);$('mod-version-saved').classList.toggle('hidden',!ready);
  $('mod-version-badge').classList.toggle('hidden',!saved);
  if(!ready){
    $('mod-version-setup-reason').textContent=!invoke?'Mod versions load in the desktop app.':modLoadError||(modInventory?'No mod versions are on this computer. Import a build to continue. USB writes stay off until a version is saved.':'Reading mod versions…');
  }else{
    const meta=$('mod-version-meta');meta.replaceChildren();
    if(saved){
      $('mod-version-label').textContent=saved.label||saved.id;
      for(const text of [channelText(saved.channel),savedPinned()?'Kept on the USB':'Network updates allowed',saved.id===modInventory.bundled_id?'Included with this app':'',saved.firmware?'Firmware '+saved.firmware:''])if(text)meta.append(node('li',text));
      $('mod-version-description').textContent=saved.description||'';
      const hash=$('mod-version-hash');hash.textContent='Runtime '+shortHash(saved.runtime_sha256)+(saved.source_commit?' · source '+String(saved.source_commit).slice(0,10):'');hash.title=saved.runtime_sha256||'';
      $('mod-version-badge').textContent=saved.hardware_qualified===true?'Hardware-tested':'Not hardware-verified';
    }else{
      $('mod-version-label').textContent=modInventory.selected.id?'Saved version is missing':'No saved choice';
      $('mod-version-description').textContent=(modInventory.selected.id?'The saved build '+modInventory.selected.id+' is not on this computer. Import it, or choose another version and save. ':'Choose a version below and save it. ')+'USB writes stay off until then.';
      $('mod-version-hash').textContent='';$('mod-version-hash').title='';
    }
    const select=$('mod-version-select');let draft=modDraftState();
    if(modVersionById(draft.id)?.network_updates_supported===false&&!draft.pinned){modDraft={...draft,pinned:true};draft=modDraft;}
    select.replaceChildren();
    for(const version of modInventory.versions){const option=node('option',(version.label||version.id)+(version.id===modInventory.bundled_id?' · included with this app':''));option.value=version.id;select.append(option);}
    select.value=draft.id;$('mod-version-pin').checked=draft.pinned;
    const choice=modVersionById(draft.id);
    const usbOnly=choice?.network_updates_supported===false;
    $('mod-version-pin-note').textContent=usbOnly?(choice.network_update_reason||'This build updates through USB setup.'):'A loader built with this on skips automatic network updates. Turn it off to allow signed network updates.';
    $('mod-version-usb-setup').classList.toggle('hidden',!usbOnly);
    $('mod-version-draft').textContent=choice&&choice.id!==saved?.id?[channelText(choice.channel),choice.hardware_qualified===true?'':'Not hardware-verified',choice.description,'Runtime '+shortHash(choice.runtime_sha256)].filter(Boolean).join(' · '):'';
    const dirty=modDirty();$('mod-version-dirty').classList.toggle('hidden',!dirty);
    $('mod-version-dirty').textContent=dirty?'Not saved. USB writes still use '+(saved?(saved.label||saved.id)+', '+pinText(savedPinned()):'no version')+'. Save choice to use '+(choice?.label||draft.id)+', '+pinText(draft.pinned)+'.':'';
  }
  for(const line of document.querySelectorAll('.mod-version-uses')){
    line.textContent=saved?'Writes mod version '+(saved.label||saved.id)+', '+pinText(savedPinned())+'.'+(modDirty()?' Your unsaved change is not used.':''):'Mod version not set. Choose it above before writing a loader.';
    line.classList.toggle('is-blocked',!saved||modDirty());
  }
  $('mod-wizard-list').textContent=!invoke?'Open the desktop app to read mod versions.':ready?plural(modInventory.versions.length,'mod version')+' found on this computer. '+(saved?'Saved choice: '+(saved.label||saved.id)+'.':'No usable saved choice yet.'):modLoadError||(modInventory?'No mod versions found on this computer. Import a build below.':'Reading mod versions…');
  updateModVersionControls();
}
async function loadModVersions(){
  if(!invoke){renderModVersions();return false;}
  const result=await run({method:'list_mod_versions'},'Reading mod versions');
  if(!result){if(!modInventory){modLoadError='Mod versions were not read because another task was running. Check again.';renderModVersions();}return false;}
  if(!result.ok){modInventory=null;modLoadError='Mod versions could not be read. '+(result.error||'Check again, or import a build.');renderModVersions();return false;}
  return applyModInventory(result.result);
}
function showModVersionEditor(focus){showPage('advanced');$('mod-version').scrollIntoView({behavior:'smooth',block:'start'});$(focus).focus({preventScroll:true});}
function openModWizard(importing){
  modImportedId=null;$('mod-wizard-title').textContent=importing?'Import a mod build':'Set up mod versions';
  $('mod-wizard-done').textContent='Choose version';$('mod-wizard-import-result').textContent='';$('mod-wizard-folder').textContent=modImportFolder;
  renderModVersions();$('mod-version-wizard').showModal();if(importing)$('mod-wizard-choose').focus();
}
$('mod-version-select').onchange=()=>{modDraft={id:$('mod-version-select').value,pinned:$('mod-version-pin').checked};renderModVersions();};
$('mod-version-pin').onchange=$('mod-version-select').onchange;
$('mod-version-usb-setup').onclick=()=>{showPage('setup');window.setupWizard?.shown();};
$('revert-mod-version').onclick=()=>{modDraft=null;renderModVersions();};
$('save-mod-version').onclick=async()=>{
  if(!modDirty())return;
  const draft=modDraftState(),origin=$('save-mod-version');
  const result=await run({method:'select_mod_version',mod_version_id:draft.id,pinned:draft.pinned,expected_revision:modInventory.selected.revision},'Saving the mod version choice',origin);
  // A refused save (for example, the choice changed in another window) keeps the draft and refreshes the revision.
  if(!result?.ok){if(result&&!result.cancelled)await loadModVersions();return;}
  modDraft=null;
  if(!applyModInventory(result.result)){showError('The save returned a result this app cannot read. Check the mod version before you write a USB.',origin);return;}
  const saved=savedModVersion();
  $('job-summary').textContent=saved?'Saved on this computer: '+(saved.label||saved.id)+', '+pinText(savedPinned())+'. The next loader you build or update uses it. No USB was changed.':'';
};
$('import-mod-version').onclick=()=>openModWizard(true);
$('open-mod-version-setup').onclick=()=>openModWizard(false);
$('ota-mod-version').onclick=()=>{if(modVersionsReady())showModVersionEditor('mod-version-pin');else openModWizard(false);};
$('mod-wizard-retry').onclick=()=>{modLoadError='';if(!modInventory)$('mod-wizard-list').textContent='Reading mod versions…';loadModVersions();};
$('mod-wizard-choose').onclick=async()=>{
  try{const folder=await invoke('pick_path',{kind:'folder'});if(!folder)return;modImportFolder=folder;modImportedId=null;$('mod-wizard-folder').textContent=folder;$('mod-wizard-import-result').textContent='';$('mod-wizard-done').textContent='Choose version';updateModVersionControls();}
  catch(error){showError(String(error),$('mod-wizard-choose'));}
};
$('mod-wizard-import').onclick=async()=>{
  if(!modImportFolder)return;const origin=$('mod-wizard-import');
  const result=await run({method:'import_mod_version',folder:modImportFolder},'Importing and verifying a mod build',origin);
  if(!result?.ok)return;
  if(!applyModInventory(result.result)){showError('The import returned a result this app cannot read. Check again before you choose a version.',origin);return;}
  const imported=modVersionById(result.result.imported_id);modImportedId=imported?.id||null;
  $('mod-wizard-import-result').textContent=imported?'Added to the list: '+(imported.label||imported.id)+'. Its files passed the hash check. Runtime '+shortHash(imported.runtime_sha256)+'. Not hardware-verified.':'Import finished. The version list was refreshed.';
  $('job-summary').textContent='';
  if(imported)$('mod-wizard-done').textContent='Choose this version';
};
// Choosing an imported build only fills the dropdown. It is used after Save choice.
$('mod-wizard-done').onclick=()=>{
  if(!modVersionsReady())return;
  if(modImportedId&&modVersionById(modImportedId))modDraft={id:modImportedId,pinned:modDraftState().pinned};
  $('mod-version-wizard').close();renderModVersions();showModVersionEditor('mod-version-select');
};
$('mod-wizard-close').onclick=()=>$('mod-version-wizard').close();
$('mod-version-wizard').addEventListener('close',()=>{
  if(!$('mod-version-wizard').contains(jobPanel))return;
  feedbackHome=modVersionsReady()?$('save-mod-version'):$('mod-version-setup-reason');positionFeedback();if(feedbackLive)revealFeedback();
});
loadModVersions();
// Setup callers query the exact saved build; an unsaved dropdown change is never capability evidence.
window.quadSetupCapability=()=>({saved_id:savedModVersion()?.id||null,supported:savedModVersion()?.quad_supported===true,dirty:modDirty()});
function renderQuadSetup(){
 const capability=window.quadSetupCapability(),included=modVersionById(modInventory?.bundled_id);
 $('quad-state').textContent=capability.supported?'The exact saved build has a verified quad profile. Controller acceptance is separate.':'The saved build does not have a verified quad profile. Choose and save the included quad build to prepare four decks.';
 $('quad-use').disabled=included?.quad_supported!==true||activeJob!==null;$('quad-save').disabled=!modDirty()||activeJob!==null;
}
const quadButton=node('button','Set up four decks','primary');quadButton.id='quad-open';quadButton.type='button';$('mod-version').append(quadButton);
// The dialog's cable picture plays while it is open (a still with reduced motion) and stops when it closes.
const quadVideo=$('quad-video');
quadButton.onclick=()=>{renderQuadSetup();$('quad-setup').showModal();if(!matchMedia('(prefers-reduced-motion: reduce)').matches){quadVideo.muted=true;quadVideo.play?.().catch(()=>{});}};
$('quad-setup').addEventListener('close',()=>quadVideo.pause());
$('quad-use').onclick=()=>{const included=modVersionById(modInventory?.bundled_id);if(included?.quad_supported!==true)return;modDraft={id:included.id,pinned:true};renderModVersions();renderQuadSetup();};
$('quad-save').onclick=async()=>{await $('save-mod-version').onclick();renderQuadSetup();};
$('quad-close').onclick=()=>$('quad-setup').close();
