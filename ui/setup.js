// USB home: a device utility. W.view is the one state that decides what shows:
//   find   -> no USB chosen; USBs are found automatically and a single one opens by itself
//   device -> the chosen USB's dashboard: status, one obvious action, tiles
//   flow   -> a short write wizard; W.flow.steps lists the steps that apply and W.step is the current one
// Writes go through the same backend requests, in the same order, as before.
(function(){
const FLOWS={
  install:{title:'Install XZ Mods',loader:'auto'},
  update:{title:'Update USB',loader:'auto'},
  profile:{title:'Save DJ profile',loader:'keep'},
  games:{title:'Add games',loader:'keep'},
  quad:{title:'Quad standalone mixer',loader:'keep'}
};
const POLL_MS=2000;
const W={view:'find',flow:null,step:null,usbs:null,finding:false,opening:null,eject:null,usb:null,inspection:null,loader:'auto',games:new Set(),settings:null,settingsStart:null,result:null,
  quadReturn:null,quadConfirmed:false,quadPaused:false,
  changesOpen:false,changesAll:false,gamesOpen:false,filesOpen:false};
const host=$('wiz'),listeners=[];
// Every run started here reports beside this one node, which survives re-renders.
const jobSlot=el('div',{class:'wz-job-slot'});
const SVG_NS='http://www.w3.org/2000/svg';
function icon(name,extra){
  const svg=document.createElementNS(SVG_NS,'svg'),use=document.createElementNS(SVG_NS,'use');
  svg.setAttribute('class','icon'+(extra?' '+extra:''));svg.setAttribute('aria-hidden','true');use.setAttribute('href','#i-'+name);svg.append(use);
  return svg;
}
const gb=bytes=>String(+(bytes/1e9).toFixed(bytes>=1e11?0:1))+' GB';
const listText=items=>items.length>1?items.slice(0,-1).join(', ')+' and '+items.at(-1):items.join('');

function loaderChoice(){
  if(W.loader!=='auto')return W.loader;
  if(!W.inspection?.loader.exists)return 'add';
  const saved=savedModVersion(),recorded=W.inspection.recorded_mod_version;
  return saved&&recorded?.id===saved.id&&recorded?.runtime_sha256===saved.runtime_sha256?'keep':'update';
}
function newGames(){return [...W.games].filter(id=>!W.inspection?.games.find(game=>game.id===id)?.installed);}
// Games the catalog offers (ui/games.js); the backend may list others, which are never offered or planned.
function offeredGames(inspection){return (inspection.games||[]).filter(game=>GAMES.some(entry=>entry.id===game.id));}
function installedGames(inspection){return new Set(offeredGames(inspection).filter(game=>game.installed).map(game=>game.id));}
// A USB without the loader gets every working free game; Change games removes any. A USB that has the
// loader keeps what it holds, so an update never adds games nobody asked for.
function defaultGames(inspection){return new Set(offeredGames(inspection).filter(game=>game.installed||!inspection.loader.exists).map(game=>game.id));}
// The editable settings the loader carries. Only the games_menu switches in Change games edit them.
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
// The DJ profile step keeps its draft in dj-profile.js; the write saves it with everything else.
function profilePlan(){return window.djProfilePage?.pending()||null;}
function nothingToWrite(){return !writesLoader()&&!newGames().length&&!settingsChanges().length&&!profilePlan()?.plan&&W.inspection?.settings?.revision?.exists!==false;}
function driveLabel(){return W.usbs?.find(item=>item.volume===W.usb)?.label||W.usb;}

// --- finding the USB ---

// discovering is the in-flight discover_usbs, so a user action here can wait for it.
let discovering=null,firstLook=null,pollTimer=null,polling=false,volumesSig=null,dismissed=null;
const usbSig=list=>list.map(item=>item.volume).sort().join('|');
async function ready(){while(discovering)await discovering;await idle();}
// origin is where an error shows: this page's job slot, or the DJ Profile page's button when it asked.
function discover(user,origin=jobSlot){
  discovering??=(async()=>{
    W.finding=true;render();
    const response=await run({method:'discover_usbs'},'Looking for USB drives',origin,()=>{});
    W.finding=false;
    if(!response){render();return null;}
    if(!response.ok){W.usbs??=[];render();if(user&&!response.cancelled)showError('USB drives could not be listed. '+(response.error||'Choose a folder instead.'),origin);return null;}
    W.usbs=response.result.usbs;
    // Once the set differs from the one the DJ stepped away from (it was unplugged, say), a single USB may open by itself again.
    if(usbSig(W.usbs)!==dismissed)dismissed=null;
    render();
    return W.usbs;
  })().finally(()=>{discovering=null;});
  return discovering;
}
// One USB opens by itself, unless it is the set the DJ just stepped away from with Change USB.
async function lookForUsbs(user){
  if(user)await idle();
  const list=await discover(user);
  if(W.view==='find'&&list?.length===1&&usbSig(list)!==dismissed)await chooseUsb(list[0].volume);
  return list;
}
function pollWanted(){return Boolean(invoke)&&W.view==='find'&&!$('setup').classList.contains('hidden');}
function schedulePoll(){
  if(!pollWanted()){clearTimeout(pollTimer);pollTimer=null;return;}
  if(!pollTimer&&!polling)pollTimer=setTimeout(poll,POLL_MS);
}
// list_volumes is a cheap host call with no job, so it never holds the job slot; a changed set triggers a real discover.
// The new set is only remembered once a discover has read it, so a refused or failed discover is retried next tick.
async function poll(){
  pollTimer=null;
  if(!pollWanted())return;
  polling=true;
  const volumes=await invoke('list_volumes').catch(()=>null);
  try{
    const sig=volumes?.join('|');
    if(volumes&&sig!==volumesSig&&pollWanted()&&activeJob===null&&!discovering&&await lookForUsbs(false))volumesSig=sig;
  }finally{
    polling=false;
    // A host without list_volumes cannot be polled; the first look and Look again still work there.
    if(volumes)schedulePoll();
  }
}
function shown(){
  if(invoke&&!firstLook)firstLook=(async()=>{
    let sig=null;
    try{sig=(await invoke('list_volumes')).join('|');}catch{}
    await idle();
    if(await lookForUsbs(false))volumesSig=sig;
  })();
  render();
}
// A queued choice never pulls the DJ out of a write flow they started in the meantime.
// W.opening marks the card busy from the click itself: the read may first wait for a background USB scan,
// and the read is a quiet job, so without it nothing on screen would change for several seconds.
async function chooseUsb(volume,origin=jobSlot){
  if(W.opening)return;
  W.opening=volume;render();
  try{
    await ready();
    if(W.view==='flow')return;
    const response=await run({method:'inspect_usb',volume},'Reading the USB',origin,()=>{});
    if(!response?.ok){if(response&&!response.cancelled)showError('The USB could not be read. '+(response.error||'Reconnect it and choose it again.'),origin);return;}
    if(W.view==='flow')return;
    W.opening=null;
    open(volume,response.result);
    await followNewest();
  }finally{
    if(W.opening===volume){W.opening=null;render();}
  }
}
function open(volume,inspection){
  if(W.flow?.steps.includes('profile'))window.djProfilePage?.discard();
  dismissed=null;
  startState(inspection);
  renderUsb(inspection);
  go({view:'device',flow:null,step:null,usb:volume,inspection,loader:'auto',result:null,eject:null,changesOpen:false,changesAll:false,gamesOpen:false,filesOpen:false});
}
async function pickFolder(){
  if(!invoke){showError('Open the desktop app to choose a USB.',jobSlot);return;}
  try{const volume=await invoke('pick_path',{kind:'folder'});if(volume)await chooseUsb(volume);}catch(error){showError(String(error),jobSlot);}
}
function changeUsb(){
  dismissed=W.usbs?.length?usbSig(W.usbs):W.usb;
  go({view:'find',usb:null,inspection:null,flow:null,step:null,result:null});
  // The cards may describe the USB as it was before a write; list again.
  lookForUsbs(false);
}
// The DJ Profile page's Find my USB: an explicit request, so a single USB opens even right after Change USB.
async function findAndChoose(origin){
  dismissed=null;
  await idle();
  const list=await discover(true,origin);
  if(list?.length===1)await chooseUsb(list[0].volume,origin);
}
// A view change drops a finished job panel from the view before; a running one stays.
function go(patch){
  if(!feedbackLive)jobPanel.classList.add('hidden');
  Object.assign(W,patch);render();
}

// --- builds ---

const bundledVersion=()=>modVersionById(modInventory?.bundled_id);
const sameRuntime=(a,b)=>Boolean(a&&b&&a.runtime_sha256===b.runtime_sha256);
const isIncluded=version=>/^Included development build /.test(version?.label||'');
function buildName(version){return isIncluded(version)?'Development build '+String(version.runtime_sha256||'').slice(0,8):version?.label||version?.id||'';}
let buildSaving=false,buildNote='';
async function saveBuild(id,pinned,origin){
  if(buildSaving||!modInventory)return;
  if(modVersionById(id)?.network_updates_supported===false)pinned=true;
  buildSaving=true;render();
  await ready();
  const result=await run({method:'select_mod_version',mod_version_id:id,pinned,expected_revision:modInventory.selected.revision},'Saving the build choice',origin,()=>{});
  buildSaving=false;
  if(result?.ok)applyModInventory(result.result);
  else{if(result&&!result.cancelled)showError('The build choice was not saved. '+(result.error||''),origin);await loadModVersions();}
  render();
}
// A saved choice that is only an older build this app once included follows the newest one; a build picked
// on purpose (a release, a preview, an import) stays.
async function followNewest(){
  const saved=savedModVersion(),newest=bundledVersion();
  if(!saved||!newest||sameRuntime(saved,newest)||!isIncluded(saved)||String(saved.archived_at||'')>=String(newest.archived_at||''))return;
  await saveBuild(newest.id,savedPinned(),null);
  if(sameRuntime(savedModVersion(),newest)){buildNote='Switched to the newest build, which came with this app.';render();}
}

// --- find view ---

function usbCard(item){
  const chips=[item.loader?'XZ Mods installed':'No XZ Mods yet',item.rekordbox?'Rekordbox USB':null].filter(Boolean);
  const opening=W.opening===item.volume,waiting=Boolean(W.opening)&&!opening;
  return el('button',{type:'button',class:'wz-usb',dataset:{state:opening?'opening':waiting?'waiting':null},'aria-busy':opening?'true':null,disabled:waiting?true:null,
      onclick:event=>chooseUsb(item.volume,host.contains(event.currentTarget)?jobSlot:event.currentTarget)},
    el('span',{class:'wz-usb-icon'},icon('usb')),
    el('span',{class:'wz-usb-text'},el('strong',{text:item.label}),el('small',{text:item.volume+(item.removable?'':' · not a removable drive')})),
    opening?el('span',{class:'wz-opening',role:'status',text:'Reading the USB…'}):
      el('span',{class:'wz-chips'},...chips.map((text,index)=>el('span',{class:'wz-chip',dataset:{tone:index===0&&item.loader?'on':null},text}))),
    opening?el('span',{class:'wz-spinner wz-go','aria-hidden':'true'}):icon('chevron','wz-go'));
}
function quietActions(){
  // A folder, or a USB not in the list, being opened has no card to show it on.
  if(W.opening&&!W.usbs?.some(item=>item.volume===W.opening))
    return el('div',{class:'wz-quiet'},el('span',{class:'wz-looking',role:'status'},el('span',{class:'wz-spinner','aria-hidden':'true'}),'Reading '+W.opening+'…'));
  return el('div',{class:'wz-quiet'},
    W.finding?el('span',{class:'wz-looking',role:'status'},el('span',{class:'wz-spinner','aria-hidden':'true'}),'Looking…'):
      el('button',{type:'button',class:'text-button',id:'wz-look',text:'Look again',onclick:()=>lookForUsbs(true)}),
    el('button',{type:'button',class:'text-button',id:'wz-folder',text:'Choose a folder instead',onclick:pickFolder}));
}
function viewFind(){
  const list=W.usbs||[];
  if(list.length)return el('section',{class:'wz-find wz-find-list'},
    el('h2',{text:list.length>1?'Choose your XZ USB':'Your XZ USB'}),
    el('div',{class:'wz-usb-list',role:'list'},...list.map(usbCard)),
    quietActions(),quadEntry(),jobSlot);
  return el('section',{class:'wz-find wz-empty',dataset:{looking:W.finding?'1':null}},
    $('tpl-usb-art').content.firstElementChild.cloneNode(true),
    el('h2',{text:'Plug in your XZ USB'}),
    el('p',{class:'wz-lead',text:'Or any USB stick formatted FAT32. XZ Mods finds it as soon as it is connected. It never formats a drive or touches your music.'}),
    quietActions(),quadEntry(),jobSlot);
}

// --- device view ---

function driveHeader(){
  const ins=W.inspection,space=ins.space||{},known=space.free_bytes!=null&&space.total_bytes>0;
  const meta=[ins.filesystem,known?gb(space.free_bytes)+' free of '+gb(space.total_bytes):''].filter(Boolean).join(' · ');
  return el('div',{class:'wz-drive'},
    el('span',{class:'wz-drive-icon'},icon('usb')),
    el('div',{class:'wz-drive-text'},
      el('div',{class:'wz-drive-name'},el('h2',{text:driveLabel()}),el('span',{class:'wz-mono',text:W.usb})),
      meta?el('p',{class:'wz-drive-meta',text:meta}):null,
      known?el('div',{class:'wz-space','aria-hidden':'true'},el('span',{style:'--used:'+(100*(1-space.free_bytes/space.total_bytes)).toFixed(1)+'%'})):null),
    el('button',{type:'button',class:'text-button wz-change',text:'Change USB',onclick:changeUsb}));
}
function changesBox(){
  const update=modUpdate(W.inspection.recorded_mod_version?.id?W.inspection.recorded_mod_version:null,savedModVersion());
  const panel=modUpdatePanel(update,{expanded:W.changesAll,onExpand:()=>{W.changesAll=true;render();}});
  if(!panel||update.kind==='unavailable')return panel;
  const count=update.added.length+update.removed.length;
  return el('details',{class:'wz-changes',open:W.changesOpen?true:null,ontoggle:event=>{W.changesOpen=event.target.open;}},
    el('summary',{},el('span',{class:'wz-changes-count',text:plural(count,'change')}),' · ',el('span',{class:'wz-changes-see',text:'See what changes'}),el('span',{class:'wz-changes-hide',text:'Hide changes'})),panel);
}
function installReady(){return loaderChoice()==='add'&&Boolean(savedModVersion());}
// One status, one action. Without a saved build the action is the one-button build setup.
function status(){
  const ins=W.inspection,saved=savedModVersion(),recorded=ins.recorded_mod_version?.id?ins.recorded_mod_version:null;
  const missing='XZ Mods is not on this USB yet',present='XZ Mods is on this USB';
  if(!modVersionsReady())return {tone:'setup',title:ins.loader.exists?present:missing,line:'No XZ Mods build is on this computer yet, so there is nothing to write.',
    action:{label:'Set up mod versions',run:()=>openModWizard(false)}};
  if(!saved)return {tone:'setup',title:ins.loader.exists?present:missing,line:'Choose which XZ Mods build goes on this USB.',
    action:{label:'Use the newest build',run:()=>saveBuild(bundledVersion()?.id||modInventory.versions[0].id,true,jobSlot)}};
  switch(loaderChoice()){
    case 'add':return {tone:'add',title:missing,line:'Adds the loader, free games and your DJ profile. Music and playlists stay as they are.',action:{label:'Install XZ Mods',run:()=>startFlow('install')}};
    case 'update':return {tone:'update',title:'Update available',line:(recorded?'This USB has '+buildName(recorded)+'. ':'The XZ Mods version on this USB is not known. ')+'Updating backs up its loader and adds '+buildName(saved)+'.',
      extra:changesBox(),action:{label:'Update USB',run:()=>startFlow('update')}};
    default:return {tone:'keep',title:'Up to date',line:buildName(saved)+' is on this USB.'};
  }
}
const STATUS_ICONS={setup:'gear',add:'download',update:'update',keep:'check'};
function statusBlock(){
  const s=status();
  return el('div',{class:'wz-status',dataset:{tone:s.tone}},
    el('span',{class:'wz-status-icon'},icon(STATUS_ICONS[s.tone])),
    el('div',{class:'wz-status-text'},el('h3',{text:s.title}),el('p',{text:s.line}),
      s.action?el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'wz-primary',disabled:buildSaving?true:null,text:s.action.label,onclick:s.action.run})):null,
      s.extra||null));
}
function tile(id,iconName,label,value,action,onclick){
  return el('button',{type:'button',class:'wz-tile',dataset:{tile:id},onclick},
    el('span',{class:'wz-tile-icon'},icon(iconName)),
    el('span',{class:'wz-tile-label',text:label}),
    el('strong',{class:'wz-tile-value',text:value}),
    el('span',{class:'wz-tile-action'},action,icon('chevron')));
}
// DJ profile and Games write on their own, so a profile or a game needs no loader change. On a USB that is
// ready for XZ Mods they join the install instead.
function tiles(){
  const ins=W.inspection,profiles=ins.dj_profiles||{count:0},games=installedGames(ins).size,stems=ins.stems?.tracks||0;
  return el('div',{class:'wz-tiles'},
    tile('profile','profile','DJ profile',profiles.active_name||(profiles.count?plural(profiles.count,'profile'):'Not set'),profiles.count?'Edit':'Add',
      ()=>startFlow(installReady()?'install':'profile',{profile:true})),
    tile('games','games','Games',games?plural(games,'game')+' on USB':'None','Choose',()=>startFlow(installReady()?'install':'games',{gamesOpen:true})),
    tile('stems','stems','Stems',stems?plural(stems,'track')+' prepared':'None yet','Prepare',()=>showPage('stems')));
}
function buildLine(){
  const saved=savedModVersion();
  return el('p',{class:'wz-build-line'},
    saved?['Build: ',el('span',{class:'wz-build-name',text:buildName(saved)}),' · ']:null,
    el('button',{type:'button',class:'text-button',text:saved?'Change':'Choose a build',onclick:()=>showPage('advanced')}),
    buildNote?el('span',{class:'wz-build-note',role:'status',text:buildNote}):null);
}
function tree(){return createUsbTree({nodes:usbLoadout(W.inspection,{driveLabel:driveLabel()})});}
function viewDevice(){
  return el('section',{class:'wz-device'},
    el('div',{class:'wz-card'},driveHeader(),statusBlock(),jobSlot),
    W.inspection.requires_copy_to_usb_root?el('p',{class:'notice',text:'This is a folder, not a USB. Copy the finished autoexec.bin to the USB afterward.'}):null,
    tiles(),
    buildLine(),quadEntry(),
    el('details',{class:'wz-files',open:W.filesOpen?true:null,ontoggle:event=>{W.filesOpen=event.target.open;}},
      el('summary',{text:'Show every file on this USB'}),tree()));
}

