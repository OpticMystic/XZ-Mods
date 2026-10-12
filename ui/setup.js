// Set up USB: one guided flow. Steps are a fixed list; what each step offers comes from the USB inspection.
// State lives in W. Writes go through the same backend methods the Advanced tools page uses.
(function(){
const STEPS=[['usb','USB'],['loadout','Loadout'],['profile','DJ Profile'],['write','Write'],['done','Done']];
const W={step:'usb',usbs:null,finding:false,usb:null,inspection:null,loader:'auto',games:new Set(),settings:null,settingsStart:null,result:null,writeOrigin:null,changesOpen:false,customOpen:false};
const host=$('wiz'),listeners=[];

function loaderChoice(){
  if(W.loader!=='auto')return W.loader;
  if(!W.inspection?.loader.exists)return 'add';
  const saved=savedModVersion(),recorded=W.inspection.recorded_mod_version;
  return saved&&recorded?.id===saved.id&&recorded?.runtime_sha256===saved.runtime_sha256?'keep':'update';
}
function newGames(){return [...W.games].filter(id=>!W.inspection?.games.find(game=>game.id===id)?.installed);}
// Games the catalog offers (ui/games.js); the backend may list others, which are never offered or planned.
function offeredGames(inspection){return (inspection.games||[]).filter(game=>GAMES.some(entry=>entry.id===game.id));}
// A USB without the loader gets every working free game; Custom installation removes any. A USB that has the
// loader keeps what it holds, so an update never adds games nobody asked for.
function defaultGames(inspection){return new Set(offeredGames(inspection).filter(game=>game.installed||!inspection.loader.exists).map(game=>game.id));}
// The editable settings the loader carries. No wizard step edits them now; games_menu switches write through them.
function startState(inspection){
  W.games=defaultGames(inspection);
  W.settings=inspection.settings.editable?{...inspection.settings.values}:null;W.settingsStart=W.settings?{...W.settings}:null;
}
function settingsChanges(){
  if(!W.settings||!W.settingsStart)return [];
  return Object.keys(W.settings).filter(key=>W.settings[key]!==W.settingsStart[key]);
}
function settingsEditable(){return Boolean(W.inspection?.settings?.editable);}
function writesLoader(){return loaderChoice()!=='keep';}
// The DJ Profile step keeps its draft in dj-profile.js; the Write step saves it with everything else.
function profilePlan(){return window.djProfilePage?.pending()||null;}
function nothingToWrite(){return !writesLoader()&&!newGames().length&&!settingsChanges().length&&!profilePlan()?.plan&&W.inspection?.settings?.revision?.exists!==false;}
function stepIndex(step){return STEPS.findIndex(([id])=>id===step);}
function reachable(step){
  const index=stepIndex(step);
  if(index===0)return true;
  if(!W.inspection)return false;
  if(step==='done')return Boolean(W.result);
  return !W.result||step!=='write';
}
function goto(step){if(!reachable(step))return;W.step=step;render();host.scrollIntoView({block:'start'});}

function reset(){Object.assign(W,{step:'usb',usb:null,inspection:null,loader:'auto',games:new Set(),settings:null,settingsStart:null,result:null,changesOpen:false,customOpen:false});render();}

async function findUsbs(origin){
  W.finding=true;render();
  const response=await run({method:'discover_usbs'},'Looking for USB drives',origin,()=>{});
  W.finding=false;
  if(!response?.ok){W.usbs=[];render();if(response&&!response.cancelled)showError('USB drives could not be listed. '+(response.error||'Choose a folder instead.'),origin);return;}
  W.usbs=response.result.usbs;render();
  return W.usbs;
}
async function chooseUsb(volume,origin){
  const response=await run({method:'inspect_usb',volume},'Reading the USB',origin,()=>{});
  if(!response?.ok){if(response&&!response.cancelled)showError('The USB could not be read. '+(response.error||'Reconnect it and choose it again.'),origin);return;}
  const inspection=response.result;
  Object.assign(W,{usb:volume,inspection,loader:'auto',result:null,changesOpen:false,customOpen:false});
  startState(inspection);
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
  return el('p',{class:'wz-version'},el('span',{text:(saved.hardware_qualified===true?'Hardware-tested':'Not hardware-verified')+(savedPinned()?' · kept on the USB':' · network updates allowed')+' · '}),
    el('button',{type:'button',class:'text-button',text:'Change',onclick:()=>showModVersionEditor('mod-version-select')}));
}
function driveLabel(){return W.usbs?.find(item=>item.volume===W.usb)?.label||W.usb;}
function choices(){
  const update=modUpdate(W.inspection.recorded_mod_version?.id?W.inspection.recorded_mod_version:null,savedModVersion());
  return {loader:loaderChoice(),saved:savedModVersion(),games:W.games,changes:settingsChanges(),settingsEditable:settingsEditable(),versionLine:versionLine(),
    update,changelog:modUpdatePanel(update,{expanded:W.changesOpen,onExpand:()=>{W.changesOpen=true;render();}}),
    setLoader:value=>{W.loader=value;render();},
    profile:profilePlan(),
    chooseGames:()=>{W.customOpen=true;goto('write');},
    editProfile:()=>goto('profile')};
}
// games_menu is a setting only once the loader's schema has it; until then every built-in game stays in the menu.
function gamesMenu(){
  const field=W.inspection.settings?.schema?.fields?.some(item=>item.key==='games_menu');
  if(!field||!settingsEditable()||!W.settings)return null;
  return {value:W.settings.games_menu,set:(bit,on)=>{W.settings={...W.settings,games_menu:on?W.settings.games_menu|1<<bit:W.settings.games_menu&~(1<<bit)};render();}};
}
function gamesChooser(){
  const games=offeredGames(W.inspection);
  return createGamesChooser({inspection:W.inspection,games:W.games,loaderOnUsb:W.inspection.loader.exists||loaderChoice()!=='keep',menu:gamesMenu(),
    toggleGame:(id,on)=>{if(on)W.games.add(id);else W.games.delete(id);render();},
    allGames:()=>{for(const game of games)W.games.add(game.id);render();},
    noGames:()=>{W.games=new Set(games.filter(game=>game.installed).map(game=>game.id));render();}});
}
// Custom installation: everything that works is included by default; open it to take games out.
function customSummary(){
  const adding=newGames().length,kept=offeredGames(W.inspection).filter(game=>game.installed).length;
  return [adding?plural(adding,'free game')+' will be added':'No games will be added',kept?plural(kept,'game')+' on the USB kept':null].filter(Boolean).join(' · ');
}
function customInstall(){
  return el('details',{class:'wz-custom',open:W.customOpen?true:null,ontoggle:event=>{W.customOpen=event.target.open;}},
    el('summary',{},el('span',{class:'wz-custom-title',text:'Custom installation'}),el('small',{class:'wz-custom-summary',text:customSummary()})),
    el('div',{class:'wz-custom-body'},
      el('p',{class:'wz-lead',text:'Games are extras. Nothing here is needed to DJ. Untick anything you do not want on this USB.'}),
      gamesChooser()));
}
function tree(mode){return createUsbTree({nodes:usbLoadout(W.inspection,{driveLabel:driveLabel(),choices:mode==='result'?null:choices()}),mode});}
function stepLoadout(){
  return el('section',{class:'wz-pane'},
    el('h2',{text:'What is on this USB'}),
    el('p',{class:'wz-lead',text:'Green is ready, amber needs an update, grey is not on the USB yet. Tick what you want written. Nothing changes until the Write step.'}),
    tree('edit'),
    W.inspection.requires_copy_to_usb_root?el('p',{class:'notice',text:'This is a folder, not a USB root. Copy the finished autoexec.bin to the USB root afterward.'}):null,
    stepNav());
}
// dj-profile.js draws into the slot (and asks for refreshNav as the draft changes).
function stepProfile(){
  return el('section',{class:'wz-pane wz-profile'},
    el('h2',{text:'Your DJ profile'}),
    el('p',{class:'wz-lead',text:'Your name and logo on the XZ, and for VJ software on the same network. '+(W.inspection.dj_profiles?.count?'Edit it here, or go on.':'It takes a minute. Skip it if you like and add it later.')}),
    el('div',{id:'wz-djp-slot'}),
    stepNav());
}
function refreshNav(){const old=host.querySelector('.wz-nav'),next=stepNav();if(old&&next)old.replaceWith(next);}
async function write(origin){
  if(nothingToWrite()){showError('Nothing is selected to write. Pick a loader action, a DJ profile or a game first.',origin);return;}
  W.writeOrigin=origin;const choice=loaderChoice(),games=newGames(),volume=W.usb,ins=W.inspection,result={loader:null,games:null,settings:null,profile:null,errors:[]};
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
  if(profilePlan()?.plan){
    result.profile=await djProfilePage.writePending(origin);
    if(result.profile&&!result.profile.ok)result.errors.push('Your DJ profile was not saved: '+result.profile.error);
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
    customInstall(),
    nothing?el('p',{class:'notice',role:'status',text:'Nothing to write. Go back and choose a loader action or a DJ profile, add a game under Custom installation, or set up another USB.'}):null,
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'wz-write',disabled:nothing?true:null,text:'Write to USB',onclick:event=>write(event.currentTarget)}),
      el('button',{type:'button',class:'secondary',text:'Back',onclick:()=>goto('profile')})));
}
function stepDone(){
  const r=W.result,games=r.games?.games||[],copied=games.filter(game=>game.ok).map(game=>game.name),failed=games.filter(game=>!game.ok);
  const lines=[];
  if(r.loader)lines.push(r.loader.format==='xz-mods-loader-updated/1'?'Loader updated. The previous loader is backed up on the USB.':'Loader written and verified.');
  if(copied.length)lines.push('Games on USB: '+copied.join(', ')+'.');
  for(const game of failed)lines.push(game.name+' did not download: '+game.error);
  if(r.settings)lines.push('Settings saved to the USB and read back.');
  if(r.profile?.ok)lines.push('DJ profile '+r.profile.name+' saved to the USB.');
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
      r.profile?.ok?el('button',{type:'button',class:'secondary',text:'Make the profile track',onclick:()=>showPage('dj-profile')}):null,
      el('button',{type:'button',class:'secondary',text:'Prepare stems for this USB',onclick:()=>showPage('stems')})));
}
function nav(backStep,nextStep,canNext,label){
  return el('div',{class:'actions wz-nav'},
    // Skipping is allowed but not the suggested path, so it is not drawn as the primary action.
    nextStep?el('button',{type:'button',class:label==='Skip for now'?'secondary':'primary',disabled:canNext?null:true,text:label||(nextStep==='write'?'Review and write':'Next'),onclick:()=>goto(nextStep)}):null,
    backStep?el('button',{type:'button',class:'secondary',text:'Back',onclick:()=>goto(backStep)}):null,
    W.usb?el('span',{class:'wz-usb-now',text:'USB '+W.usb}):null);
}
// Back and Next for the middle steps. A DJ Profile step with no profile and no name says Skip for now.
function stepNav(){
  const profile=profilePlan()||W.inspection?.dj_profiles?.count;
  switch(W.step){
    case 'loadout':return nav('usb','profile',Boolean(savedModVersion())||loaderChoice()==='keep');
    case 'profile':return nav('loadout','write',true,profile?'Review and write':'Skip for now');
  }
  return null;
}
async function findAndChoose(origin){
  const found=await findUsbs(origin);
  if(found?.length===1)await chooseUsb(found[0].volume,origin);
}
function render(){
  const body=W.step==='usb'?stepUsb():W.step==='loadout'?stepLoadout():W.step==='profile'?stepProfile():W.step==='write'?stepWrite():stepDone();
  // The shared job panel may sit inside a pane being replaced; re-place it so its ids stay in the document.
  const hadPanel=host.contains(jobPanel);
  host.replaceChildren(stepBar(),body);
  if(hadPanel||feedbackLive)positionFeedback();
  // The done step lists the outcome itself; a finished job panel would say it twice.
  if(W.step==='done'&&!feedbackLive&&!jobPanel.classList.contains('is-error'))jobPanel.classList.add('hidden');
  for(const listener of listeners)listener();
}
window.setupWizard={state:W,render,refreshNav,goto,reset,chooseUsb,findAndChoose,usbCard,driveLabel,loaderChoice,newGames,settingsChanges,usbLoadout,createUsbTree,onRender:listener=>listeners.push(listener)};
render();
// Advanced tools can hand a USB it already read to the wizard.
window.openSetupFor=inspection=>{Object.assign(W,{usb:inspection.volume,inspection,loader:'auto',result:null,changesOpen:false,customOpen:false});startState(inspection);showPage('setup');goto('loadout');};
})();
