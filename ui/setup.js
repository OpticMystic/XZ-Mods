// Set up USB: one guided flow. Steps are a fixed list; what each step offers comes from the USB inspection.
// State lives in W. Writes go through the same backend methods the Advanced tools page uses.
(function(){
const STEPS=[['usb','USB'],['loadout','Loadout'],['settings','Settings'],['write','Write'],['done','Done']];
const W={step:'usb',usbs:null,finding:false,usb:null,inspection:null,loader:'auto',games:new Set(),settings:null,settingsStart:null,menu:null,result:null,writeOrigin:null};
const host=$('wiz');

function loaderChoice(){
  if(W.loader!=='auto')return W.loader;
  if(!W.inspection?.loader.exists)return 'add';
  const saved=savedModVersion(),recorded=W.inspection.recorded_mod_version;
  return saved&&recorded?.id===saved.id&&recorded?.runtime_sha256===saved.runtime_sha256?'keep':'update';
}
function newGames(){return [...W.games].filter(id=>!W.inspection?.games.find(game=>game.id===id)?.installed);}
function settingsChanges(){
  if(!W.settings||!W.settingsStart)return [];
  return Object.keys(W.settings).filter(key=>W.settings[key]!==W.settingsStart[key]);
}
function settingsEditable(){return Boolean(W.inspection?.settings?.editable);}
function writesLoader(){return loaderChoice()!=='keep';}
function nothingToWrite(){return !writesLoader()&&!newGames().length&&!settingsChanges().length&&W.inspection?.settings?.revision?.exists!==false;}
function stepIndex(step){return STEPS.findIndex(([id])=>id===step);}
function reachable(step){
  const index=stepIndex(step);
  if(index===0)return true;
  if(!W.inspection)return false;
  if(step==='done')return Boolean(W.result);
  return !W.result||step!=='write';
}
function goto(step){if(!reachable(step))return;W.step=step;render();host.scrollIntoView({block:'start'});}

function reset(){Object.assign(W,{step:'usb',usb:null,inspection:null,loader:'auto',games:new Set(),settings:null,settingsStart:null,menu:null,result:null});render();}

async function findUsbs(origin){
  W.finding=true;render();
  const response=await run({method:'discover_usbs'},'Looking for USB drives',origin,()=>{});
  W.finding=false;
  if(!response?.ok){W.usbs=[];render();if(response&&!response.cancelled)showError('USB drives could not be listed. '+(response.error||'Choose a folder instead.'),origin);return;}
  W.usbs=response.result.usbs;render();
}
async function chooseUsb(volume,origin){
  const response=await run({method:'inspect_usb',volume},'Reading the USB',origin,()=>{});
  if(!response?.ok){if(response&&!response.cancelled)showError('The USB could not be read. '+(response.error||'Reconnect it and choose it again.'),origin);return;}
  const inspection=response.result;
  Object.assign(W,{usb:volume,inspection,loader:'auto',games:new Set(inspection.games.filter(game=>game.installed).map(game=>game.id)),result:null,menu:null});
  W.settings=inspection.settings.editable?{...inspection.settings.values}:null;W.settingsStart=W.settings?{...W.settings}:null;
  usbInspection=inspection;renderUsb(inspection);
  W.step='loadout';render();
}
async function pickFolder(origin){
  if(!invoke){showError('Open the desktop app to choose a USB.',origin);return;}
  try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await chooseUsb(volume,origin);}catch(error){showError(String(error),origin);}
}

function stepBar(){
  return el('ol',{class:'wz-steps','aria-label':'Setup steps'},...STEPS.map(([id,text],index)=>{
    const current=id===W.step,done=stepIndex(W.step)>index&&(id!=='done');
    return el('li',{},el('button',{type:'button',class:'wz-step',dataset:{state:current?'current':done?'done':'todo'},'aria-current':current?'step':null,disabled:reachable(id)?null:true,onclick:()=>goto(id)},
      el('span',{class:'wz-step-n',text:done?'✓':String(index+1)}),el('span',{text})));
  }));
}
function usbCard(item){
  const chips=[item.loader?'XZ Mods loader':'No loader yet',item.rekordbox?'Rekordbox library':null,item.games?plural(item.games,'game')+' on USB':null,item.settings?'Saved settings':null].filter(Boolean);
  return el('button',{type:'button',class:'wz-usb',onclick:event=>chooseUsb(item.volume,event.currentTarget)},
    el('span',{class:'wz-usb-icon','aria-hidden':'true'}),
    el('span',{class:'wz-usb-text'},el('strong',{text:item.label}),el('small',{text:item.volume+(item.removable?'':' · not a removable drive')})),
    el('span',{class:'wz-chips'},...chips.map(text=>el('span',{class:'wz-chip',text}))),
    el('span',{class:'wz-go','aria-hidden':'true',text:'Use this USB'}));
}
function stepUsb(){
  const list=W.usbs;
  return el('section',{class:'wz-pane'},
    el('h2',{text:'Plug in the USB you use on your XDJ-XZ'}),
    el('p',{class:'wz-lead',text:'XZ Mods reads what is already on it and only offers the steps that apply. Nothing is written until the Write step.'}),
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'wz-find',disabled:W.finding?true:null,text:W.finding?'Looking…':list?'Look again':'Find my USB',onclick:event=>findUsbs(event.currentTarget)}),
      el('button',{type:'button',class:'secondary',id:'wz-folder',text:'Choose a folder instead',onclick:event=>pickFolder(event.currentTarget)})),
    list&&!list.length?el('p',{class:'notice',role:'status',text:'No USB drive was found. Plug in the USB, wait a moment, then choose Look again. A folder holding a copy of the USB also works.'}):null,
    list?.length?el('div',{class:'wz-usb-list',role:'list'},...list.map(usbCard)):null,
    !list?el('p',{class:'fine',text:'A FAT32 USB that Rekordbox exports to works best. XZ Mods never formats a drive or touches your music.'}):null);
}
function versionLine(){
  const saved=savedModVersion();
  if(!modVersionsReady())return el('div',{class:'wz-setup-needed'},el('p',{text:'No mod version is on this PC yet, so there is nothing to write to a loader.'}),el('button',{type:'button',class:'primary',text:'Set up mod versions',onclick:()=>openModWizard(false)}));
  if(!saved)return el('div',{class:'wz-setup-needed'},el('p',{text:'Choose which XZ Mods build goes on this USB.'}),el('button',{type:'button',class:'primary',text:'Choose mod version',onclick:()=>showModVersionEditor('mod-version-select')}));
  return el('p',{class:'wz-version'},el('span',{text:'Writes '}),el('strong',{text:saved.label||saved.id}),el('span',{text:(saved.hardware_qualified===true?' · hardware-tested':' · not hardware-verified')+(savedPinned()?' · kept on the USB':' · network updates allowed')+' · '}),
    el('button',{type:'button',class:'text-button',text:'Change',onclick:()=>showModVersionEditor('mod-version-select')}));
}
function driveLabel(){return W.usbs?.find(item=>item.volume===W.usb)?.label||W.usb;}
function choices(){
  const games=W.inspection.games;
  return {loader:loaderChoice(),saved:savedModVersion(),games:W.games,changes:settingsChanges(),settingsEditable:settingsEditable(),versionLine:versionLine(),
    setLoader:value=>{W.loader=value;render();},
    toggleGame:(id,on)=>{if(on)W.games.add(id);else W.games.delete(id);render();},
    allGames:()=>{for(const game of games)W.games.add(game.id);render();},
    noGames:()=>{W.games=new Set(games.filter(game=>game.installed).map(game=>game.id));render();},
    editSettings:()=>goto('settings')};
}
function tree(mode){return createUsbTree({nodes:usbLoadout(W.inspection,{driveLabel:driveLabel(),choices:mode==='result'?null:choices()}),mode});}
function stepLoadout(){
  return el('section',{class:'wz-pane'},
    el('h2',{text:'What is on this USB'}),
    el('p',{class:'wz-lead',text:'Green is ready, amber needs an update, grey is not on the USB yet. Tick what you want written. Nothing changes until the Write step.'}),
    tree('edit'),
    W.inspection.requires_copy_to_usb_root?el('p',{class:'notice',text:'This is a folder, not a USB root. Copy the finished autoexec.bin to the USB root afterward.'}):null,
    nav('usb','settings',Boolean(savedModVersion())||loaderChoice()==='keep'));
}
function saveState(){
  const changes=settingsChanges().length;
  return {label:changes?plural(changes,'change')+' · written in step 4':W.inspection.settings.revision.exists?'MATCHES USB':'NEW FILE · DEFAULTS',tone:changes?'dirty':'clean'};
}
function stepSettings(){
  const settings=W.inspection.settings;
  if(!settings.editable)return el('section',{class:'wz-pane'},el('h2',{text:'Settings on this USB'}),el('p',{class:'notice',role:'alert',text:settings.reason}),
    el('p',{text:'Updating the loader writes a fresh settings file with defaults. The settings editor opens once the USB has a readable file.'}),nav('loadout','write',true));
  if(!W.menu)W.menu=createSettingsMenu({schema:settings.schema,values:W.settings,usbLabel:W.usb,onChange:values=>{W.settings=values;renderNavOnly();},save:saveState,onGames:()=>goto('loadout')});
  else W.menu.setValues(W.settings);
  return el('section',{class:'wz-pane wz-settings'},
    el('h2',{text:'Settings, laid out like the MODS menu on your XZ'}),
    el('p',{class:'wz-lead',text:'The same five sections, the same rows. Hover a control to read what it does in the dock. Changes are written to the USB in the Write step.'}),
    W.menu.el,
    nav('loadout','write',true));
}
function renderNavOnly(){const old=host.querySelector('.wz-nav');if(old)old.replaceWith(nav(W.step==='settings'?'loadout':null,W.step==='settings'?'write':null,true));const dock=host.querySelector('.dv-dock');if(dock&&W.menu)W.menu.render();}
async function write(origin){
  if(nothingToWrite()){showError('Nothing is selected to write. Pick a loader action, a game or a setting first.',origin);return;}
  W.writeOrigin=origin;const choice=loaderChoice(),games=newGames(),volume=W.usb,ins=W.inspection,result={loader:null,games:null,settings:null,errors:[]};
  if(choice!=='keep'){
    const version=selectedVersionRequest(origin);if(!version)return;
    const settings=await confirmModSettings(volume,version,origin);if(!settings)return;
    const request=choice==='add'?{method:'prepare_usb',volume,experimental:true,games,...version,...settings}:
      {method:'update_usb',volume,expected_identity:ins.identity,expected_loader:ins.loader,experimental:true,games,...version,...settings};
    const response=await run(request,choice==='add'?'Adding the XZ Mods loader':'Backing up and updating the loader',origin);
    if(!response?.ok)return;
    result.loader=response.result;result.games=response.result.game_setup||null;
    if(response.result.inspection){W.inspection=response.result.inspection;usbInspection=W.inspection;}
  }else if(newGames().length){
    const response=await run({method:'prepare_games_usb',volume,games},'Copying games to the USB',origin);
    if(!response?.ok)return;result.games=response.result;
  }
  if(settingsChanges().length||(W.settings&&ins.settings.revision.exists===false&&choice==='keep')){
    const fresh=await run({method:'read_usb_settings',volume},'Reading the settings file again',origin,()=>{});
    if(!fresh?.ok){showError('The settings file could not be read back, so the settings were not written. '+(fresh?.error||''),origin);W.result=result;goto('done');return;}
    const current=fresh.result.settings;
    if(!current.editable){result.errors.push('Settings were not written: '+current.reason);}
    else{
      const values={...current.values};for(const key of settingsChanges())if(key in values)values[key]=W.settings[key];
      const saved=await run({method:'save_usb_settings',volume,expected_identity:fresh.result.identity,expected_revision:current.revision,values},'Saving settings to the USB',origin);
      if(saved?.ok){result.settings=saved.result;W.settingsStart={...values};W.settings={...values};}
      else result.errors.push('Settings were not written.');
    }
  }
  W.result=result;
  const again=await run({method:'inspect_usb',volume},'Checking the USB',origin,()=>{});
  if(again?.ok){W.inspection=again.result;usbInspection=again.result;renderUsb(again.result);}
  goto('done');
}
function stepWrite(){
  const nothing=nothingToWrite();
  return el('section',{class:'wz-pane'},
    el('h2',{text:'Ready to write'}),
    el('p',{class:'wz-lead',text:'Everything that changes is highlighted. Music, playlists and prepared stems are never touched.'}),
    tree('plan'),
    nothing?el('p',{class:'notice',role:'status',text:'Nothing to write. Go back and choose a loader action, a game or a setting, or set up another USB.'}):null,
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'wz-write',disabled:nothing?true:null,text:'Write to USB',onclick:event=>write(event.currentTarget)}),
      el('button',{type:'button',class:'secondary',text:'Back',onclick:()=>goto('settings')})));
}
function stepDone(){
  const r=W.result,games=r.games?.games||[],copied=games.filter(game=>game.ok).map(game=>game.name),failed=games.filter(game=>!game.ok);
  const lines=[];
  if(r.loader)lines.push(r.loader.format==='xz-mods-loader-updated/1'?'Loader updated. The previous loader is backed up on the USB.':'Loader written and verified.');
  if(copied.length)lines.push('Games on USB: '+copied.join(', ')+'.');
  for(const game of failed)lines.push(game.name+' did not download: '+game.error);
  if(r.settings)lines.push('Settings saved to the USB and read back.');
  lines.push(...r.errors);
  return el('section',{class:'wz-pane'},
    el('h2',{text:failed.length||r.errors.length?'Finished, with something to check':'Your USB is ready'}),
    el('ul',{class:'wz-done'},...lines.map(text=>el('li',{text}))),
    tree('result'),
    el('div',{class:'wz-next'},el('h3',{text:'On the XZ'}),el('ol',{},el('li',{text:'Eject the USB here and plug it into USB 1 or USB 2 on the XDJ-XZ, then power it on.'}),
      el('li',{text:'Press the REKORDBOX key or touch MODS to open the menu. GENERAL is the first tab.'}),el('li',{text:'Test the loader before you use it in a set.'}))),
    el('div',{class:'actions'},
      failed.length?el('button',{type:'button',class:'primary',text:'Retry the failed games',onclick:async event=>{const response=await run({method:'prepare_games_usb',volume:W.usb,games:failed.map(game=>game.game)},'Retrying game downloads',event.currentTarget);if(response?.ok){W.result={...W.result,games:response.result};render();}}}):null,
      el('button',{type:'button',class:failed.length?'secondary':'primary',text:'Set up another USB',onclick:reset}),
      el('button',{type:'button',class:'secondary',text:'Prepare stems for this USB',onclick:()=>showPage('stems')})));
}
function nav(backStep,nextStep,canNext){
  return el('div',{class:'actions wz-nav'},
    nextStep?el('button',{type:'button',class:'primary',disabled:canNext?null:true,text:nextStep==='write'?'Review and write':'Next',onclick:()=>goto(nextStep)}):null,
    backStep?el('button',{type:'button',class:'secondary',text:'Back',onclick:()=>goto(backStep)}):null,
    W.usb?el('span',{class:'wz-usb-now',text:'USB '+W.usb}):null);
}
function render(){
  const body=W.step==='usb'?stepUsb():W.step==='loadout'?stepLoadout():W.step==='settings'?stepSettings():W.step==='write'?stepWrite():stepDone();
  // The shared job panel may sit inside the pane being replaced; re-place it so its ids stay in the document.
  const hadPanel=host.contains(jobPanel);
  host.replaceChildren(stepBar(),body);
  if(hadPanel||feedbackLive)positionFeedback();
  // The done step lists the outcome itself; a finished job panel would say it twice.
  if(W.step==='done'&&!feedbackLive&&!jobPanel.classList.contains('is-error'))jobPanel.classList.add('hidden');
}
window.setupWizard={state:W,render,goto,reset,chooseUsb,loaderChoice,newGames,settingsChanges,usbLoadout,createUsbTree};
render();
// Advanced tools can hand a USB it already read to the wizard.
window.openSetupFor=inspection=>{W.usb=inspection.volume;W.inspection=inspection;W.loader='auto';W.games=new Set(inspection.games.filter(game=>game.installed).map(game=>game.id));W.settings=inspection.settings.editable?{...inspection.settings.values}:null;W.settingsStart=W.settings?{...W.settings}:null;W.menu=null;W.result=null;showPage('setup');goto('loadout');};
})();