// --- eject ---

// The choice to eject on finish is remembered on this computer; it starts off.
const EJECT_KEY='xz-eject-after-write';
function ejectAfterWrite(){try{return localStorage.getItem(EJECT_KEY)==='1';}catch{return false;}}
function setEjectAfterWrite(on){try{localStorage.setItem(EJECT_KEY,on?'1':'0');}catch{}render();}
// Only a real drive in the desktop app can be ejected; a chosen folder cannot.
// A drive the USB list did not mark removable (an internal disk, a folder) is never offered.
function canEject(){return Boolean(invoke)&&Boolean(W.usb)&&!W.inspection?.requires_copy_to_usb_root&&Boolean(W.usbs?.find(item=>item.volume===W.usb)?.removable);}
function ejectChoice(later=false){
  if(!canEject())return null;
  return el('label',{class:'check wz-eject-auto'},
    el('input',{type:'checkbox',id:'wz-eject-auto',checked:ejectAfterWrite()?true:null,onchange:event=>setEjectAfterWrite(event.target.checked)}),
    el('span',{},el('span',{text:later?'Eject the USB by itself next time':'Eject the USB when writing finishes'}),el('small',{text:'Then you can unplug it and take it straight to the XZ.'})));
}
async function ejectUsb(){
  if(!canEject()||W.eject?.state==='ejecting'||W.eject?.state==='done')return;
  await idle();
  W.eject={state:'ejecting'};render();
  try{await invoke('eject_volume',{volume:W.usb});W.eject={state:'done'};}
  catch(error){W.eject={state:'error',message:String(error)};}
  render();
}
// After an eject the USB is gone, so Done goes back to finding one.
function afterEject(){
  dismissed=null;
  go({view:'find',usb:null,inspection:null,flow:null,step:null,result:null,eject:null,usbs:null});
  lookForUsbs(false);
}

