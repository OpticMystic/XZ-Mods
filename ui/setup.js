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
  games:{title:'Add games',loader:'keep'}
};
const POLL_MS=2000;
const W={view:'find',flow:null,step:null,usbs:null,finding:false,usb:null,inspection:null,loader:'auto',games:new Set(),settings:null,settingsStart:null,result:null,
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

// discovering is the in-flight discover_usbs, so a user action can wait for it instead of being dropped by run().
let discovering=null,firstLook=null,pollTimer=null,polling=false,volumesSig=null,dismissed=null;
const usbSig=list=>list.map(item=>item.volume).sort().join('|');
async function ready(){while(discovering)await discovering;await idle();}
function discover(user){
  discovering??=(async()=>{
    W.finding=true;render();
    const response=await run({method:'discover_usbs'},'Looking for USB drives',jobSlot,()=>{});
    W.finding=false;
    if(!response){render();return null;}
    if(!response.ok){W.usbs??=[];render();if(user&&!response.cancelled)showError('USB drives could not be listed. '+(response.error||'Choose a folder instead.'),jobSlot);return null;}
    W.usbs=response.result.usbs;render();
    return W.usbs;
  })().finally(()=>{discovering=null;});
  return discovering;
}
// One USB opens by itself, unless it is the set the DJ just stepped away from with Change USB.
async function lookForUsbs(user){
  if(user)await idle();
  const list=await discover(user);
  if(W.view==='find'&&list?.length===1&&usbSig(list)!==dismissed)await chooseUsb(list[0].volume);
}
function pollWanted(){return Boolean(invoke)&&W.view==='find'&&!$('setup').classList.contains('hidden');}
function schedulePoll(){
  if(!pollWanted()){clearTimeout(pollTimer);pollTimer=null;return;}
  if(!pollTimer&&!polling)pollTimer=setTimeout(poll,POLL_MS);
}
// list_volumes is a cheap host call with no job, so it never holds the job slot; a changed set triggers a real discover.
async function poll(){
  pollTimer=null;
  if(!pollWanted())return;
  polling=true;
  try{
    const sig=(await invoke('list_volumes')).join('|');
    if(sig!==volumesSig&&activeJob===null&&!discovering){volumesSig=sig;await lookForUsbs(false);}
  }catch{return;}
  finally{polling=false;}
  schedulePoll();
}
function shown(){
  if(invoke&&!firstLook)firstLook=(async()=>{
    try{volumesSig=(await invoke('list_volumes')).join('|');}catch{}
    await idle();
    await lookForUsbs(false);
  })();
  render();
}
async function chooseUsb(volume){
  await ready();
  const response=await run({method:'inspect_usb',volume},'Reading the USB',jobSlot,()=>{});
  if(!response?.ok){if(response&&!response.cancelled)showError('The USB could not be read. '+(response.error||'Reconnect it and choose it again.'),jobSlot);return;}
  open(volume,response.result);
  await followNewest();
}
function open(volume,inspection){
  dismissed=null;
  startState(inspection);
  renderUsb(inspection);
  go({view:'device',flow:null,step:null,usb:volume,inspection,loader:'auto',result:null,changesOpen:false,changesAll:false,gamesOpen:false,filesOpen:false});
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
async function findAndChoose(){
  dismissed=null;
  await idle();
  const list=await discover(true);
  if(list?.length===1)await chooseUsb(list[0].volume);
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
  return el('button',{type:'button',class:'wz-usb',onclick:()=>chooseUsb(item.volume)},
    el('span',{class:'wz-usb-icon'},icon('usb')),
    el('span',{class:'wz-usb-text'},el('strong',{text:item.label}),el('small',{text:item.volume+(item.removable?'':' · not a removable drive')})),
    el('span',{class:'wz-chips'},...chips.map((text,index)=>el('span',{class:'wz-chip',dataset:{tone:index===0&&item.loader?'on':null},text}))),
    icon('chevron','wz-go'));
}
function quietActions(){
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
    quietActions(),jobSlot);
  return el('section',{class:'wz-find wz-empty',dataset:{looking:W.finding?'1':null}},
    $('tpl-usb-art').content.firstElementChild.cloneNode(true),
    el('h2',{text:'Plug in your XZ USB'}),
    el('p',{class:'wz-lead',text:'XZ Mods finds it as soon as it is connected. It never formats a drive or touches your music.'}),
    quietActions(),jobSlot);
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
    el('summary',{},el('span',{text:'What changes on your XZ'}),el('small',{text:plural(count,'change')})),panel);
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
    el('div',{class:'wz-status-text'},el('h3',{text:s.title}),el('p',{text:s.line}),s.extra||null,
      s.action?el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'wz-primary',disabled:buildSaving?true:null,text:s.action.label,onclick:s.action.run})):null));
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
    buildLine(),
    el('details',{class:'wz-files',open:W.filesOpen?true:null,ontoggle:event=>{W.filesOpen=event.target.open;}},
      el('summary',{text:'Show every file on this USB'}),tree()));
}

// --- flow ---

