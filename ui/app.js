const $=id=>document.getElementById(id);
const invoke=window.__TAURI__?.core?.invoke;
let activeJob=null,appUpdating=false,updateChecking=false;
let importTarget=null,importFiles=[];
let stemLibraryStarted=false,stemLibrary=null,stemLibraryLoading=false;
let usbInspection=null,settingsVolume=null,settingsIdentity=null,settingsData=null,settingsValues=null;
const titles={usb:'USB loader','usb-settings':'USB settings',stems:'Prepare your stems',updates:'App updates',licenses:'About'};
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
function busy(value){value=value||appUpdating;value=value||appUpdating;value=value||appUpdating;document.querySelectorAll(".library-action,.stem-import-control,#add-stem-files,#import-stem-groups,#stem-drive,#choose-stem-drive,#refresh-stem-drives,#search-stem-library,#library-engine").forEach(el=>el.disabled=value||!invoke);if(stemLibrary)updateLibraryPagination(value);document.querySelectorAll('.pick,#prepare-usb,#build,#verify-inputs,#setup-engine,#separate,#import-stems,#check-overcue,#choose-cache,#download-firmware,#inspect-usb,#update-usb,#refresh-usb,#edit-selected-usb,#choose-settings-usb,#save-usb-settings,#reload-usb-settings,.restore-loader').forEach(button=>button.disabled=value||!invoke);document.querySelectorAll('.usb-setting,[name=engine],#harmonics-gain,#vocals-gain,#separation-id,#experimental').forEach(input=>input.disabled=value);$('import-stem-groups').disabled=value||!invoke||!importTarget||!importFiles.length||importFiles.some(item=>!item.group);$('install-app-update').disabled=value||activeJob!==null;}
function stemSummary(result){
  return (result.reused?'Matching stems for '+result.file_path+' were already on the USB. XZ Mods reused them.':'XZ Mods wrote beta stems for '+result.file_path+'.')+
    (result.replaced_previous?' It replaced the stems XZ Mods made earlier for this track.':'')+
    ' All files passed verification. Check alignment on the player before you use this track in a set.';
}
async function run(request,title){
  if(!invoke){showError('Open XZ Mods Builder to run this operation. This browser view is a preview.');return;}
  if(activeJob!==null||appUpdating)return;
  $('job-panel').classList.toggle('hidden',['discover_stem_usbs','browse_stem_library'].includes(request.method));$('job-title').textContent=title;$('job-message').textContent='Starting…';$('job-details').classList.add('hidden');$('job-details').open=false;$('job-result').textContent='';$('job-summary').textContent='';$('job-progress').classList.remove('hidden');$('cancel').classList.remove('hidden');busy(true);
  try{
    activeJob=await invoke('start_job',{request});
    while(true){
      const state=await invoke('job_status',{id:activeJob});
      $('job-message').textContent=state.message||state.error||(state.ok?'Completed':'Working…');
      if(!state.running){
        $('job-title').textContent=state.ok?'Completed':state.cancelled?'Cancelled':'Operation not completed';
        if(state.result){
          $('job-result').textContent=JSON.stringify(state.result,null,2);$('job-details').classList.remove('hidden');
          $('job-summary').textContent=state.result.format==='xz-mods-loader-updated/1'?'Loader updated. Previous loader: '+state.result.backup:state.result.format==='xz-mods-loader-restored/1'?'Previous loader restored.':state.result.format==='xz-mods-settings-saved/1'?'Settings saved to USB and checked.':state.result.image?'Verified image: '+state.result.image+(state.result.requires_copy_to_usb_root?' · Copy autoexec.bin to the root of a FAT/FAT32 USB.':''):
            state.result.format==='overcue-stems/4'?'OverCue track verified. All seven mixes and every audio page passed. Your USB was not changed.':
            state.result.format==='overcue-index/1'?stemSummary(state.result):
            state.result.installed?'The stem engine is ready.':'';
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
$('cancel').onclick=async()=>{if(activeJob!==null){await invoke('cancel_job',{id:activeJob});$('job-message').textContent='Cancelling safely…';}};
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
    $('update-install-note').textContent=info.installed?'The Windows installer updates the app and reopens it when finished.':'You are using the portable app. Updating here installs the Windows version. To stay portable, download the ZIP below and extract it to a new folder.';
    $('release-notes').replaceChildren();for(const release of info.release_notes.releases)renderReleaseNotes($('release-notes'),release,release.version);
    if(info.show_release_notes){showPage('updates');$('updated-message').classList.remove('hidden');await invoke('acknowledge_release_notes');}
    $('check-app-update').click();
  }catch(error){$('update-status').textContent=String(error);}
}
loadAppInformation();

function updateLibraryPagination(disabled=false){
  $('stem-previous').disabled=disabled||!stemLibrary||stemLibrary.offset===0;
  $('stem-next').disabled=disabled||!stemLibrary||stemLibrary.offset+stemLibrary.page_size>=stemLibrary.matched;
}
function renderStemLibrary(data){
  stemLibrary=data;const rows=$('stem-tracks');rows.replaceChildren();
  $('stem-drive-status').textContent=data.total+' tracks in the Rekordbox database on '+data.volume;
  $('stem-space').textContent=(data.free_bytes/1073741824).toFixed(2)+' GB free of '+(data.total_bytes/1073741824).toFixed(2)+' GB';
  if(data.index_error)$('stem-drive-status').textContent+=' · The stem index could not be read. '+data.index_error;
  for(const track of data.tracks){
    const row=node('tr');const title=node('td');title.append(node('strong',track.title),node('small',track.file_path));row.append(title,node('td',track.artist||'Unknown artist'),node('td',track.duration?Math.floor(track.duration/60)+':'+String(track.duration%60).padStart(2,'0'):'—'));
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
    if(!track.can_generate&&!track.can_verify)action.append(node('span',track.stem_status==='missing'?'Unavailable':'—'));
    if(track.can_generate){const button=node('button','Import stems','secondary library-action');button.onclick=()=>{importTarget={volume:data.volume,identity:data.identity,track};$('import-target').textContent='Complete track: '+track.title;renderStemAssignments();$('grouped-import').scrollIntoView({behavior:'smooth',block:'start'});};action.append(button);}
    row.append(action);rows.append(row);
  }
  if(!data.tracks.length){const cell=node('td',data.total?'No tracks match your search.':'This Rekordbox library is empty. Export tracks to the USB, then refresh.');cell.colSpan=5;const row=node('tr');row.append(cell);rows.append(row);}
  $('stem-page-summary').textContent=data.matched?(data.offset+1)+'–'+Math.min(data.offset+data.page_size,data.matched)+' of '+data.matched:'0 tracks';updateLibraryPagination();
}
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