// --- flow ---

function startFlow(kind,{profile=false,gamesOpen=false}={}){
  const ins=W.inspection,withProfile=profile||kind==='profile'||(kind==='install'&&!ins.dj_profiles?.count);
  startState(ins);
  // The screen choice comes with a loader install or update, after the profile and before the review.
  const withLook=(kind==='install'||kind==='update')&&lookAvailable();
  const steps=[...(withProfile?['profile']:[]),...(withLook?['look']:[]),...((kind==='install'||kind==='update')?['quad']:[]),'review','writing','done',...((kind==='install'||kind==='update')?['support']:[])];
  if(kind==='profile')W.games=installedGames(ins);
  go({view:'flow',flow:{kind,steps},step:gamesOpen?'review':steps[0],loader:FLOWS[kind].loader,result:null,gamesOpen,quadConfirmed:false,quadPaused:false});focusStep();
}
function backToDevice(){
  if(W.flow?.steps.includes('profile')&&W.step!=='done')window.djProfilePage?.discard();
  startState(W.inspection);
  go({view:'device',flow:null,step:null,loader:'auto',result:null,gamesOpen:false});
}
function goto(step){
  if(W.view!=='flow'||!W.flow.steps.includes(step))return;
  W.step=step;render();host.scrollIntoView({block:'start'});focusStep();
}
function previous(){const steps=W.flow.steps,index=steps.indexOf(W.step);if(index>0)goto(steps[index-1]);else backToDevice();}
function flowHead(){
  const steps=W.flow.steps,index=steps.indexOf(W.step),cancellable=W.step==='profile'||W.step==='look'||W.step==='review'||W.step==='quad';
  return el('div',{class:'wz-flow-head'},
    el('div',{class:'wz-flow-title'},el('h2',{text:FLOWS[W.flow.kind].title}),W.flow.kind==='quad'?null:el('span',{class:'wz-mono',text:driveLabel()===W.usb?W.usb:driveLabel()+' · '+W.usb})),
    el('div',{class:'wz-flow-progress'},
      el('span',{class:'wz-flow-count',text:'Step '+(index+1)+' of '+steps.length}),
      el('ol',{class:'wz-dots','aria-hidden':'true'},...steps.map((step,i)=>el('li',{dataset:{state:i<index?'done':i===index?'current':'todo'}})))),
    cancellable?el('button',{type:'button',class:'text-button wz-cancel',text:'Cancel',onclick:W.flow.kind==='quad'?closeQuad:backToDevice}):null);
}
function navButtons(next,label,primary=true){
  return el('div',{class:'actions wz-nav'},
    el('button',{type:'button',class:primary?'primary':'secondary',text:label,onclick:next}),
    el('button',{type:'button',class:'secondary',text:'Back',onclick:previous}));
}
// Skipping is allowed but not the suggested path, so Skip for now is not drawn as the primary action.
function stepNav(){
  if(W.view!=='flow'||W.step!=='profile')return null;
  const profile=profilePlan()||W.inspection?.dj_profiles?.count;
  const next=W.flow.steps[W.flow.steps.indexOf('profile')+1];
  return navButtons(()=>goto(next),profile?'Next':'Skip for now',Boolean(profile));
}
function refreshNav(){const old=host.querySelector('.wz-nav'),next=stepNav();if(old&&next)old.replaceWith(next);}
// dj-profile.js draws into the slot (and asks for refreshNav as the draft changes).
function stepProfile(){
  return [el('h3',{text:'Your DJ profile'}),
    el('p',{class:'wz-lead',text:'Your name and logo on the XZ, and for VJ software on the same network. '+(W.inspection.dj_profiles?.count?'Edit it here, or go on.':'It takes a minute. Skip it if you like and add it later.')}),
    el('div',{id:'wz-djp-slot'}),
    stepNav()];
}

