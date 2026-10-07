const $=id=>document.getElementById(id);
const invoke=window.__TAURI__?.core?.invoke;
let activeJob=null,appUpdating=false,updateChecking=false;
let importTarget=null,importFiles=[];
let stemLibraryStarted=false,stemLibraryStarting=false,stemLibrary=null,stemLibraryLoading=false;
// Work that found another job running waits here and runs once the current job ends.
const idleQueue=[];
const batchLimit=100,batchFormat='xz-stem-batch/1';
const batchStates={queued:'Queued',running:'Working',prepared:'Prepared',reused:'Already prepared',failed:'Failed',cancelled:'Cancelled'};
let stemSelection=new Map(),stemSelectionKey=null,stemBatch=null;
let usbInspection=null,settingsVolume=null,settingsIdentity=null,settingsData=null,settingsValues=null;
// Library repair: the inspection is the only basis for a repair or undo, and it is dropped whenever the chosen USB changes.
let repairInspection=null,repairDiscovery=null;
// Mod version: modInventory is the last list the backend returned and holds the saved choice. modDraft is the
// unsaved dropdown and checkbox state. USB writes read only the saved choice, through selectedVersionRequest().
let modInventory=null,modDraft=null,modLoadError='',modImportFolder='',modImportedId=null;
const modChannels={preview:'Preview',public:'Public release',release:'Public release',released:'Released',development:'Development build',dev:'Development build',imported:'Imported'};
const titles={usb:'USB loader','usb-settings':'USB settings',stems:'Prepare your stems','library-repair':'Repair library',games:'Games','device-updates':'XZ network updates',updates:'App updates',licenses:'About'};
// The single job panel moves next to the control that started the work. feedbackHome is that control;
// feedbackLive is true while a visible job runs. Library reads report in the library and leave the panel alone.
const jobPanel=$('job-panel'),quietMethods=['discover_stem_usbs','browse_stem_library','list_mod_versions','inspect_mod_settings'];
const feedbackGroups='p,label,.actions,.path-row,.batch-toolbar,.library-toolbar,#stem-dropzone,.section-heading,.columns';
let feedbackHome=null,feedbackLive=false;
for(const button of document.querySelectorAll('nav button'))button.onclick=()=>{
  document.querySelectorAll('.page').forEach(page=>page.classList.toggle('hidden',page.id!==button.dataset.page));
  document.querySelectorAll('nav button').forEach(item=>item.classList.toggle('selected',item===button));
  $('page-title').textContent=titles[button.dataset.page];
  if(!feedbackLive)jobPanel.classList.add('hidden');else{positionFeedback();revealFeedback();}
  if(button.dataset.page==='stems')startStemLibrary();
  if(button.dataset.page==='library-repair'&&!repairDiscovery)repairDiscovery=discoverRepairDrives();
};
const preset=()=>document.querySelector('[name=engine]:checked').value;
function required(ids,origin){for(const id of ids)if(!$(id).value.trim()){showError('Enter or choose the required '+({'volume':'USB folder',source:'track from your USB Contents folder',firmware:'firmware input',key:'boot key',harmonics:'harmonics file',vocals:'vocals file'}[id]||id)+' first.',origin);return false;}return true;}
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
  const lead=page.querySelector(':scope>.intro,:scope>.overcue-recommendation');
  movePanel(()=>lead?lead.after(jobPanel):page.prepend(jobPanel));
}
function revealFeedback(){if(!jobPanel.classList.contains('hidden')&&jobPanel.isConnected)jobPanel.scrollIntoView({block:'nearest'});}
function showFeedback(origin){feedbackHome=origin||null;jobPanel.classList.remove('hidden');positionFeedback();}
function showError(message,origin){jobPanel.classList.remove('is-cancelled');jobPanel.classList.add('is-error');$('job-title').textContent='Operation not completed';$('job-message').textContent=message;$('job-summary').textContent='';$('job-details').classList.add('hidden');$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');showFeedback(origin);revealFeedback();}
function busy(value){value=value||appUpdating;value=value||appUpdating;value=value||appUpdating;document.querySelectorAll(".library-action,.stem-import-control,#add-stem-files,#import-stem-groups,#stem-drive,#choose-stem-drive,#refresh-stem-drives,#search-stem-library,#library-engine").forEach(el=>el.disabled=value||!invoke);if(stemLibrary)updateLibraryPagination(value);document.querySelectorAll('.pick,#prepare-usb,#build,#verify-inputs,#setup-engine,#separate,#import-stems,#check-overcue,#choose-cache,#download-firmware,#inspect-usb,#update-usb,#refresh-usb,#edit-selected-usb,#choose-settings-usb,#save-usb-settings,#reload-usb-settings,.restore-loader,#setup-games-usb,#ota-detect,#ota-start,[id^=download-],[id^=copy-]').forEach(button=>button.disabled=value||!invoke);document.querySelectorAll('.usb-setting,[name=engine],#harmonics-gain,#vocals-gain,#separation-id,#experimental').forEach(input=>input.disabled=value);$('import-stem-groups').disabled=value||!invoke||!importTarget||!importFiles.length||importFiles.some(item=>!item.group);$('install-app-update').disabled=value||activeJob!==null;updateStemBatchControls(value);updateRepairControls(value);updateModVersionControls(value);}
function stemSummary(result){
  return (result.reused?'Matching stems for '+result.file_path+' were already on the USB. XZ Mods reused them.':'XZ Mods wrote beta stems for '+result.file_path+'.')+
    (result.replaced_previous?' It replaced the stems XZ Mods made earlier for this track.':'')+
    ' All files passed verification. Check alignment on the player before you use this track in a set.';
}
async function run(request,title,origin){
  if(!invoke){showError('Open XZ Mods Builder to run this operation. This browser view is a preview.',origin);return;}
  if(activeJob!==null||appUpdating)return;
  const quiet=quietMethods.includes(request.method);let settled=null;
  if(!quiet){jobPanel.classList.remove('is-error','is-cancelled');$('job-title').textContent=title;$('job-message').textContent='Starting…';$('job-details').classList.add('hidden');$('job-details').open=false;$('job-result').textContent='';$('job-summary').textContent='';$('job-progress').removeAttribute('value');$('job-progress').classList.remove('hidden');$('cancel').classList.remove('hidden');$('cancel').disabled=false;feedbackLive=true;showFeedback(origin);revealFeedback();}
  busy(true);
  try{
    activeJob=await invoke('start_job',{request});
    while(true){
      const state=await invoke('job_status',{id:activeJob});
      if(quiet){if(!state.running)return state;await new Promise(resolve=>setTimeout(resolve,350));continue;}
      $('job-message').textContent=state.message||state.error||(state.ok?'Completed':'Working…');
      const batch=request.method==='generate_library_stem_batch'?(state.result?.format===batchFormat?state.result:state.batch):null;
      if(batch&&applyStemBatch(batch,state)){const counts=stemBatchCounts();$('job-progress').max=Math.max(1,counts.total);$('job-progress').value=counts.done;$('job-summary').textContent=stemBatchCountsText(counts);}
      if(!state.running){
        const cancelled=Boolean(state.cancelled||state.result?.cancelled===true);
        $('job-title').textContent=state.ok?'Completed':cancelled?'Cancelled':'Operation not completed';
        settled=state.ok?null:cancelled?'is-cancelled':'is-error';
        if(state.result){
          $('job-result').textContent=JSON.stringify(state.result,null,2);$('job-details').classList.remove('hidden');
          $('job-summary').textContent=state.result.format===batchFormat?stemBatchCountsText(stemBatchCounts()):state.result.format==='xz-mods-loader-updated/1'?'Loader updated. Previous loader: '+state.result.backup:state.result.format==='xz-mods-loader-restored/1'?'Previous loader restored.':state.result.format==='xz-mods-settings-saved/1'?'Settings saved to USB and checked.':state.result.image?'Verified image: '+state.result.image+(state.result.requires_copy_to_usb_root?' · Copy autoexec.bin to the root of a FAT/FAT32 USB.':''):
            state.result.format==='overcue-stems/4'?'OverCue '+(state.result.page_codec==='flac-96k'?'FLAC ':state.result.page_codec==='mixed'?'FLAC/zlib ':'')+'track verified. All seven mixes and every audio page passed. Your USB was not changed.':
            state.result.format==='overcue-index/1'?stemSummary(state.result):
            state.result.installed?'The stem engine is ready.':'';
          if(state.result.game_setup || state.result.format==='xz-games-usb/1'){
            const setup=state.result.game_setup||state.result;
            const names=setup.games.filter(game=>game.ok).map(game=>game.name).join(', ');
            $('job-summary').textContent=(state.result.image?'Loader ready. ':'')+(names?'Game files on USB: '+names+'. ':'')+(setup.failed_count?setup.failed_count+' download(s) failed. Open Games and choose Set up games to retry. ':'')+'My House still needs the native GZDoom port.';
          }else if(state.result.format==='xz-game-installed/1')$('job-summary').textContent=state.result.name+' copied to USB and verified.'+(state.result.requires_gzdoom?' My House still needs the native GZDoom port.':'');
          else if(state.result.format==='xz-game-downloaded/1')$('job-summary').textContent=state.result.name+' downloaded and verified.';
          else if(state.result.format==='xz-library-inspected/1')$('job-summary').textContent='Check finished. The results are below.';
          else if(state.result.format==='xz-library-repaired/1'){$('job-title').textContent='Repair written · not yet tested in Rekordbox';$('job-summary').textContent=state.result.summary||'';}
          else if(state.result.format==='xz-library-restored/1'){$('job-title').textContent='Repair undone';$('job-summary').textContent=state.result.summary||'';}
          else if(state.result.format==='xz-library-report/1')$('job-summary').textContent='Report saved: '+state.result.report_path;
          if(state.result.mod_version?.id){const summary=$('job-summary').textContent.trim();$('job-summary').textContent=(summary&&!/[.!?]$/.test(summary)?summary+'.':summary)+(summary?' ':'')+'Mod version: '+modVersionText(state.result.mod_version)+'.';}
          const adjusted=state.result.settings_adjustment;
          if(adjusted?.backup){const summary=$('job-summary').textContent.trim();$('job-summary').textContent=(summary&&!/[.!?]$/.test(summary)?summary+'.':summary)+(summary?' ':'')+'USB settings adjusted for this version. Original settings backed up: '+adjusted.backup+'.';}
          if(state.result.format===batchFormat&&stemBatch)$('job-title').textContent=stemBatchHeading(stemBatch.status,stemBatchCounts());
        }
        return state;
      }
      await new Promise(resolve=>setTimeout(resolve,350));
    }
  }catch(error){if(quiet)return {ok:false,running:false,error:String(error)};settled='is-error';showError(String(error),origin);}
  finally{
    activeJob=null;busy(false);
    if(idleQueue.length)setTimeout(()=>{while(idleQueue.length&&activeJob===null&&!appUpdating)idleQueue.shift()();});
    if(!quiet){feedbackLive=false;$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');if(settled){jobPanel.classList.add(settled);revealFeedback();}}
  }
}
for(const button of document.querySelectorAll('.pick'))button.onclick=async()=>{
  try{const path=await invoke('pick_path',{kind:button.dataset.kind});if(path)$(button.dataset.target).value=path;}
  catch(error){showError(String(error),button);}
};
async function cancelActiveJob(){
  if(activeJob===null)return;
  if(stemBatch?.running){stemBatch.cancelRequested=true;renderStemBatch();}
  $('cancel').disabled=true;
  try{await invoke('cancel_job',{id:activeJob});$('job-message').textContent='Cancelling safely…';}
  catch(error){$('cancel').disabled=false;if(stemBatch?.running){stemBatch.cancelRequested=false;renderStemBatch();}$('job-message').textContent='Could not cancel: '+String(error);}
}
$('cancel').onclick=cancelActiveJob;
const gameLabels={doom:'Doom shareware',chex:'Chex Quest',freedoom:'Freedoom Phase 2',myhouse:'My House'};
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
    $('ota-status').textContent=response.result.ready?'Choose the XZ network adapter, then start the update server.':'Network update files are missing. Install the current XZ Mods app and prepare its USB loader.';
  }
};
$('ota-start').onclick=async()=>{
  const origin=$('ota-start'),address=$('ota-network').value;if(!address){showError('Detect adapters and choose the XZ network adapter first.',origin);return;}
  $('ota-status').textContent='Starting on '+address+'. On the XZ, open Extras → Network updates.';
  await run({method:'serve_runtime_updates',address},'Serving signed XZ updates',origin);
};
$('prepare-usb').onclick=async()=>{
  const origin=$('prepare-usb');
  if(!invoke){showError('Open XZ Mods Builder to prepare a USB.',origin);return;}
  const version=selectedVersionRequest(origin);if(!version)return;
  try{
    const volume=await invoke('pick_path',{kind:'folder'});
    if(!volume)return;
    const settings=await confirmModSettings(volume,version,origin);if(!settings)return;
    await run({method:'prepare_usb',volume,experimental:true,...version,...settings},'Preparing your XDJ-XZ USB',origin);
  }catch(error){showError(String(error),origin);}
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
$('import-stems').onclick=()=>{const origin=$('import-stems');if(required(['source','harmonics','vocals'],origin))run({method:'import_stems',source:$('source').value,harmonics:$('harmonics').value,vocals:$('vocals').value,separation_id:$('separation-id').value,harmonics_gain:Number($('harmonics-gain').value),vocals_gain:Number($('vocals-gain').value)},'Importing stems (beta)',origin);};
$('setup-engine').onclick=()=>run({method:'setup_engine',preset:preset()},'Setting up the separation engine',$('setup-engine'));
$('download-firmware').onclick=async()=>{const result=await run({method:'download_firmware'},'Getting the official firmware',$('download-firmware'));if(result?.ok)$('firmware').value=result.result.firmware_path;};
$('separate').onclick=()=>{const origin=$('separate');if(required(['source'],origin))run({method:'separate',source:$('source').value,preset:preset(),device:'cpu'},'Preparing stems (beta)',origin);};
$('choose-cache').onclick=async()=>{
  const origin=$('choose-cache');
  if(!$('source').value){showError('Choose the original track before reading its cache.',origin);return;}
  try{
    const entry=await invoke('pick_path',{kind:'folder'});if(!entry)return;
    const response=await run({method:'inspect_cache',source:$('source').value,entry},'Reading stem cache',origin);
    if(response?.ok){const data=response.result;for(const name of ['harmonics','vocals']){$(name).value=data[name];$(name+'-gain').value=data[name+'_gain'];}$('separation-id').value=data.separation_id;}
  }catch(error){showError(String(error),origin);}
};
if(!invoke){$('browser-note').classList.remove('hidden');busy(false);}
for(const link of document.querySelectorAll('a[target=_blank]'))link.onclick=async event=>{if(invoke){event.preventDefault();try{await invoke('open_link',{url:link.getAttribute('href')});}catch(error){showError(String(error),link);}}};

$('check-overcue').onclick=async()=>{
  const origin=$('check-overcue');
  try{const source=await invoke('pick_path',{kind:'file'});if(source)await run({method:'inspect_overcue',source},'Checking OverCue track',origin);}
  catch(error){showError(String(error),origin);}
};

function showPage(name){document.querySelector('nav button[data-page="'+name+'"]').click();}
function node(tag,text,className){const item=document.createElement(tag);if(text!==undefined)item.textContent=text;if(className)item.className=className;return item;}
function renderUsb(inspection){
  usbInspection=inspection;$('usb-update-details').classList.remove('hidden');
  $('usb-selected-path').textContent=inspection.volume;
  $('usb-loader-status').textContent=inspection.loader.exists?'Existing loader: '+(inspection.loader.bytes/1048576).toFixed(1)+' MB.':'No loader found. Use Prepare a new USB below.';
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
function stemDiagram(values){
  const block=node('div',undefined,'pad-map'),roles=['VOCALS','HARMONICS','DRUMS','BYPASS'];
  for(let pad=0;pad<8;pad++){
    const assigned=Math.floor(pad/4)===values.stem_bank;
    const button=node('button',undefined,'pad-square usb-setting'+(assigned?' assigned':''));button.type='button';
    button.append(node('strong',String.fromCharCode(65+pad)),node('span',assigned?roles[pad%4]:'Original pad'));
    button.setAttribute('aria-label','Use pads '+(pad<4?'A to D':'E to H')+' for stem shortcuts');
    button.onclick=()=>{settingsValues.stem_bank=Math.floor(pad/4);renderSettings();};block.append(button);
  }
  return block;
}
function renderSettings(){
  const data=settingsData;$('settings-editor').classList.toggle('hidden',!data?.editable);
  $('settings-selected-path').textContent=settingsVolume||'';
  if(!data)return;
  $('settings-status').textContent=data.editable?(data.revision.exists?'Loaded saved settings. Changes are written only when you click Save.':'No settings file yet. Save to create one with these defaults.'):data.reason;
  if(!data.editable)return;
  const holder=$('settings-fields');holder.replaceChildren();
  const names={appearance:'Appearance',stems:'Stem shortcuts',beat_jump:'Beat Jump',advanced:'Other controls'};
  for(const group of Object.keys(names)){
    const panel=node('section',undefined,'panel');panel.append(node('h2',names[group]));
    if(group==='stems'){panel.append(node('p','Choose the pad mode, then tap a row to assign its four pads.'));}
    if(group==='beat_jump')panel.append(node('p','Page 1 opens with Beat Jump. Hold Shift and tap Beat Jump for page 2. Each pair jumps backward and forward. One bar is four beats.'));
    const fields=node('div',undefined,'settings-grid');
    for(const field of data.schema.fields.filter(item=>item.group===group&&!item.readonly&&item.key!=='stem_bank')){
      const label=node('label',field.label);let input;
      if(field.kind==='boolean'){
        input=node('input');input.type='checkbox';input.checked=settingsValues[field.key]===1;label.className='setting-toggle';
        input.onchange=()=>{settingsValues[field.key]=input.checked?1:0;};
      }else{
        input=node('select');for(const choice of field.choices){const option=node('option',choice.label);option.value=String(choice.value);input.append(option);}
        input.value=String(settingsValues[field.key]);
        input.onchange=()=>{settingsValues[field.key]=Number(input.value);if(field.key==='stem_bank'||field.key==='stem_page')renderSettings();};
      }
      input.classList.add('usb-setting');input.dataset.setting=field.key;label.append(input);fields.append(label);
    }
    panel.append(fields);if(group==='stems')panel.append(stemDiagram(settingsValues));
    if(group==='stems'&&settingsValues.stem_page===3&&settingsValues.jump_enabled)panel.append(node('p','Custom Beat Jump is using the Beat Jump pads. Choose another mode for stem shortcuts, or switch Custom Beat Jump off.','notice'));
    holder.append(panel);
  }
}
async function loadSettings(volume,origin){
  const result=await run({method:'read_usb_settings',volume},'Reading USB settings',origin);
  if(result?.ok){settingsVolume=volume;settingsIdentity=result.result.identity;settingsData=result.result.settings;settingsValues=settingsData.editable?{...settingsData.values}:null;renderSettings();}
}
$('choose-settings-usb').onclick=async()=>{const origin=$('choose-settings-usb');try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await loadSettings(volume,origin);}catch(error){showError(String(error),origin);}};
// The read happens on the settings page, so its feedback belongs beside that page's USB chooser.
$('edit-selected-usb').onclick=()=>{if(usbInspection){showPage('usb-settings');loadSettings(usbInspection.volume,$('choose-settings-usb'));}};
$('reload-usb-settings').onclick=()=>{if(settingsVolume)loadSettings(settingsVolume,$('reload-usb-settings'));};
$('save-usb-settings').onclick=async()=>{
  if(!settingsData?.editable||!settingsVolume)return;
  const result=await run({method:'save_usb_settings',volume:settingsVolume,expected_identity:settingsIdentity,expected_revision:settingsData.revision,values:{...settingsValues}},'Saving USB settings',$('save-usb-settings'));
  if(result?.ok){settingsData=result.result.settings;settingsIdentity=result.result.identity;settingsValues={...settingsData.values};renderSettings();$('settings-status').textContent='Settings saved to USB and verified. The XZ loads them when its loader starts.';}
};

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
$('check-app-update').onclick=async()=>{
  if(updateChecking||appUpdating)return;updateChecking=true;$('available-notes').classList.add('hidden');renderAppUpdate({phase:'checking',message:'Checking for updates...'});
  try{renderAppUpdate(await invoke('check_app_update'));}catch(error){renderAppUpdate({phase:'error',message:String(error)});}finally{updateChecking=false;$('check-app-update').disabled=!invoke||appUpdating;}
};
$('install-app-update').onclick=async()=>{
  if(activeJob!==null||appUpdating)return;appUpdating=true;busy(true);renderAppUpdate({phase:'downloading',message:'Downloading the installer...'});
  const timer=setInterval(async()=>{try{renderAppUpdate(await invoke('app_update_status'));}catch{}},400);
  try{await invoke('install_app_update');}catch(error){try{renderAppUpdate(await invoke('app_update_status'));}catch{renderAppUpdate({phase:'error',message:String(error)});}}
  finally{clearInterval(timer);appUpdating=false;busy(false);$('check-app-update').disabled=!invoke;}
};
async function loadAppInformation(){
  if(!invoke){$('check-app-update').disabled=true;$('open-licenses').disabled=true;return;}
  try{
    const info=await invoke('app_information');document.querySelectorAll('[data-app-version]').forEach(item=>item.textContent=info.version);
    $('update-install-note').textContent=info.platform==='macos'?'Mac updates require a published, signed Mac package. Check the release page for available downloads.':info.installed?'The Windows installer updates the app and reopens it when finished.':'You are using the portable app. Updating here installs the Windows version; it does not replace this portable folder. After installing, open XZ Mods from the desktop shortcut. To stay portable, download the current ZIP below and replace the old folder.';
    if(info.platform==='macos'){
      const link=document.querySelector('a[href$="XZ-Mods-Builder-preview-win64.zip"]');
      link.href=info.portable_url;link.textContent='View Mac downloads';
      link.closest('.panel').querySelector('p').textContent='Mac downloads are available for Apple Silicon and Intel. These preview packages are ad-hoc signed and are not notarized.';
    }
    $('release-notes').replaceChildren();for(const release of info.release_notes.releases)renderReleaseNotes($('release-notes'),release,release.version);
    if(info.show_release_notes){showPage('updates');$('updated-message').classList.remove('hidden');await invoke('acknowledge_release_notes');}
    $('check-app-update').click();
  }catch(error){$('update-status').textContent=String(error);}
}
loadAppInformation();

function updateLibraryPagination(disabled=false){
  $('stem-previous').disabled=disabled||!stemLibrary||stemLibrary.offset===0;
  $('stem-next').disabled=disabled||!stemLibrary||stemLibrary.offset+stemLibrary.page_size>=stemLibrary.matched;
  updateStemBatchControls(disabled);
}
// Empties the track table without taking the job panel with it. Returns the track row the feedback belonged to.
function clearStemTracks(){
  const rows=$('stem-tracks'),row=feedbackHome?.closest('#stem-tracks tr'),held=row?{path:row.dataset.filePath,action:feedbackHome.dataset.action}:null;
  if(row||jobPanel.closest('#stem-tracks')){feedbackHome=rows.closest('.table-scroll');positionFeedback();}
  rows.replaceChildren();return held;
}
function renderStemLibrary(data){
  stemLibrary=data;const rows=$('stem-tracks'),held=clearStemTracks();
  const key=libraryKey(data);
  if(key!==stemSelectionKey){stemSelection.clear();stemSelectionKey=key;$('stem-selection-note').textContent='';}
  if(stemBatch&&!stemBatch.running&&stemBatch.key===key)stemBatch.refreshed=true;
  $('stem-drive-status').textContent=data.total+' tracks in the Rekordbox database on '+data.volume;
  $('stem-space').textContent=(data.free_bytes/1073741824).toFixed(2)+' GB free of '+(data.total_bytes/1073741824).toFixed(2)+' GB';
  if(data.index_error)$('stem-drive-status').textContent+=' · The stem index could not be read. '+data.index_error;
  for(const track of data.tracks){
    const row=node('tr');row.dataset.filePath=track.file_path;row.append(selectionCell(track,row));const title=node('td');title.append(node('strong',track.title),node('small',track.file_path));row.append(title,node('td',track.artist||'Unknown artist'),node('td',track.duration?Math.floor(track.duration/60)+':'+String(track.duration%60).padStart(2,'0'):'—'));
    const status=node('td',track.stem_label);if(track.detail)status.append(node('small',track.detail));row.append(status);
    const action=node('td');
    if(track.can_generate){const button=node('button',track.stem_status==='present'?'Regenerate':'Generate','secondary library-action');button.dataset.action='generate';button.onclick=async()=>{
      const result=await run({method:'generate_library_stems',volume:data.volume,expected_identity:data.identity,file_path:track.file_path,preset:$('library-engine').value},'Preparing stems: '+track.title,button);
      if(result?.ok)await browseStemLibrary(data.volume,data.offset);
    };action.append(button);}
    if(track.can_verify){const button=node('button','Verify','secondary library-action');button.dataset.action='verify';button.onclick=async()=>{
      const result=await run({method:'inspect_overcue',source:track.source},'Verifying stems: '+track.title,button);
      status.replaceChildren(node('span',result?.ok?'Verified':result?.cancelled?'Verification cancelled':'Verification failed'));
    };action.append(button);}
    if(!track.can_generate&&!track.can_verify){const button=node('button','Set up stems','primary library-action');button.onclick=()=>openStemSetup(data,track);action.append(button);}
    if(track.can_generate){const button=node('button','Import stems','secondary library-action');button.onclick=()=>{importTarget={volume:data.volume,identity:data.identity,track};$('import-target').textContent='Complete track: '+track.title;renderStemAssignments();$('grouped-import').scrollIntoView({behavior:'smooth',block:'start'});};action.append(button);}
    row.append(action);rows.append(row);
  }
  if(!data.tracks.length){const cell=node('td',data.total?'No tracks match your search.':'This Rekordbox library is empty. Export tracks to the USB, then refresh.');cell.colSpan=6;const row=node('tr');row.append(cell);rows.append(row);}
  if(held){const row=[...rows.children].find(item=>item.dataset.filePath===held.path);if(row){feedbackHome=row.querySelector('[data-action="'+held.action+'"]')||row;positionFeedback();}}
  $('stem-page-summary').textContent=data.matched?(data.offset+1)+'–'+Math.min(data.offset+data.page_size,data.matched)+' of '+data.matched:'0 tracks';updateLibraryPagination();
  renderStemSelection();renderStemBatch();
}

// Batch preparation. Selection is keyed by the authoritative PDB track ID and is scoped to one USB identity.
function libraryKey(data){return data?data.volume+'\n'+JSON.stringify(data.identity):null;}
let stemSetupTarget=null;
function openStemSetup(data,track){
  stemSetupTarget={volume:data.volume,file_path:track.file_path};
  $('stem-setup-detect').textContent=track.stem_status==='unavailable'?'The exported audio file is missing. Re-export this track from Rekordbox to this USB before preparing stems.':track.detail||track.stem_label;
  $('stem-setup-result').textContent='';$('stem-setup').showModal();
}
$('stem-setup-close').onclick=()=>$('stem-setup').close();
// Feedback started in the dialog moves to the page when the dialog closes.
$('stem-setup').addEventListener('close',()=>{if(jobPanel.closest('#stem-setup')){positionFeedback();if(feedbackLive)revealFeedback();}});
$('stem-setup-verify').onclick=async()=>{
  if(!stemSetupTarget||activeJob!==null||appUpdating)return;
  const button=$('stem-setup-verify');button.disabled=true;
  try{
    await browseStemLibrary(stemSetupTarget.volume,stemLibrary?.offset||0);
    const track=stemLibrary?.tracks.find(item=>item.file_path===stemSetupTarget.file_path);
    if(!track?.source||!track.can_verify){$('stem-setup-result').textContent=track?.stem_status==='unavailable'?'The audio file is still missing. Re-export it from Rekordbox to this USB, then check again.':track?.detail||'This track has no complete stem export yet. Prepare it in OverCue Desktop, then check again.';return;}
    const result=await run({method:'inspect_overcue',source:track.source},'Verifying prepared stems',button);
    $('stem-setup-result').textContent=result?.ok?'All seven mixes verified. This track is ready for the updated XZ loader.':result?.error||'Verification did not complete. Check the stem export and retry.';
  }finally{button.disabled=false;}
};
function batchEligible(track){return track.id!==undefined&&track.id!==null&&track.stem_status!=='error'&&Boolean(track.can_generate||track.can_verify);}
function plural(count,word){return count+' '+word+(count===1?'':'s');}
function stemWorkLocked(){return activeJob!==null||appUpdating||!invoke;}
function trackLabel(item){return (item.title||'Track '+item.id)+(item.artist?' by '+item.artist:'');}
function selectionCell(track,row){
  const cell=node('td',undefined,'select-cell'),id=String(track.id);
  if(!batchEligible(track)){stemSelection.delete(id);return cell;}
  const label=node('label',undefined,'row-check'),box=node('input');box.type='checkbox';box.className='stem-select';box.dataset.trackId=id;
  box.setAttribute('aria-label','Select '+trackLabel(track));box.checked=stemSelection.has(id);row.classList.toggle('picked',box.checked);
  if(box.checked)stemSelection.set(id,{id,title:track.title,artist:track.artist});
  box.onchange=()=>{
    if(stemWorkLocked()){box.checked=stemSelection.has(id);return;}
    if(box.checked&&stemSelection.size>=batchLimit){box.checked=false;$('stem-selection-note').textContent='A batch can include up to '+batchLimit+' tracks.';return;}
    if(box.checked)stemSelection.set(id,{id,title:track.title,artist:track.artist});else stemSelection.delete(id);
    $('stem-selection-note').textContent='';renderStemSelection();
  };
  label.append(box);cell.append(label);return cell;
}
function renderStemSelection(){
  const count=stemSelection.size,boxes=[...document.querySelectorAll('#stem-tracks .stem-select')];
  for(const box of boxes){box.checked=stemSelection.has(box.dataset.trackId);box.closest('tr').classList.toggle('picked',box.checked);}
  const onPage=boxes.filter(box=>box.checked).length;
  $('stem-selection-count').textContent=count?plural(count,'track')+' selected'+(count>=batchLimit?' (maximum)':'')+(onPage<count?' · '+onPage+' on this page':''):'No tracks selected';
  $('prepare-selected-stems').textContent=count?'Prepare '+count+' selected':'Prepare selected';
  const list=$('stem-selection-list');list.replaceChildren();
  for(const item of stemSelection.values()){
    const entry=node('li');entry.append(node('span',trackLabel(item)));
    const remove=node('button','Remove','secondary stem-select-remove');remove.setAttribute('aria-label','Remove '+trackLabel(item)+' from the selection');
    remove.onclick=()=>{if(stemWorkLocked())return;stemSelection.delete(item.id);renderStemSelection();};entry.append(remove);list.append(entry);
  }
  $('stem-selection-review').classList.toggle('hidden',!count);if(!count)$('stem-selection-review').open=false;
  updateStemBatchControls();
}
function updateStemBatchControls(locked=false){
  locked=locked||stemWorkLocked();
  const boxes=[...document.querySelectorAll('#stem-tracks .stem-select')],full=stemSelection.size>=batchLimit;
  for(const box of boxes)box.disabled=locked||(!box.checked&&full);
  document.querySelectorAll('.stem-select-remove').forEach(button=>button.disabled=locked);
  $('stem-select-page').disabled=locked||full||!boxes.some(box=>!box.checked);
  $('stem-clear-selection').disabled=locked||!stemSelection.size;
  $('prepare-selected-stems').disabled=locked||!stemSelection.size||!stemLibrary||libraryKey(stemLibrary)!==stemSelectionKey;
  if(!stemBatch)return;
  const failed=stemBatch.items.filter(item=>item.state==='failed').length,sameUsb=Boolean(stemLibrary)&&libraryKey(stemLibrary)===stemBatch.key;
  const retry=$('retry-failed-stems');retry.classList.toggle('hidden',stemBatch.running||!failed);retry.textContent='Retry '+failed+' failed';
  retry.disabled=locked||stemBatch.running||!failed||!stemBatch.refreshed||!sameUsb;
  $('stem-batch-retry-note').textContent=!stemBatch.running&&failed&&!sameUsb?'Open the same USB to retry the failed tracks. Selections and retries are tied to the USB they came from.':'';
  $('cancel-stem-batch').classList.toggle('hidden',!stemBatch.running);$('cancel-stem-batch').disabled=!stemBatch.running||stemBatch.cancelRequested||activeJob===null;
  $('clear-stem-batch').disabled=stemBatch.running||locked;
}
function stemBatchCounts(){
  const counts={queued:0,running:0,prepared:0,reused:0,failed:0,cancelled:0,total:0,done:0};if(!stemBatch)return counts;
  for(const item of stemBatch.items)if(item.state in counts)counts[item.state]++;
  counts.total=stemBatch.items.length;counts.done=counts.prepared+counts.reused+counts.failed+counts.cancelled;return counts;
}
function stemBatchCountsText(counts){
  return counts.done+' of '+counts.total+' finished · '+counts.prepared+' prepared · '+counts.reused+' already prepared · '+counts.failed+' failed'+(counts.cancelled?' · '+counts.cancelled+' cancelled':'');
}
function stemBatchHeading(status,counts){
  return status==='running'?'Preparing stems':status==='cancelled'?'Batch cancelled':status==='aborted'||status==='error'?'Batch stopped':counts.failed?'Batch finished with failures':'Batch complete';
}
// Returns true when the snapshot changed, so callers can skip redundant redraws while polling.
function applyStemBatch(snapshot,state){
  if(!stemBatch||!stemBatch.running)return false;
  const signature=JSON.stringify(snapshot)+'\n'+(state.message||'')+'\n'+Boolean(state.running);if(signature===stemBatch.signature)return false;stemBatch.signature=signature;
  if(Array.isArray(snapshot.items)&&snapshot.items.length){
    const known=new Map(stemBatch.items.map(item=>[item.id,item]));
    stemBatch.items=snapshot.items.map(item=>{const id=String(item.id),previous=known.get(id)||{};return {id,title:item.title||previous.title,artist:item.artist||previous.artist,state:batchStates[item.state]?item.state:'queued',stage:item.stage||'',message:item.message||'',error:item.error||''};});
  }
  stemBatch.message=state.message||'';
  // Progress snapshots carry the same format; only the final result (job no longer running) settles the status.
  if(snapshot.format===batchFormat&&!state.running){stemBatch.result=snapshot;stemBatch.status=snapshot.aborted?'aborted':snapshot.cancelled===true?'cancelled':'completed';stemBatch.error=snapshot.error||state.error||'';}
  renderStemBatch();return true;
}
function stemBatchMessage(batch,counts){
  if(batch.running&&batch.cancelRequested)return 'Cancelling after the current step. Tracks that already finished stay on the USB.';
  if(batch.status==='running'){
    const index=batch.items.findIndex(item=>item.state==='running');
    if(index<0)return batch.message||'Starting…';
    const item=batch.items[index];return 'Track '+(index+1)+' of '+counts.total+': '+trackLabel(item)+(item.message||batch.message?' · '+(item.message||batch.message):'');
  }
  const kept=counts.prepared+counts.reused,left=counts.queued+counts.cancelled+counts.running;
  const finished=(counts.prepared?plural(counts.prepared,'track')+' prepared. ':'')+(counts.reused?plural(counts.reused,'track')+' already had verified stems. ':'')+(counts.failed?plural(counts.failed,'track')+' failed; details are below. ':'');
  const unfinished=left?plural(left,'track')+' did not finish and '+(left===1?'stays':'stay')+' selected. ':'';
  if(batch.status==='cancelled')return 'Cancelled. '+finished+unfinished+(kept?'Finished tracks are kept.':'');
  if(batch.status==='aborted'||batch.status==='error')return 'The batch stopped before finishing. '+finished+unfinished+(kept?'Finished tracks are kept.':'');
  return finished||'No tracks were prepared.';
}
function renderStemBatch(){
  const batch=stemBatch;$('stem-batch').classList.toggle('hidden',!batch);if(!batch){updateStemBatchControls();return;}
  const counts=stemBatchCounts(),status=batch.running&&batch.status!=='running'?'running':batch.status;
  const badge=$('stem-batch-state');badge.textContent=batch.running?(batch.cancelRequested?'Cancelling':'Running'):stemBatchHeading(status,counts).replace('Batch ','').replace(/^./,c=>c.toUpperCase());badge.dataset.state=batch.running?'running':counts.failed&&status==='completed'?'failed':status;
  $('stem-batch-title').textContent=(batch.retry?'Retry: ':'')+'Batch preparation · '+plural(counts.total,'track');
  const message=stemBatchMessage(batch,counts);if($('stem-batch-message').textContent!==message)$('stem-batch-message').textContent=message;
  $('stem-batch-progress').max=Math.max(1,counts.total);$('stem-batch-progress').value=counts.done;
  $('stem-batch-engine').textContent=batch.presetLabel;
  const totals=$('stem-batch-counts');totals.replaceChildren();
  for(const [state,text] of [['prepared','Prepared'],['reused','Already prepared'],['failed','Failed'],['cancelled','Cancelled'],['queued',batch.running?'Queued':'Not started']]){
    if(!counts[state]&&(state==='cancelled'||state==='queued'&&batch.running===false))continue;
    const chip=node('li');chip.dataset.state=state;chip.append(node('strong',String(counts[state])),document.createTextNode(text));totals.append(chip);
  }
  const error=batch.error&&(batch.status==='aborted'||batch.status==='error')?batch.error:'';
  $('stem-batch-error').textContent=error;$('stem-batch-error').classList.toggle('hidden',!error);
  const failedOnly=$('stem-batch-failed-only').checked,list=$('stem-batch-items');list.replaceChildren();
  for(const item of batch.items){
    if(failedOnly&&item.state!=='failed')continue;
    const entry=node('li',undefined,'batch-item');entry.dataset.state=item.state;if(!batch.running&&item.state==='queued')entry.classList.add('not-started');
    entry.append(node('span',!batch.running&&item.state==='queued'?'Not started':!batch.running&&item.state==='running'?'Interrupted':batchStates[item.state],'batch-badge'));
    const body=node('div',undefined,'batch-body');body.append(node('strong',item.title||'Track '+item.id),node('small',item.artist||'Unknown artist'));
    const detail=item.state==='failed'?(item.error||item.message||'This track could not be prepared.'):item.state==='running'?(batch.running?(item.message||item.stage||'Working…'):'Stopped before this track finished.'):item.message;
    if(detail)body.append(node('p',detail,'batch-detail'));
    entry.append(body);list.append(entry);
  }
  if(!list.children.length)list.append(node('li',failedOnly?'No failed tracks.':'No tracks in this batch.','batch-empty'));
  updateStemBatchControls();
}
async function startStemBatch(entries,retry=false){
  if(!stemLibrary||stemWorkLocked()||stemBatch?.running)return;
  const data=stemLibrary,seen=new Set(),items=[];
  for(const entry of entries){const id=String(entry.id);if(seen.has(id)||items.length>=batchLimit)continue;seen.add(id);items.push({id,title:entry.title,artist:entry.artist,state:'queued',stage:'',message:'',error:''});}
  if(!items.length)return;
  const engine=$('library-engine');
  stemBatch={key:libraryKey(data),volume:data.volume,preset:engine.value,presetLabel:'Engine: '+engine.selectedOptions[0].textContent,retry,running:true,cancelRequested:false,refreshed:false,status:'running',items,message:'',error:'',result:null,signature:''};
  $('stem-batch-failed-only').checked=false;renderStemBatch();$('stem-batch').scrollIntoView({behavior:'smooth',block:'nearest'});
  // Batch feedback sits under the batch progress bar, where the batch is tracked.
  const state=await run({method:'generate_library_stem_batch',volume:data.volume,expected_identity:data.identity,track_ids:items.map(item=>item.id),preset:stemBatch.preset},(retry?'Retrying stems for ':'Preparing stems for ')+plural(items.length,'track'),$('stem-batch-progress'));
  const batch=stemBatch;batch.running=false;batch.cancelRequested=false;
  if(state?.result?.format!==batchFormat&&state?.cancelled){batch.status='cancelled';batch.error='';}
  else if(state?.result?.format!==batchFormat){batch.status='error';batch.error=state?.error||(state?'The batch ended without a result. Refresh the library to see which tracks were prepared.':$('job-message').textContent||'The batch could not be started.');}
  // Finished tracks leave the selection; anything that did not finish stays selected so Prepare selected continues the work.
  if(stemSelectionKey===batch.key)for(const item of batch.items){
    if(item.state==='prepared'||item.state==='reused')stemSelection.delete(item.id);
    else if(['queued','running','cancelled'].includes(item.state)&&!stemSelection.has(item.id)&&stemSelection.size<batchLimit)stemSelection.set(item.id,{id:item.id,title:item.title,artist:item.artist});
  }
  renderStemBatch();renderStemSelection();
  if(stemLibrary&&libraryKey(stemLibrary)===batch.key)await browseStemLibrary(batch.volume,stemLibrary.offset);
}
$('stem-select-page').onclick=()=>{
  if(stemWorkLocked()||!stemLibrary)return;let skipped=0;
  for(const track of stemLibrary.tracks.filter(batchEligible)){const id=String(track.id);if(stemSelection.has(id))continue;if(stemSelection.size>=batchLimit){skipped++;continue;}stemSelection.set(id,{id,title:track.title,artist:track.artist});}
  $('stem-selection-note').textContent=skipped?'A batch can include up to '+batchLimit+' tracks. '+plural(skipped,'track')+' on this page were not added.':'';renderStemSelection();
};
$('stem-clear-selection').onclick=()=>{if(stemWorkLocked())return;stemSelection.clear();$('stem-selection-note').textContent='';renderStemSelection();};
$('prepare-selected-stems').onclick=()=>startStemBatch([...stemSelection.values()]);
$('retry-failed-stems').onclick=()=>{if(stemBatch&&!stemBatch.running&&stemBatch.refreshed&&stemLibrary&&libraryKey(stemLibrary)===stemBatch.key)startStemBatch(stemBatch.items.filter(item=>item.state==='failed'),true);};
$('cancel-stem-batch').onclick=cancelActiveJob;
$('clear-stem-batch').onclick=()=>{if(stemBatch?.running)return;stemBatch=null;renderStemBatch();};
$('stem-batch-failed-only').onchange=renderStemBatch;
async function browseStemLibrary(volume,offset=0){
  if(stemLibraryLoading||activeJob!==null||appUpdating)return;stemLibraryLoading=true;
  $('stem-drive-status').textContent='Reading the Rekordbox library and stem files...';
  try{const result=await run({method:'browse_stem_library',volume,offset,query:$('stem-search').value},'Reading USB music');
    if(result?.ok)renderStemLibrary(result.result);else{$('stem-drive-status').textContent=result?.error||'The USB could not be read. Reconnect it and choose the drive again.';clearStemTracks();stemLibrary=null;updateLibraryPagination();}
  }finally{stemLibraryLoading=false;}
}
async function discoverStemDrives(){
  if(!invoke)return;
  const result=await run({method:'discover_stem_usbs'},'Finding Rekordbox USBs');
  if(!result)return;
  if(!result.ok){$('stem-drive-status').textContent=result.error||'Rekordbox USBs could not be listed. Reconnect the USB, then choose Find USBs.';return false;}
  stemLibraryStarted=true;
  const select=$('stem-drive'),previous=select.value;select.replaceChildren();
  for(const drive of result.result.drives){const option=node('option',drive.label+' · '+drive.volume);option.value=drive.volume;select.append(option);}
  if([...select.options].some(option=>option.value===previous))select.value=previous;
  if(select.value)await browseStemLibrary(select.value);else{$('stem-drive-status').textContent='No Rekordbox USB found. Connect a USB exported for the XDJ-XZ, then choose Find USBs.';$('stem-space').textContent='';clearStemTracks();stemLibrary=null;updateLibraryPagination();}
  return true;
}
// The first visit to Prepare stems lists USBs. It counts as started only once the listing ran; if another job
// (such as the startup mod version read) was running, it waits for that job and tries again.
async function startStemLibrary(){
  if(stemLibraryStarted||stemLibraryStarting||!invoke)return;
  stemLibraryStarting=true;let listed;
  try{listed=await discoverStemDrives();}finally{stemLibraryStarting=false;}
  if(listed===undefined&&!$('stems').classList.contains('hidden')&&!idleQueue.includes(startStemLibrary))idleQueue.push(startStemLibrary);
}
$('stem-drive').onchange=()=>{$('stem-search').value='';browseStemLibrary($('stem-drive').value);};
$('choose-stem-drive').onclick=async()=>{try{const volume=await invoke('pick_path',{kind:'folder'});if(!volume)return;const select=$('stem-drive');if(![...select.options].some(o=>o.value===volume)){const option=node('option',volume);option.value=volume;select.append(option);}select.value=volume;$('stem-search').value='';await browseStemLibrary(volume);}catch(error){showError(String(error),$('choose-stem-drive'));}};
$('refresh-stem-drives').onclick=discoverStemDrives;
$('search-stem-library').onclick=()=>{if($('stem-drive').value)browseStemLibrary($('stem-drive').value);};
$('stem-search').onkeydown=event=>{if(event.key==='Enter')$('search-stem-library').click();};
$('stem-previous').onclick=()=>{if(stemLibrary)browseStemLibrary(stemLibrary.volume,stemLibrary.offset-stemLibrary.page_size);};
$('stem-next').onclick=()=>{if(stemLibrary)browseStemLibrary(stemLibrary.volume,stemLibrary.offset+stemLibrary.page_size);};

function addStemFiles(paths){
  for(const path of paths){if(importFiles.some(item=>item.path.toLowerCase()===path.toLowerCase()))continue;if(importFiles.length>=128){$('stem-import-message').textContent='A maximum of 128 stem files can be imported at once.';break;}importFiles.push({path,group:''});}
  renderStemAssignments();
}
function renderStemAssignments(){
  const holder=$('stem-assignments');holder.replaceChildren();
  for(const [index,item] of importFiles.entries()){
    const row=node('div',undefined,'stem-assignment');row.append(node('span',item.path.split(/[\\/]/).pop()));
    const select=node('select');select.className='stem-import-control';select.setAttribute('aria-label','Group for '+item.path.split(/[\\/]/).pop());
    for(const [value,text] of [['','Choose a group'],['drums','Drums'],['vocals','Vocals'],['harmonics','Harmonics']]){const option=node('option',text);option.value=value;select.append(option);}select.value=item.group;
    select.onchange=()=>{item.group=select.value;renderStemAssignments();};const remove=node('button','Remove','secondary stem-import-control');remove.onclick=()=>{importFiles.splice(index,1);renderStemAssignments();};row.append(select,remove);holder.append(row);
  }
  $('stem-group-summary').textContent=['drums','vocals','harmonics'].map(group=>group[0].toUpperCase()+group.slice(1)+': '+importFiles.filter(item=>item.group===group).length).join(' · ');
  $('import-stem-groups').disabled=!invoke||!importTarget||!importFiles.length||importFiles.some(item=>!item.group)||activeJob!==null||appUpdating;
}
$('add-stem-files').onclick=async event=>{event.stopPropagation();try{addStemFiles(await invoke('pick_stem_files'));}catch(error){showError(String(error),$('add-stem-files'));}};
$('stem-dropzone').onclick=()=>$('add-stem-files').click();
$('stem-dropzone').onkeydown=event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();$('add-stem-files').click();}};
$('import-stem-groups').onclick=async()=>{
  if(!importTarget||!importFiles.length||importFiles.some(item=>!item.group))return;
  const result=await run({method:'import_grouped_stems',volume:importTarget.volume,expected_identity:importTarget.identity,file_path:importTarget.track.file_path,files:importFiles.map(item=>({...item})),fit_length:$('fit-stem-length').checked},'Importing stems: '+importTarget.track.title,$('import-stem-groups'));
  $('stem-import-message').textContent=result?.ok?'Stems saved in OverCue format. Check playback timing on the XZ.':result?.error||'Import cancelled.';
  if(result?.ok&&stemLibrary)await browseStemLibrary(stemLibrary.volume,stemLibrary.offset);renderStemAssignments();
};
if(invoke)window.__TAURI__.webviewWindow.getCurrentWebviewWindow().onDragDropEvent(event=>{
  const payload=event.payload;
  if(payload.type==='over')$('stem-dropzone').classList.add('drag-over');
  else $('stem-dropzone').classList.remove('drag-over');
  if(payload.type==='drop'&&!$('stems').classList.contains('hidden')&&activeJob===null&&!appUpdating){addStemFiles(payload.paths);$('grouped-import').scrollIntoView({behavior:'smooth',block:'start'});}
}).catch(error=>{$('stem-import-message').textContent='Drag and drop is unavailable. Use Choose stem files.';});

