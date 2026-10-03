const $=id=>document.getElementById(id);
const invoke=window.__TAURI__?.core?.invoke;
let activeJob=null,appUpdating=false,updateChecking=false;
let importTarget=null,importFiles=[];
let stemLibraryStarted=false,stemLibrary=null,stemLibraryLoading=false;
const batchLimit=100,batchFormat='xz-stem-batch/1';
const batchStates={queued:'Queued',running:'Working',prepared:'Prepared',reused:'Already prepared',failed:'Failed',cancelled:'Cancelled'};
let stemSelection=new Map(),stemSelectionKey=null,stemBatch=null;
let usbInspection=null,settingsVolume=null,settingsIdentity=null,settingsData=null,settingsValues=null;
const titles={usb:'USB loader','usb-settings':'USB settings',stems:'Prepare your stems',games:'Games','device-updates':'XZ network updates',updates:'App updates',licenses:'About'};
for(const button of document.querySelectorAll('nav button'))button.onclick=()=>{
  document.querySelectorAll('.page').forEach(page=>page.classList.toggle('hidden',page.id!==button.dataset.page));
  document.querySelectorAll('nav button').forEach(item=>item.classList.toggle('selected',item===button));
  $('page-title').textContent=titles[button.dataset.page];
  if(activeJob===null)$('job-panel').classList.add('hidden');
  if(button.dataset.page==='stems'&&!stemLibraryStarted){stemLibraryStarted=true;discoverStemDrives();}
};
const preset=()=>document.querySelector('[name=engine]:checked').value;
function required(ids){for(const id of ids)if(!$(id).value.trim()){showError('Enter or choose the required '+({'volume':'USB folder',source:'track from your USB Contents folder',firmware:'firmware input',key:'boot key',harmonics:'harmonics file',vocals:'vocals file'}[id]||id)+' first.');return false;}return true;}
function showError(message){$('job-panel').classList.remove('hidden');$('job-title').textContent='Operation not completed';$('job-message').textContent=message;$('job-summary').textContent='';$('job-details').classList.add('hidden');$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');}
function busy(value){value=value||appUpdating;value=value||appUpdating;value=value||appUpdating;document.querySelectorAll(".library-action,.stem-import-control,#add-stem-files,#import-stem-groups,#stem-drive,#choose-stem-drive,#refresh-stem-drives,#search-stem-library,#library-engine").forEach(el=>el.disabled=value||!invoke);if(stemLibrary)updateLibraryPagination(value);document.querySelectorAll('.pick,#prepare-usb,#build,#verify-inputs,#setup-engine,#separate,#import-stems,#check-overcue,#choose-cache,#download-firmware,#inspect-usb,#update-usb,#refresh-usb,#edit-selected-usb,#choose-settings-usb,#save-usb-settings,#reload-usb-settings,.restore-loader,#setup-games-usb,[id^=download-],[id^=copy-]').forEach(button=>button.disabled=value||!invoke);document.querySelectorAll('.usb-setting,[name=engine],#harmonics-gain,#vocals-gain,#separation-id,#experimental').forEach(input=>input.disabled=value);$('import-stem-groups').disabled=value||!invoke||!importTarget||!importFiles.length||importFiles.some(item=>!item.group);$('install-app-update').disabled=value||activeJob!==null;updateStemBatchControls(value);}
function stemSummary(result){
  return (result.reused?'Matching stems for '+result.file_path+' were already on the USB. XZ Mods reused them.':'XZ Mods wrote beta stems for '+result.file_path+'.')+
    (result.replaced_previous?' It replaced the stems XZ Mods made earlier for this track.':'')+
    ' All files passed verification. Check alignment on the player before you use this track in a set.';
}
async function run(request,title){
  if(!invoke){showError('Open XZ Mods Builder to run this operation. This browser view is a preview.');return;}
  if(activeJob!==null||appUpdating)return;
  $('job-panel').classList.toggle('hidden',['discover_stem_usbs','browse_stem_library'].includes(request.method));$('job-title').textContent=title;$('job-message').textContent='Starting…';$('job-details').classList.add('hidden');$('job-details').open=false;$('job-result').textContent='';$('job-summary').textContent='';$('job-progress').removeAttribute('value');$('job-progress').classList.remove('hidden');$('cancel').classList.remove('hidden');$('cancel').disabled=false;busy(true);
  try{
    activeJob=await invoke('start_job',{request});
    while(true){
      const state=await invoke('job_status',{id:activeJob});
      $('job-message').textContent=state.message||state.error||(state.ok?'Completed':'Working…');
      const batch=request.method==='generate_library_stem_batch'?(state.result?.format===batchFormat?state.result:state.batch):null;
      if(batch&&applyStemBatch(batch,state)){const counts=stemBatchCounts();$('job-progress').max=Math.max(1,counts.total);$('job-progress').value=counts.done;$('job-summary').textContent=stemBatchCountsText(counts);}
      if(!state.running){
        $('job-title').textContent=state.ok?'Completed':state.cancelled||state.result?.cancelled===true?'Cancelled':'Operation not completed';
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
          if(state.result.format===batchFormat&&stemBatch)$('job-title').textContent=stemBatchHeading(stemBatch.status,stemBatchCounts());
        }
        return state;
      }
      await new Promise(resolve=>setTimeout(resolve,350));
    }
  }catch(error){showError(String(error));}
  finally{activeJob=null;busy(false);$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');}
}
for(const button of document.querySelectorAll('.pick'))button.onclick=async()=>{
  try{const path=await invoke('pick_path',{kind:button.dataset.kind});if(path)$(button.dataset.target).value=path;}
  catch(error){showError(String(error));}
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
  $('download-'+game).onclick=()=>run({method:'download_game',game},'Downloading '+gameLabels[game]);
  $('copy-'+game+'-usb').onclick=async()=>{
    if(!invoke){showError('Open the installed XZ Mods app to copy games to USB.');return;}
    try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await run({method:'install_game_usb',game,volume},'Downloading and copying '+gameLabels[game]);}
    catch(error){showError(String(error));}
  };
}
$('setup-games-usb').onclick=async()=>{
  if(!invoke){showError('Open the installed XZ Mods app to set up games on USB.');return;}
  try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await run({method:'prepare_games_usb',volume},'Setting up games on USB');}
  catch(error){showError(String(error));}
};
$('ota-detect').onclick=async()=>{
  const response=await run({method:'ota_networks'},'Detecting XZ network adapters');
  if(response?.ok){const select=$('ota-network');select.replaceChildren();for(const address of response.result.addresses){const option=document.createElement('option');option.value=address;option.textContent=address;select.append(option);}
    $('ota-status').textContent=response.result.ready?'Choose the XZ network adapter, then start the update server.':'Network update files are missing. Install the current XZ Mods app and prepare its USB loader.';
  }
};
$('ota-start').onclick=async()=>{
  const address=$('ota-network').value;if(!address){showError('Detect adapters and choose the XZ network adapter first.');return;}
  $('ota-status').textContent='Starting on '+address+'. On the XZ, open Extras → Network updates.';
  await run({method:'serve_runtime_updates',address},'Serving signed XZ updates');
};
$('prepare-usb').onclick=async()=>{
  if(!invoke){showError('Open XZ Mods Builder to prepare a USB.');return;}
  try{
    const volume=await invoke('pick_path',{kind:'folder'});
    if(volume)await run({method:'prepare_usb',volume,experimental:true},'Preparing your XDJ-XZ USB');
  }catch(error){showError(String(error));}
};
$('verify-inputs').onclick=async()=>{if(!required(['firmware']))return;const result=await run({method:'inspect_firmware',firmware:$('firmware').value},'Checking firmware');if(result?.ok)$('input-result').textContent=result.result.key_verified_against_input?'XZ 1.26 firmware and boot support verified.':'XZ 1.26 application verified. Choose the official firmware ZIP to verify complete boot support.';};
$('build').onclick=()=>{
  if(!required(['volume','firmware']))return;
  if(!$('experimental').checked){showError('Check the testing reminder before preparing a USB.');return;}
  run({method:'build_usb',volume:$('volume').value,firmware:$('firmware').value,experimental:true},'Preparing the USB loader');
};
$('import-stems').onclick=()=>{if(required(['source','harmonics','vocals']))run({method:'import_stems',source:$('source').value,harmonics:$('harmonics').value,vocals:$('vocals').value,separation_id:$('separation-id').value,harmonics_gain:Number($('harmonics-gain').value),vocals_gain:Number($('vocals-gain').value)},'Importing stems (beta)');};
$('setup-engine').onclick=()=>run({method:'setup_engine',preset:preset()},'Setting up the separation engine');
$('download-firmware').onclick=async()=>{const result=await run({method:'download_firmware'},'Getting the official firmware');if(result?.ok)$('firmware').value=result.result.firmware_path;};
$('separate').onclick=()=>{if(required(['source']))run({method:'separate',source:$('source').value,preset:preset(),device:'cpu'},'Preparing stems (beta)');};
$('choose-cache').onclick=async()=>{
  if(!$('source').value){showError('Choose the original track before reading its cache.');return;}
  try{
    const entry=await invoke('pick_path',{kind:'folder'});if(!entry)return;
    const response=await run({method:'inspect_cache',source:$('source').value,entry},'Reading stem cache');
    if(response?.ok){const data=response.result;for(const name of ['harmonics','vocals']){$(name).value=data[name];$(name+'-gain').value=data[name+'_gain'];}$('separation-id').value=data.separation_id;}
  }catch(error){showError(String(error));}
};
if(!invoke){$('browser-note').classList.remove('hidden');busy(false);}
for(const link of document.querySelectorAll('a[target=_blank]'))link.onclick=async event=>{if(invoke){event.preventDefault();try{await invoke('open_link',{url:link.getAttribute('href')});}catch(error){showError(String(error));}}};