function startFlow(kind,{profile=false,gamesOpen=false}={}){
  const ins=W.inspection,withProfile=profile||kind==='profile'||(kind==='install'&&!ins.dj_profiles?.count);
  const steps=[...(withProfile?['profile']:[]),'review','writing','done'];
  startState(ins);
  if(kind==='profile')W.games=installedGames(ins);
  go({view:'flow',flow:{kind,steps},step:gamesOpen?'review':steps[0],loader:FLOWS[kind].loader,result:null,gamesOpen});
}
function backToDevice(){
  if(W.flow?.steps.includes('profile')&&W.step!=='done')window.djProfilePage?.discard();
  startState(W.inspection);
  go({view:'device',flow:null,step:null,loader:'auto',result:null,gamesOpen:false});
}
function goto(step){
  if(W.view!=='flow'||!W.flow.steps.includes(step))return;
  W.step=step;render();host.scrollIntoView({block:'start'});
}
function previous(){const steps=W.flow.steps,index=steps.indexOf(W.step);if(index>0)goto(steps[index-1]);else backToDevice();}
function flowHead(){
  const steps=W.flow.steps,index=steps.indexOf(W.step),cancellable=W.step==='profile'||W.step==='review';
  return el('div',{class:'wz-flow-head'},
    el('div',{class:'wz-flow-title'},el('h2',{text:FLOWS[W.flow.kind].title}),el('span',{class:'wz-mono',text:driveLabel()===W.usb?W.usb:driveLabel()+' · '+W.usb})),
    el('div',{class:'wz-flow-progress'},
      el('span',{class:'wz-flow-count',text:'Step '+(index+1)+' of '+steps.length}),
      el('ol',{class:'wz-dots','aria-hidden':'true'},...steps.map((step,i)=>el('li',{dataset:{state:i<index?'done':i===index?'current':'todo'}})))),
    cancellable?el('button',{type:'button',class:'text-button wz-cancel',text:'Cancel',onclick:backToDevice}):null);
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
  return navButtons(()=>goto('review'),profile?'Next':'Skip for now',Boolean(profile));
}
function refreshNav(){const old=host.querySelector('.wz-nav'),next=stepNav();if(old&&next)old.replaceWith(next);}
// dj-profile.js draws into the slot (and asks for refreshNav as the draft changes).
function stepProfile(){
  return [el('h3',{text:'Your DJ profile'}),
    el('p',{class:'wz-lead',text:'Your name and logo on the XZ, and for VJ software on the same network. '+(W.inspection.dj_profiles?.count?'Edit it here, or go on.':'It takes a minute. Skip it if you like and add it later.')}),
    el('div',{id:'wz-djp-slot'}),
    stepNav()];
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
  if(settingsChanges().length)lines.push({icon:'gear',text:'Change which games show in the GAMES menu'});
  else if(ins.settings?.revision?.exists===false&&(writesLoader()||settingsEditable()))lines.push({icon:'gear',text:'Create the settings file'});
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
    el('div',{class:'actions wz-nav-write'},
      el('button',{type:'button',class:'primary',id:'wz-write',disabled:nothing?true:null,text:'Write to USB',onclick:startWrite}),
      el('button',{type:'button',class:'secondary',text:'Back',onclick:previous})),
    jobSlot];
}
function stepWriting(){return [el('h3',{text:'Writing to '+driveLabel()}),planList(),safeLine(),jobSlot];}
async function startWrite(){
  if(W.step!=='review'||nothingToWrite())return;
  W.step='writing';render();
  await ready();
  await write(jobSlot);
  // A write that stopped before the end (a refused confirmation or a failed request) returns to the review.
  if(W.view==='flow'&&W.step==='writing'){W.step='review';render();}
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
  if(settingsChanges().length||(W.settings&&ins.settings.revision.exists===false&&choice==='keep')){
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
function showDone(){W.step='done';render();host.scrollIntoView({block:'start'});}
async function retryGames(failed){
  await ready();
  const response=await run({method:'prepare_games_usb',volume:W.usb,games:failed.map(game=>game.game)},'Retrying game downloads',jobSlot);
  if(response?.ok){W.result={...W.result,games:response.result};render();}
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
  const trouble=Boolean(failed.length||r.errors.length);
  return [el('div',{class:'wz-done-hero',dataset:{tone:trouble?'warn':'ok'}},el('span',{class:'wz-done-mark'},icon(trouble?'alert':'check')),
      el('h3',{text:trouble?'Finished, with something to check':'Your USB is ready'})),
    el('ul',{class:'wz-done'},...lines.map(text=>el('li',{text}))),
    r.loader?el('div',{class:'wz-next'},el('h4',{text:'On the XZ'}),el('ol',{},el('li',{text:'Eject the USB here and plug it into USB 1 or USB 2 on the XDJ-XZ, then power it on.'}),
      el('li',{text:'Press the REKORDBOX key or touch MODS to open the menu. GENERAL is the first tab.'}),el('li',{text:'Test the loader before you use it in a set.'}))):null,
    el('div',{class:'actions'},
      failed.length?el('button',{type:'button',class:'primary',text:'Retry the failed games',onclick:()=>retryGames(failed)}):null,
      el('button',{type:'button',class:failed.length?'secondary':'primary',id:'wz-done',text:'Done',onclick:backToDevice}),
      r.profile?.ok?el('button',{type:'button',class:'secondary',text:'Make the profile track',onclick:()=>showPage('dj-profile')}):null),
    jobSlot];
}
const STEPS={profile:stepProfile,review:stepReview,writing:stepWriting,done:stepDone};
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