// Library repair. Checking only reads. Repair and undo send back the identity and file hashes from the check
// they were planned on, so the backend refuses if the library changed in between.
const repairVerdicts={
  healthy:{badge:'No damage found',text:'No structural damage found in the checked tables. Rekordbox and player acceptance still need testing.'},
  repairable:{badge:'Repair available',text:'Some playback history pages are cut off by blank pages. XZ Mods can reconnect the intact pages. History entries that are already missing cannot be recovered.'},
  not_repairable:{badge:'Cannot repair here',text:'This library has damage XZ Mods cannot repair. This app only reconnects intact playback history pages. Save a report to keep the details.'},
  no_library:{badge:'No library',text:'No Rekordbox library was found here. Choose the USB root or a folder that contains PIONEER.'}
};
const repairCountLabels=[['tracks','Tracks'],['playlists','Playlists'],['stem_sets','Stem sets']];
function repairVerdictText(data){return repairVerdicts[data?.verdict]?.text||'XZ Mods could not classify this library. Save a report to keep the details.';}
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
  $('repair-connect-found').textContent=count?plural(count,'Rekordbox USB')+' found. Selected: '+volume:'No Rekordbox USB found yet. Check that it is plugged in and shows in File Explorer, then find it again, or choose a folder.'+(volume?' Selected: '+volume:'');
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
// Always reachable from the stems page, including when the library there cannot be read.
$('open-library-repair').onclick=async()=>{
  const volume=$('stem-drive').value;showPage('library-repair');await repairDiscovery;
  if(!volume)return;selectRepairVolume(volume);
  if(activeJob===null&&!appUpdating&&invoke)await inspectLibrary($('inspect-library'));
};
updateRepairControls();