// This guide records a user's observation only. It makes no host/device requests or settings changes.
function focusStep(){const heading=host.querySelector('.wz-flow-body h3');if(heading){heading.tabIndex=-1;heading.focus({preventScroll:true});}}
function quadEntry(){return el('div',{class:'wz-quad-entry'},
  el('span',{},el('strong',{text:'Quad standalone mixer'}),el('small',{text:'Experimental beta. Requires an exact paired XDJ-XZ 1.26 four-deck loader and bridge. Reopen this guide any time.'})),
  el('button',{type:'button',class:'secondary',id:'wz-quad-open',text:'Set up four decks',onclick:openQuad}));}
function openQuad(){
  W.quadReturn={view:W.view,flow:W.flow,step:W.step};
  go({view:'flow',flow:{kind:'quad',steps:['quad']},step:'quad',quadConfirmed:false,quadPaused:false});focusStep();
}
function closeQuad(){
  const target=W.quadReturn;W.quadReturn=null;
  go(target||{view:W.inspection?'device':'find',flow:null,step:null});
  host.querySelector('#wz-quad-open')?.focus({preventScroll:true});
}
function quadNext(){if(W.flow.kind==='quad')closeQuad();else goto(W.flow.steps[W.flow.steps.indexOf('quad')+1]);}
function quadStatus(){return W.quadConfirmed?'User confirmation: you saw four decks. This app has not detected or verified the cable connection.':'Connection not checked by this app. Confirm the result on the XZ.';}
// The cable guide's picture: the deck's own loopback-cable explainer (source/.../mods/ui/cable_anim.c), rendered for
// the app by tools/build_cable_media.py. Muted, looping, no controls; with reduced motion, its first frame.
const QUAD_VIDEO='assets/wizard/loopback-cable.mp4',QUAD_POSTER='assets/wizard/loopback-cable-poster.webp';
const QUAD_TITLE='One USB-B to USB-A cable loops back into the same XDJ-XZ';
const QUAD_DESC='The XDJ-XZ seen from behind and above, with one cable from the rear USB-B socket over the back edge into top USB1. The view turns to the rear panel, where the square USB-B plug with beveled top corners comes out to show the socket and goes back in, then to the top, where the flat USB-A plug lifts out of USB1 and goes back in. USB2 is beside it. The loop ends where it began, both ends connected. No computer is involved.';
function quadCable(){
  const still=reducedMotion();
  const media=still?el('img',{class:'wz-quad-video',src:QUAD_POSTER,alt:QUAD_TITLE+'. '+QUAD_DESC,width:1280,height:400})
    :el('video',{class:'wz-quad-video',id:'wz-quad-video',src:QUAD_VIDEO,poster:QUAD_POSTER,muted:true,loop:true,autoplay:W.quadPaused?null:true,playsinline:true,
      preload:'auto',width:1280,height:400,'aria-label':QUAD_TITLE,'aria-describedby':'wz-quad-desc'});
  if(!still){media.muted=true;if(!W.quadPaused)media.play?.().catch(()=>{});}
  return [media,el('p',{class:'visually-hidden',id:'wz-quad-desc',text:QUAD_DESC})];
}
function stepQuad(){
  return [el('h3',{text:'Four decks, one standalone mixer'}),
    el('p',{class:'wz-lead',text:'Decks 3 and 4 send audio back to mixer CH3 and CH4 through a USB-B to USB-A cable. This is the cable guide for after installation.'}),
    el('p',{class:'notice wz-quad-prereq',text:'Requires the paired XDJ-XZ 1.26 quad beta resources. The matching loader starts the automatic bridge; this cable guide does not install it or verify the connection. Other saved versions may not include quad support.'}),
    el('figure',{class:'wz-quad-art',dataset:{paused:W.quadPaused?'1':null}},quadCable(),
      el('figcaption',{},el('span',{text:'Rear USB-B → top USB1 · Music stays in USB2'}),
        reducedMotion()?null:el('button',{type:'button',class:'text-button',id:'wz-quad-motion','aria-pressed':String(W.quadPaused),text:W.quadPaused?'Play cable animation':'Pause cable animation',onclick:event=>{
          W.quadPaused=!W.quadPaused;
          const figure=event.currentTarget.closest('figure'),video=figure.querySelector('video');
          if(W.quadPaused){figure.dataset.paused='1';video?.pause();}else{delete figure.dataset.paused;video?.play?.().catch(()=>{});}
          event.currentTarget.textContent=W.quadPaused?'Play cable animation':'Pause cable animation';event.currentTarget.setAttribute('aria-pressed',String(W.quadPaused));
        }}))),
    el('ol',{class:'wz-quad-steps'},
      el('li',{},el('strong',{text:'Boot first. '}),'Keep the boot stick in USB1 until XZ Mods has finished starting. Keep the music stick in USB2. Leave the square, beveled USB-B end plugged into the rear USB-B socket; leave its rectangular USB-A end free.'),
      el('li',{},el('strong',{text:'Stop every deck. '}),'Hold USB1 STOP for more than 2 seconds. Wait until the USB1 indicator is off, then remove only the boot stick.'),
      el('li',{},el('strong',{text:'Loop back. '}),'Insert only the free USB-A cable end into top USB1. It connects back to the XZ, not a computer. Keep music USB2 connected.'),
      el('li',{},el('strong',{text:'Check on the XZ. '}),'Four decks should appear when the route is ready. Set the CH3 and CH4 input selectors to USB if required. Press left SHIFT twice to switch 1/3; right SHIFT twice to switch 2/4.')),
    el('label',{class:'check wz-quad-confirm'},el('input',{type:'checkbox',id:'wz-quad-confirm',checked:W.quadConfirmed?true:null,onchange:event=>{W.quadConfirmed=event.currentTarget.checked;$('wz-quad-status').textContent=quadStatus();}}),'I can see four decks on the XZ'),
    el('p',{class:'wz-quad-status',id:'wz-quad-status',role:'status',text:quadStatus()}),
    el('details',{class:'wz-quad-trouble'},el('summary',{text:'Still two decks, or no CH3/CH4 audio?'}),
      el('p',{text:'Confirm the matching four-deck loader and bridge are running. With all decks stopped, check rear USB-B → top USB1, music USB2 and the CH3/CH4 USB selectors. If the cable was connected before boot, leave the rear USB-B end in place and reconnect only the free USB-A end after startup. Stop every deck before reconnecting.'}),
      el('p',{text:'These USB1 loopback / USB2 music instructions describe the paired quad beta setup. Other loader/bridge configurations may differ. This guide does not detect your configuration. Reopen it from USB → Set up four decks.'})),
    el('p',{class:'wz-mono',text:'Keep the music USB connected. Music removal and full RAM carry are unsupported. Four-deck stems are still incomplete.'}),
    el('div',{class:'actions wz-nav'},
      el('button',{type:'button',class:'secondary',text:'Back',onclick:W.flow.kind==='quad'?closeQuad:previous}),
      el('button',{type:'button',class:'primary',text:'Next',onclick:quadNext}),
      el('button',{type:'button',class:'text-button',text:'Skip for now',onclick:quadNext}))];
}