$('check-overcue').onclick=async()=>{
  try{const source=await invoke('pick_path',{kind:'file'});if(source)await run({method:'inspect_overcue',source},'Checking OverCue track');}
  catch(error){showError(String(error));}
};

function showPage(name){document.querySelector('nav button[data-page="'+name+'"]').click();}
function node(tag,text,className){const item=document.createElement(tag);if(text!==undefined)item.textContent=text;if(className)item.className=className;return item;}
function renderUsb(inspection){
  usbInspection=inspection;$('usb-update-details').classList.remove('hidden');
  $('usb-selected-path').textContent=inspection.volume;
  $('usb-loader-status').textContent=inspection.loader.exists?'Existing loader: '+(inspection.loader.bytes/1048576).toFixed(1)+' MB.':'No loader found. Use Prepare a new USB below.';
  $('usb-loader-note').textContent=inspection.requires_copy_to_usb_root?'This is a prepared folder. Copy the updated autoexec.bin to the USB root afterward.':'The old loader is backed up before replacement. Its installed version cannot be identified from this file.';
  $('update-usb').disabled=!inspection.loader.exists||!invoke;
  const backups=$('loader-backups');backups.replaceChildren();
  if(inspection.backups.length){
    backups.append(node('h3','Restore a previous loader'));
    const select=node('select');select.className='usb-setting';
    for(const backup of inspection.backups){const option=node('option',new Date(backup.created).toLocaleString()+' - '+(backup.previous.bytes/1048576).toFixed(1)+' MB');option.value=backup.id;select.append(option);}
    const button=node('button','Restore selected backup','secondary restore-loader');
    button.onclick=async()=>{const result=await run({method:'restore_usb_loader',volume:inspection.volume,expected_identity:usbInspection.identity,expected_loader:usbInspection.loader,transaction_id:select.value},'Restoring the previous loader');if(result?.ok)renderUsb(result.result.inspection);};
    backups.append(select,button);
  }
}
async function inspectUsb(volume){const result=await run({method:'inspect_usb',volume},'Reading USB loader and settings');if(result?.ok)renderUsb(result.result);}
$('inspect-usb').onclick=async()=>{try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await inspectUsb(volume);}catch(error){showError(String(error));}};
$('refresh-usb').onclick=()=>{if(usbInspection)inspectUsb(usbInspection.volume);};
$('update-usb').onclick=async()=>{
  if(!usbInspection?.loader.exists)return;
  const request={method:'update_usb',volume:usbInspection.volume,expected_identity:usbInspection.identity,expected_loader:usbInspection.loader,experimental:true};
  if($('firmware').value.trim())request.firmware=$('firmware').value.trim();
  const result=await run(request,'Backing up and updating the USB loader');if(result?.ok)renderUsb(result.result.inspection);
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
async function loadSettings(volume){
  const result=await run({method:'read_usb_settings',volume},'Reading USB settings');
  if(result?.ok){settingsVolume=volume;settingsIdentity=result.result.identity;settingsData=result.result.settings;settingsValues=settingsData.editable?{...settingsData.values}:null;renderSettings();}
}
$('choose-settings-usb').onclick=async()=>{try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await loadSettings(volume);}catch(error){showError(String(error));}};
$('edit-selected-usb').onclick=()=>{if(usbInspection){showPage('usb-settings');loadSettings(usbInspection.volume);}};
$('reload-usb-settings').onclick=()=>{if(settingsVolume)loadSettings(settingsVolume);};
$('save-usb-settings').onclick=async()=>{
  if(!settingsData?.editable||!settingsVolume)return;
  const result=await run({method:'save_usb_settings',volume:settingsVolume,expected_identity:settingsIdentity,expected_revision:settingsData.revision,values:{...settingsValues}},'Saving USB settings');
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
$('open-licenses').onclick=async()=>{try{await invoke('open_licenses');}catch(error){showError(String(error));}};
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
function renderStemLibrary(data){
  stemLibrary=data;const rows=$('stem-tracks');rows.replaceChildren();
  const key=libraryKey(data);
  if(key!==stemSelectionKey){stemSelection.clear();stemSelectionKey=key;$('stem-selection-note').textContent='';}
  if(stemBatch&&!stemBatch.running&&stemBatch.key===key)stemBatch.refreshed=true;
  $('stem-drive-status').textContent=data.total+' tracks in the Rekordbox database on '+data.volume;
  $('stem-space').textContent=(data.free_bytes/1073741824).toFixed(2)+' GB free of '+(data.total_bytes/1073741824).toFixed(2)+' GB';
  if(data.index_error)$('stem-drive-status').textContent+=' · The stem index could not be read. '+data.index_error;
  for(const track of data.tracks){
    const row=node('tr');row.append(selectionCell(track,row));const title=node('td');title.append(node('strong',track.title),node('small',track.file_path));row.append(title,node('td',track.artist||'Unknown artist'),node('td',track.duration?Math.floor(track.duration/60)+':'+String(track.duration%60).padStart(2,'0'):'—'));
    const status=node('td',track.stem_label);if(track.detail)status.append(node('small',track.detail));row.append(status);
    const action=node('td');
    if(track.can_generate){const button=node('button',track.stem_status==='present'?'Regenerate':'Generate','secondary library-action');button.onclick=async()=>{
      const result=await run({method:'generate_library_stems',volume:data.volume,expected_identity:data.identity,file_path:track.file_path,preset:$('library-engine').value},'Preparing stems: '+track.title);
      if(result?.ok)await browseStemLibrary(data.volume,data.offset);
    };action.append(button);}
    if(track.can_verify){const button=node('button','Verify','secondary library-action');button.onclick=async()=>{
      const result=await run({method:'inspect_overcue',source:track.source},'Verifying stems: '+track.title);
      status.replaceChildren(node('span',result?.ok?'Verified':result?.cancelled?'Verification cancelled':'Verification failed'));
    };action.append(button);}
    if(!track.can_generate&&!track.can_verify){const button=node('button','Set up stems','primary library-action');button.onclick=()=>openStemSetup(data,track);action.append(button);}
    if(track.can_generate){const button=node('button','Import stems','secondary library-action');button.onclick=()=>{importTarget={volume:data.volume,identity:data.identity,track};$('import-target').textContent='Complete track: '+track.title;renderStemAssignments();$('grouped-import').scrollIntoView({behavior:'smooth',block:'start'});};action.append(button);}
    row.append(action);rows.append(row);
  }
  if(!data.tracks.length){const cell=node('td',data.total?'No tracks match your search.':'This Rekordbox library is empty. Export tracks to the USB, then refresh.');cell.colSpan=6;const row=node('tr');row.append(cell);rows.append(row);}
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
$('stem-setup-verify').onclick=async()=>{
  if(!stemSetupTarget||activeJob!==null||appUpdating)return;
  const button=$('stem-setup-verify');button.disabled=true;
  try{
    await browseStemLibrary(stemSetupTarget.volume,stemLibrary?.offset||0);
    const track=stemLibrary?.tracks.find(item=>item.file_path===stemSetupTarget.file_path);
    if(!track?.source||!track.can_verify){$('stem-setup-result').textContent=track?.stem_status==='unavailable'?'The audio file is still missing. Re-export it from Rekordbox to this USB, then check again.':track?.detail||'This track has no complete stem export yet. Prepare it in OverCue Desktop, then check again.';return;}
    const result=await run({method:'inspect_overcue',source:track.source},'Verifying prepared stems');
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
  const state=await run({method:'generate_library_stem_batch',volume:data.volume,expected_identity:data.identity,track_ids:items.map(item=>item.id),preset:stemBatch.preset},(retry?'Retrying stems for ':'Preparing stems for ')+plural(items.length,'track'));
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
    if(result?.ok)renderStemLibrary(result.result);else{$('stem-drive-status').textContent=result?.error||'The USB could not be read. Reconnect it and choose the drive again.';$('stem-tracks').replaceChildren();stemLibrary=null;updateLibraryPagination();}
  }finally{stemLibraryLoading=false;}
}
async function discoverStemDrives(){
  if(!invoke)return;
  const result=await run({method:'discover_stem_usbs'},'Finding Rekordbox USBs');if(!result?.ok)return;
  const select=$('stem-drive'),previous=select.value;select.replaceChildren();
  for(const drive of result.result.drives){const option=node('option',drive.label+' · '+drive.volume);option.value=drive.volume;select.append(option);}
  if([...select.options].some(option=>option.value===previous))select.value=previous;
  if(select.value)await browseStemLibrary(select.value);else{$('stem-drive-status').textContent='No Rekordbox USB found. Connect a USB exported for the XDJ-XZ, then choose Find USBs.';$('stem-space').textContent='';$('stem-tracks').replaceChildren();stemLibrary=null;updateLibraryPagination();}
}
$('stem-drive').onchange=()=>{$('stem-search').value='';browseStemLibrary($('stem-drive').value);};
$('choose-stem-drive').onclick=async()=>{try{const volume=await invoke('pick_path',{kind:'folder'});if(!volume)return;const select=$('stem-drive');if(![...select.options].some(o=>o.value===volume)){const option=node('option',volume);option.value=volume;select.append(option);}select.value=volume;$('stem-search').value='';await browseStemLibrary(volume);}catch(error){showError(String(error));}};
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
$('add-stem-files').onclick=async event=>{event.stopPropagation();try{addStemFiles(await invoke('pick_stem_files'));}catch(error){showError(String(error));}};
$('stem-dropzone').onclick=()=>$('add-stem-files').click();
$('stem-dropzone').onkeydown=event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();$('add-stem-files').click();}};
$('import-stem-groups').onclick=async()=>{
  if(!importTarget||!importFiles.length||importFiles.some(item=>!item.group))return;
  const result=await run({method:'import_grouped_stems',volume:importTarget.volume,expected_identity:importTarget.identity,file_path:importTarget.track.file_path,files:importFiles.map(item=>({...item})),fit_length:$('fit-stem-length').checked},'Importing stems: '+importTarget.track.title);
  $('stem-import-message').textContent=result?.ok?'Stems saved in OverCue format. Check playback timing on the XZ.':result?.error||'Import cancelled.';
  if(result?.ok&&stemLibrary)await browseStemLibrary(stemLibrary.volume,stemLibrary.offset);renderStemAssignments();
};
if(invoke)window.__TAURI__.webviewWindow.getCurrentWebviewWindow().onDragDropEvent(event=>{
  const payload=event.payload;
  if(payload.type==='over')$('stem-dropzone').classList.add('drag-over');
  else $('stem-dropzone').classList.remove('drag-over');
  if(payload.type==='drop'&&!$('stems').classList.contains('hidden')&&activeJob===null&&!appUpdating){addStemFiles(payload.paths);$('grouped-import').scrollIntoView({behavior:'smooth',block:'start'});}
}).catch(error=>{$('stem-import-message').textContent='Drag and drop is unavailable. Use Choose stem files.';});