// Mod version. The list, save and import calls return the same inventory. Saving only records the choice on this
// PC; the next loader built or updated uses it. The backend writes the exact archived build and refuses unknown ones.
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
  if(!saved){showError('Set and save the mod version at the top of the USB loader page before writing a loader.',origin);return null;}
  if(modDirty()){showError('You changed the mod version but did not save it. Choose Save choice or Undo change at the top of the USB loader page, then try again.',origin);return null;}
  return {mod_version_id:saved.id,pin_mod_version:savedPinned()};
}
// True while the saved choice on this PC is still exactly the one a request was built from.
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
  $('save-mod-version').disabled=locked||!dirty;
  $('revert-mod-version').classList.toggle('hidden',!dirty||!ready);
  $('mod-wizard-import').disabled=locked||!modImportFolder;
  $('mod-wizard-done').disabled=locked||!modVersionsReady();
  // Writes need a saved, available version. A dirty selection is refused with an explanation when clicked.
  $('prepare-usb').disabled=locked||!ready;$('build').disabled=locked||!ready;
  $('update-usb').disabled=locked||!ready||!usbInspection?.loader.exists;
}
function renderModVersions(){
  const ready=modVersionsReady(),saved=savedModVersion();
  $('mod-version-setup').classList.toggle('hidden',ready);$('mod-version-editor').classList.toggle('hidden',!ready);$('mod-version-saved').classList.toggle('hidden',!ready);
  $('mod-version-badge').classList.toggle('hidden',!saved);
  if(!ready){
    $('mod-version-setup-reason').textContent=!invoke?'Mod versions load in the desktop app.':modLoadError||(modInventory?'No mod versions are on this PC. Import a build to continue. USB writes stay off until a version is saved.':'Reading mod versions…');
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
      $('mod-version-description').textContent=(modInventory.selected.id?'The saved build '+modInventory.selected.id+' is not on this PC. Import it, or choose another version and save. ':'Choose a version below and save it. ')+'USB writes stay off until then.';
      $('mod-version-hash').textContent='';$('mod-version-hash').title='';
    }
    const select=$('mod-version-select'),draft=modDraftState();select.replaceChildren();
    for(const version of modInventory.versions){const option=node('option',(version.label||version.id)+(version.id===modInventory.bundled_id?' · included with this app':''));option.value=version.id;select.append(option);}
    select.value=draft.id;$('mod-version-pin').checked=draft.pinned;
    const choice=modVersionById(draft.id);
    $('mod-version-draft').textContent=choice&&choice.id!==saved?.id?[channelText(choice.channel),choice.hardware_qualified===true?'':'Not hardware-verified',choice.description,'Runtime '+shortHash(choice.runtime_sha256)].filter(Boolean).join(' · '):'';
    const dirty=modDirty();$('mod-version-dirty').classList.toggle('hidden',!dirty);
    $('mod-version-dirty').textContent=dirty?'Not saved. USB writes still use '+(saved?(saved.label||saved.id)+', '+pinText(savedPinned()):'no version')+'. Save choice to use '+(choice?.label||draft.id)+', '+pinText(draft.pinned)+'.':'';
  }
  for(const line of document.querySelectorAll('.mod-version-uses')){
    line.textContent=saved?'Writes mod version '+(saved.label||saved.id)+', '+pinText(savedPinned())+'.'+(modDirty()?' Your unsaved change is not used.':''):'Mod version not set. Set it at the top of the USB loader page before writing a loader.';
    line.classList.toggle('is-blocked',!saved||modDirty());
  }
  $('mod-wizard-list').textContent=!invoke?'Open the desktop app to read mod versions.':ready?plural(modInventory.versions.length,'mod version')+' found on this PC. '+(saved?'Saved choice: '+(saved.label||saved.id)+'.':'No usable saved choice yet.'):modLoadError||(modInventory?'No mod versions found on this PC. Import a build below.':'Reading mod versions…');
  updateModVersionControls();
}
async function loadModVersions(){
  if(!invoke){renderModVersions();return false;}
  const result=await run({method:'list_mod_versions'},'Reading mod versions');
  if(!result){if(!modInventory){modLoadError='Mod versions were not read because another task was running. Check again.';renderModVersions();}return false;}
  if(!result.ok){modInventory=null;modLoadError='Mod versions could not be read. '+(result.error||'Check again, or import a build.');renderModVersions();return false;}
  return applyModInventory(result.result);
}
function showModVersionEditor(focus){showPage('usb');$('mod-version').scrollIntoView({behavior:'smooth',block:'start'});$(focus).focus({preventScroll:true});}
function openModWizard(importing){
  modImportedId=null;$('mod-wizard-title').textContent=importing?'Import a mod build':'Set up mod versions';
  $('mod-wizard-done').textContent='Choose version';$('mod-wizard-import-result').textContent='';$('mod-wizard-folder').textContent=modImportFolder;
  renderModVersions();$('mod-version-wizard').showModal();if(importing)$('mod-wizard-choose').focus();
}
$('mod-version-select').onchange=()=>{modDraft={id:$('mod-version-select').value,pinned:$('mod-version-pin').checked};renderModVersions();};
$('mod-version-pin').onchange=$('mod-version-select').onchange;
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
  $('job-summary').textContent=saved?'Saved on this PC: '+(saved.label||saved.id)+', '+pinText(savedPinned())+'. The next loader you build or update uses it. No USB was changed.':'';
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