// --- the screen the XZ starts with ---
// One setting decides it: interface (1 new, 0 standard), the same value MODS > APPEARANCE > INTERFACE
// changes on the deck. Choosing the standard interface leaves classic_theme as the USB has it.
const LOOK_NEW=1,LOOK_STANDARD=0;
const reducedMotion=()=>matchMedia('(prefers-reduced-motion: reduce)').matches;
function lookAvailable(){
  return settingsEditable()&&Boolean(W.settings)&&'interface' in W.settings&&Boolean(W.inspection.settings.schema?.fields?.some(field=>field.key==='interface'));
}
const LOOKS=[
  {value:LOOK_NEW,id:'new',title:'New interface',tag:'Beta',
    text:'Replaces the whole screen: bigger waveforms, a browse sidebar and a stems row with mute buttons.',
    deck:'MODERN',video:()=>WIZARD_MEDIA.newVideo,poster:()=>WIZARD_MEDIA.newPoster,
    alt:'The new interface running on the XZ: two scrolling stem waveforms, a DRUMS, VOCAL and INST mute row, then the browse screen with its sidebar and track list.'},
  {value:LOOK_STANDARD,id:'standard',title:'Standard XDJ-XZ interface',
    text:'Keeps the deck\'s own screen layout, as it is now. Classic themes can still restyle it.',
    deck:'CLASSIC',video:()=>WIZARD_MEDIA.standardVideo,poster:()=>WIZARD_MEDIA.standardPoster,
    alt:'The XDJ-XZ\'s own player screen with a track on each deck, captured from the deck.'}
];
function lookMedia(look,selected){
  const playing=selected&&look.video()&&!reducedMotion();
  if(!playing)return el('img',{class:'wz-look-media',src:look.poster(),alt:look.alt,width:800,height:480});
  const video=el('video',{class:'wz-look-media',src:look.video(),poster:look.poster(),muted:true,loop:true,autoplay:true,playsinline:true,
    preload:'auto',width:800,height:480,'aria-label':look.alt});
  video.muted=true;video.play?.().catch(()=>{});
  return video;
}
// After install, the way back, drawn by the deck's own menus: the new interface's MODS button opens its MODS
// screen, whose APPEARANCE tab opens the panel page; the standard screen's MODS button opens the panel directly.
function lookLater(choice){
  const fromNew=choice===LOOK_NEW,images=fromNew?WIZARD_MEDIA.switchFromNew:WIZARD_MEDIA.switchFromStandard,other=fromNew?'CLASSIC':'MODERN';
  const first=fromNew?{w:560,h:336,alt:'The MODS screen of the new interface on its APPEARANCE tab, with the tab and the OPEN APPEARANCE button marked.',
      text:'Tap MODS in the top right corner, then the APPEARANCE tab and OPEN APPEARANCE.'}:
    {w:450,h:135,alt:'The top right of the standard screen with the MODS button marked.',text:'Tap MODS in the top right corner, then APPEARANCE.'};
  return el('div',{class:'wz-later',dataset:{choice:fromNew?'new':'standard'}},
    el('p',{class:'wz-later-line',text:'You can switch any time on the deck: tap MODS, then APPEARANCE → INTERFACE.'}),
    el('ol',{class:'wz-later-steps'},
      el('li',{},el('img',{src:images[0],width:first.w,height:first.h,alt:first.alt}),el('span',{},el('b',{text:'1'}),first.text)),
      el('li',{},el('img',{src:images[1],width:560,height:336,alt:'The MODS menu on its APPEARANCE page, with INTERFACE marked: MODERN or CLASSIC.'}),
        el('span',{},el('b',{text:'2'}),'Choose INTERFACE: '+other+'.'))));
}
function lookCard(look,choice){
  const selected=look.value===choice;
  return el('label',{class:'wz-look-card',dataset:{look:look.id,selected:selected?'1':null}},
    el('span',{class:'wz-look-frame'},lookMedia(look,selected)),
    el('span',{class:'wz-look-text'},
      el('input',{type:'radio',name:'wz-look',value:String(look.value),checked:selected?true:null,onchange:event=>chooseLook(look.value,event.currentTarget.closest('.wz-look-step'))}),
      el('span',{class:'wz-look-title'},el('strong',{text:look.title}),look.tag?el('span',{class:'wz-chip wz-look-tag',text:look.tag}):null),
      el('span',{class:'wz-look-desc',text:look.text})));
}
// A choice updates the step in place, so the larger frame moves across smoothly instead of redrawing.
function chooseLook(value,root){
  W.settings={...W.settings,interface:value};
  if(!root)return render();
  root.dataset.choice=value===LOOK_NEW?'new':'standard';
  for(const card of root.querySelectorAll('.wz-look-card')){
    const look=LOOKS.find(item=>item.id===card.dataset.look),selected=look.value===value;
    if(selected)card.dataset.selected='1';else delete card.dataset.selected;
    card.querySelector('.wz-look-frame').replaceChildren(lookMedia(look,selected));
  }
  root.querySelector('.wz-later').replaceWith(lookLater(value));
}
function stepLook(){
  const choice=W.settings.interface===LOOK_STANDARD?LOOK_STANDARD:LOOK_NEW;
  return [el('h3',{text:'Choose the screen the XZ shows'}),
    el('p',{class:'wz-lead',text:'XZ Mods can draw the whole player screen itself, or keep the deck\'s own screen.'}),
    el('div',{class:'wz-look-step',dataset:{choice:choice===LOOK_NEW?'new':'standard'}},
      el('div',{class:'wz-look',role:'radiogroup','aria-label':'Screen the XZ shows'},...LOOKS.map(look=>lookCard(look,choice))),
      lookLater(choice)),
    navButtons(()=>goto('quad'),'Next')];
}
// The review lists what the write will do, from the same plan data the write uses.
function planLines(){
  const ins=W.inspection,choice=loaderChoice(),saved=savedModVersion(),lines=[];
  if(choice==='add')lines.push({icon:'download',text:'Add the XZ Mods loader ('+buildName(saved)+')'});
  if(choice==='update'){
    const recorded=ins.recorded_mod_version?.id?ins.recorded_mod_version:null;
    lines.push({icon:'update',text:'Back up and update the loader',detail:(recorded?buildName(recorded):'Unknown version')+' → '+buildName(saved)});
  }
  const games=newGames().map(id=>GAMES.find(entry=>entry.id===id)?.name||id);
  if(games.length)lines.push({icon:'games',text:'Add '+plural(games.length,'free game')+': '+listText(games)});
  const profile=profilePlan();
  if(profile?.plan)lines.push({icon:'profile',text:'Save DJ profile '+profile.name});
  else if(profile?.problem)lines.push({icon:'alert',tone:'warn',text:'DJ profile '+profile.name+' will not be saved',detail:profile.problem});
  const changes=settingsChanges(),creates=ins.settings?.revision?.exists===false&&(writesLoader()||settingsEditable());
  if(changes.includes('interface')||(creates&&W.flow?.steps.includes('look')))
    lines.push({icon:'screen',text:W.settings.interface===LOOK_NEW?'Use the new interface on the XZ':'Use the standard XDJ-XZ interface on the XZ',
      detail:'Switch any time in MODS → APPEARANCE → INTERFACE'});
  if(changes.includes('games_menu'))lines.push({icon:'gear',text:'Change which games show in the GAMES menu'});
  if(creates&&!changes.length)lines.push({icon:'gear',text:'Create the settings file'});
  return lines;
}
function planList(){
  return el('ul',{class:'wz-plan'},...planLines().map(line=>el('li',{dataset:{tone:line.tone||null}},icon(line.icon),
    el('span',{},el('span',{class:'wz-plan-text',text:line.text}),line.detail?el('small',{text:line.detail}):null))));
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
    noGames:()=>{W.games=installedGames(W.inspection);render();}});
}
function safeLine(){return el('p',{class:'wz-safe'},icon('check'),'Music, playlists and prepared stems are never touched.');}
function stepReview(){
  const nothing=nothingToWrite();
  return [el('h3',{text:'Ready to write to '+driveLabel()}),
    planList(),
    el('div',{class:'wz-games-change'},
      el('button',{type:'button',class:'text-button',id:'wz-change-games','aria-expanded':String(W.gamesOpen),text:W.gamesOpen?'Hide games':'Change games',onclick:()=>{W.gamesOpen=!W.gamesOpen;render();}}),
      W.gamesOpen?gamesChooser():null),
    nothing?el('p',{class:'notice',role:'status',text:'Nothing to write yet. Add a game or a DJ profile, or cancel.'}):null,
    W.inspection.requires_copy_to_usb_root?el('p',{class:'notice',text:'This is a folder, not a USB. Copy the finished autoexec.bin to the USB afterward.'}):null,
    safeLine(),
    ejectChoice(),
    el('div',{class:'actions wz-nav-write'},
      el('button',{type:'button',class:'primary',id:'wz-write',disabled:nothing?true:null,text:'Write to USB',onclick:startWrite}),
      el('button',{type:'button',class:'secondary',text:'Back',onclick:previous})),
    jobSlot];
}
function stepWriting(){return [el('h3',{text:'Writing to '+driveLabel()}),planList(),safeLine(),ejectChoice(),jobSlot];}
async function startWrite(){
  if(W.step!=='review'||nothingToWrite())return;
  if(appUpdating){showError('XZ Mods is updating itself. Write to the USB once the update has finished.',jobSlot);return;}
  W.step='writing';W.eject=null;render();
  try{
    await ready();
    await write(jobSlot);
  }catch(error){showError(String(error),jobSlot);}
  finally{
    // A write that stopped before the end (a refused confirmation, a failed request, an error) returns to the review.
    if(W.view==='flow'&&W.step==='writing'){W.step='review';render();}
  }
}
async function write(origin){
  if(nothingToWrite()){showError('Nothing is selected to write. Pick a loader action, a DJ profile or a game first.',origin);return;}
  const choice=loaderChoice(),games=newGames(),volume=W.usb,ins=W.inspection,result={loader:null,games:null,settings:null,profile:null,errors:[]};
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
  // A USB without a settings file gets one when nothing else writes it: a games or profile write that keeps the
  // loader, or any write that asked for the screen. Without the file the XZ would keep its remembered screen.
  if(settingsChanges().length||(W.settings&&ins.settings.revision.exists===false&&(choice==='keep'||W.flow?.steps.includes('look')))){
    const fresh=await run({method:'read_usb_settings',volume},'Reading the settings file again',origin,()=>{});
    if(!fresh?.ok){showError('The settings file could not be read back, so the settings were not written. '+(fresh?.error||''),origin);W.result=result;showDone();return;}
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
  showDone();
}
// A finished write ejects by itself when asked to, unless the done screen offers a retry that needs the USB.
function showDone(){
  W.step='done';render();host.scrollIntoView({block:'start'});
  if(ejectAfterWrite()&&canEject()&&!failedGames().length)ejectUsb();
}
function failedGames(){return (W.result?.games?.games||[]).filter(game=>!game.ok);}
async function retryGames(failed){
  await ready();
  const response=await run({method:'prepare_games_usb',volume:W.usb,games:failed.map(game=>game.game)},'Retrying game downloads',jobSlot);
  if(!response?.ok)return;
  W.result={...W.result,games:response.result};
  const again=await run({method:'inspect_usb',volume:W.usb},'Checking the USB',jobSlot,()=>{});
  if(again?.ok){W.inspection=again.result;usbInspection=again.result;renderUsb(again.result);}
  render();
}
function stepDone(){
  const r=W.result,games=r.games?.games||[],copied=games.filter(game=>game.ok).map(game=>game.name),failed=games.filter(game=>!game.ok);
  const lines=[];
  if(r.loader)lines.push(r.loader.format==='xz-mods-loader-updated/1'?'Loader updated. The previous loader is backed up on the USB.':'Loader written and verified.');
  if(copied.length)lines.push('Games on USB: '+copied.join(', ')+'.');
  for(const game of failed)lines.push(game.name+' did not download: '+game.error);
  if(r.settings)lines.push('Settings saved to the USB and read back.');
  if(r.profile?.ok)lines.push('DJ profile '+r.profile.name+' saved to the USB.',...(r.profile.notes||[]));
  lines.push(...r.errors);
  const trouble=Boolean(failed.length||r.errors.length),ejected=W.eject?.state==='done',ejecting=W.eject?.state==='ejecting';
  if(failed.length&&ejectAfterWrite()&&canEject()&&!W.eject)lines.push('The USB was not ejected, so you can retry the failed games first.');
  const offerEject=canEject()&&!ejected&&!failed.length;
  return [el('div',{class:'wz-done-hero',dataset:{tone:trouble?'warn':'ok'}},el('span',{class:'wz-done-mark'},icon(trouble?'alert':'check')),
      el('h3',{text:trouble?'Finished, with something to check':'Your USB is ready'})),
    el('ul',{class:'wz-done'},...lines.map(text=>el('li',{text}))),
    ejectStatus(),
    r.loader?quadEntry():null,
    r.loader&&!trouble?el('button',{type:'button',class:'text-button',id:'wz-support-open',text:'Support XZ Mods (optional)',onclick:()=>goto('support')}):null,
    r.loader?el('div',{class:'wz-next'},el('h4',{text:'On the XZ'}),el('ol',{},
      el('li',{text:ejected?'Unplug the USB and plug it into USB 1 or USB 2 on the XDJ-XZ, then power it on.':'Eject the USB here and plug it into USB 1 or USB 2 on the XDJ-XZ, then power it on.'}),
      el('li',{text:'On the play screen, press MENU or touch MODS at the top right to open the menu. Hold MENU for the player’s own settings. HOME is the first page.'}),el('li',{text:'The button next to LINK switches between the Classic and Modern screens.'}),el('li',{text:'Test the loader before you use it in a set.'}))):null,
    el('div',{class:'actions'},
      failed.length?el('button',{type:'button',class:'primary',text:'Retry the failed games',onclick:()=>retryGames(failed)}):null,
      offerEject?el('button',{type:'button',class:'primary wz-eject',id:'wz-eject',disabled:ejecting?true:null,'aria-busy':ejecting?'true':null,onclick:ejectUsb},
        ejecting?el('span',{class:'wz-spinner wz-spinner-dark','aria-hidden':'true'}):icon('eject'),
        el('span',{text:ejecting?'Ejecting…':W.eject?.state==='error'?'Try ejecting again':'Eject USB'})):null,
      el('button',{type:'button',class:failed.length||offerEject?'secondary':'primary',id:'wz-done',disabled:ejecting?true:null,text:'Done',onclick:ejected?afterEject:backToDevice}),
      r.profile?.ok?el('button',{type:'button',class:'secondary',text:'Make the profile track',onclick:()=>showPage('dj-profile')}):null),
    failed.length||ejected?null:ejectChoice(true),
    jobSlot];
}
function ejectStatus(){
  if(W.eject?.state==='done')return el('p',{class:'wz-ejected',role:'status'},icon('check'),el('span',{text:'USB ejected. You can unplug it now.'}));
  if(W.eject?.state==='error')return el('p',{class:'notice wz-eject-error',role:'alert',text:'The USB was not ejected. '+W.eject.message});
  return null;
}
function supportLink(label,url){
  return el('a',{class:'button secondary',href:url,target:'_blank',rel:'noopener noreferrer',text:label,onclick:async event=>{
    if(!invoke)return;
    event.preventDefault();
    try{await invoke('open_link',{url});}catch(error){showError(String(error),jobSlot);}
  }});
}
function stepSupport(){
  return [el('div',{class:'wz-support'},
    el('img',{class:'wz-support-logo',src:'assets/xz-mods.png',alt:'XZ Mods',width:112,height:112}),
    el('span',{class:'wz-eyebrow',text:'Optional support'}),
    el('h3',{text:'Help keep XZ Mods growing'}),
    el('p',{class:'wz-lead',text:'If XZ Mods has been useful to you, voluntary support helps fund upkeep and future projects.'}),
    el('p',{text:'Every feature stays available whether you contribute or not. Thanks for using it and sharing what works for you.'}),
    el('div',{class:'wz-support-link'},supportLink('Support on Cash App','https://cash.app/$FireAtWill42'),el('code',{text:'$FireAtWill42'})),
    el('p',{class:'muted',text:'Opens Cash App in your browser. You choose whether to pay and how much.'}),
    el('p',{class:'wz-support-credit',text:'Created by VJ OpticMystic. Updates and projects: @opticmysticvj. Available for live VJ performances and custom visuals.'}),
    el('div',{class:'actions'},
      el('button',{type:'button',class:'primary',id:'wz-support-finish',text:'Finish',onclick:W.eject?.state==='done'?afterEject:backToDevice}),
      el('button',{type:'button',class:'secondary',text:'Back to results',onclick:()=>goto('done')})))];
}
const STEPS={support:stepSupport,quad:stepQuad,profile:stepProfile,look:stepLook,review:stepReview,writing:stepWriting,done:stepDone};
function viewFlow(){return el('section',{class:'wz-flow',dataset:{step:W.step}},flowHead(),el('div',{class:'wz-flow-body'},...STEPS[W.step]()));}

const VIEWS={find:viewFind,device:viewDevice,flow:viewFlow};
function render(){
  // The shared job panel may sit inside a view being replaced; re-place it so its ids stay in the document.
  const hadPanel=host.contains(jobPanel);
  host.replaceChildren(VIEWS[W.view]());
  if(hadPanel||feedbackLive)positionFeedback();
  // The done step lists the outcome itself; a finished job panel would say it twice.
  if(W.view==='flow'&&W.step==='done'&&!feedbackLive&&!jobPanel.classList.contains('is-error'))jobPanel.classList.add('hidden');
  for(const listener of listeners)listener();
  schedulePoll();
}
window.setupWizard={state:W,render,shown,refreshNav,goto,startFlow,chooseUsb,findAndChoose,usbCard,driveLabel,loaderChoice,newGames,settingsChanges,onRender:listener=>listeners.push(listener)};
// Other pages can hand over a USB they already read.
window.openSetupFor=inspection=>{open(inspection.volume,inspection);showPage('setup');followNewest();};
shown();
})();
