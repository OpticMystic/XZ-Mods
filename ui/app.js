const $=id=>document.getElementById(id);
const invoke=window.__TAURI__?.core?.invoke;
let activeJob=null;
const titles={usb:'Prepare your USB',stems:'Prepare your stems',branding:'Your DJ branding',vj:'Meet VJ.Tools',licenses:'Licenses & release status'};
for(const button of document.querySelectorAll('nav button'))button.onclick=()=>{
  document.querySelectorAll('.page').forEach(page=>page.classList.toggle('hidden',page.id!==button.dataset.page));
  document.querySelectorAll('nav button').forEach(item=>item.classList.toggle('selected',item===button));
  $('page-title').textContent=titles[button.dataset.page];
};
function values(){return {volume:$('stem-volume').value,source:$('source').value,preset:document.querySelector('[name=engine]:checked').value};}
function required(ids){for(const id of ids)if(!$(id).value.trim()){showError('Enter or choose the required '+({'volume':'USB folder','stem-volume':'output folder','branding-artist':'artist name','branding-volume':'branding destination',source:'original track',firmware:'firmware input',key:'boot key',harmonics:'harmonics file',vocals:'vocals file'}[id]||id)+' first.');return false;}return true;}
function showError(message){$('job-panel').classList.remove('hidden');$('job-title').textContent='Operation not completed';$('job-message').textContent=message;$('job-summary').textContent='';$('job-details').classList.add('hidden');$('job-progress').classList.add('hidden');$('cancel').classList.add('hidden');}
function busy(value){document.querySelectorAll('.pick,#prepare-usb,#build,#verify-inputs,#setup-engine,#separate,#import-stems,#check-overcue,#choose-cache,#download-firmware,#import-branding,.branding-add,.branding-remove').forEach(button=>button.disabled=value||!invoke);document.querySelectorAll('[name=engine],#harmonics-gain,#vocals-gain,#separation-id,#experimental').forEach(input=>input.disabled=value);}
async function run(request,title){
  if(!invoke){showError('Open XZ Mods Builder to run this operation. This browser view is a preview.');return;}
  if(activeJob!==null)return;
  $('job-panel').classList.remove('hidden');$('job-title').textContent=title;$('job-message').textContent='Starting…';$('job-details').classList.add('hidden');$('job-details').open=false;$('job-summary').textContent='';$('job-progress').classList.remove('hidden');$('cancel').classList.remove('hidden');busy(true);
  try{
    activeJob=await invoke('start_job',{request});
    while(true){
      const state=await invoke('job_status',{id:activeJob});
      $('job-message').textContent=state.message||state.error||(state.ok?'Completed':'Working…');
      if(!state.running){
        $('job-title').textContent=state.ok?'Completed':state.cancelled?'Cancelled':'Operation not completed';
        if(state.result){
          $('job-result').textContent=JSON.stringify(state.result,null,2);$('job-details').classList.remove('hidden');
          $('job-summary').textContent=state.result.image?'Verified image: '+state.result.image+(state.result.requires_copy_to_usb_root?' · Copy autoexec.bin to the root of a FAT/FAT32 USB.':''):
            state.result.format==='overcue-stems/4'?'OverCue track verified. All seven mixes and every audio page passed. Your USB was not changed.':
            state.result.track_on_usb?'Legacy stemd cache prepared. Original track preserved. Track on USB: '+state.result.track_on_usb:
            state.result.branding_directory?'Branding saved: '+state.result.branding_directory:state.result.installed?'Selected engine files installed separately from VJ.Tools.':'';
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
$('verify-inputs').onclick=async()=>{if(!required(['firmware']))return;const result=await run({method:'inspect_firmware',firmware:$('firmware').value},'Checking firmware inputs');if(result?.ok)$('input-result').textContent=result.result.key_verified_against_input?'XZ 1.26 firmware and boot support verified.':'XZ 1.26 application verified. Choose the official firmware ZIP to verify complete boot support.';};
$('build').onclick=()=>{
  if(!required(['volume','firmware']))return;
  if(!$('experimental').checked){showError('Review and acknowledge the experimental build status before preparing a USB.');return;}
  run({method:'build_usb',volume:$('volume').value,firmware:$('firmware').value,experimental:true},'Building your preview USB');
};
$('import-stems').onclick=()=>{if(required(['source','stem-volume','harmonics','vocals']))run({method:'import_stems',...values(),harmonics:$('harmonics').value,vocals:$('vocals').value,separation_id:$('separation-id').value,harmonics_gain:Number($('harmonics-gain').value),vocals_gain:Number($('vocals-gain').value)},'Importing compatible stems');};
$('setup-engine').onclick=()=>run({method:'setup_engine',...values()},'Setting up the separation engine');
$('download-firmware').onclick=async()=>{const result=await run({method:'download_firmware'},'Getting the official firmware');if(result?.ok)$('firmware').value=result.result.firmware_path;};
$('separate').onclick=()=>{if(required(['source','stem-volume']))run({method:'separate',...values(),device:'cpu'},'Preparing stems');};
$('choose-cache').onclick=async()=>{
  if(!$('source').value){showError('Choose the original track before reading its cache.');return;}
  try{
    const entry=await invoke('pick_path',{kind:'folder'});if(!entry)return;
    const response=await run({method:'inspect_cache',source:$('source').value,entry},'Reading upstream cache');
    if(response?.ok){const data=response.result;for(const name of ['harmonics','vocals']){$(name).value=data[name];$(name+'-gain').value=data[name+'_gain'];}$('separation-id').value=data.separation_id;}
  }catch(error){showError(String(error));}
};
if(!invoke){$('browser-note').classList.remove('hidden');busy(false);}
$('vj-link').onclick=async event=>{if(invoke){event.preventDefault();try{await invoke('open_vj_tools');}catch(error){showError(String(error));}}};

const brandingFiles=[];
function renderBrandingFiles(){
  $('branding-files').replaceChildren();
  brandingFiles.forEach((file,index)=>{
    const item=document.createElement('li');
    const label=document.createElement('span');label.textContent=file.role.toUpperCase()+': '+file.path.split(/[\\/]/).pop()+' ';
    const remove=document.createElement('button');remove.className='branding-remove secondary';remove.textContent='Remove';remove.setAttribute('aria-label','Remove '+label.textContent);remove.onclick=()=>{brandingFiles.splice(index,1);renderBrandingFiles();};
    item.append(label,remove);$('branding-files').append(item);
  });
}
for(const button of document.querySelectorAll('.branding-add'))button.onclick=async()=>{
  try{const path=await invoke('pick_path',{kind:'file'});if(path){brandingFiles.push({role:button.dataset.role,path});renderBrandingFiles();}}
  catch(error){showError(String(error));}
};
$('import-branding').onclick=()=>{
  if(!required(['branding-artist','branding-volume']))return;
  if(!brandingFiles.length){showError('Add a logo, EPK or visual first.');return;}
  run({method:'import_branding',volume:$('branding-volume').value,artist:$('branding-artist').value,website:$('branding-website').value,files:brandingFiles.map(file=>({...file}))},'Preparing DJ branding');
};

$('check-overcue').onclick=async()=>{
  try{const source=await invoke('pick_path',{kind:'file'});if(source)await run({method:'inspect_overcue',source},'Checking OverCue track');}
  catch(error){showError(String(error));}
};
