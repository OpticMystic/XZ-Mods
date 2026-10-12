// DJ Profile page: the profiles on the selected USB, an editor with a live device preview, and the profile track
// wizard. One state object P moves through the phases below; each phase has one view. Errors are data on P.
//   no-usb  -> loading -> list <-> editing -> saving -> list
//                         list  -> track (where -> create -> rekordbox) -> list
// The USB is the wizard's (setupWizard.state): choosing it on any page is enough.
(function(){
const LIMITS={name:160,tagline:80,aliases:8,alias:80,links:12,url:2048,logos:8,logoBytes:8*1048576,totalBytes:24*1048576,noteBytes:8192};
const LINK_KINDS=[['media-kit','EPK'],['files','Files'],['website','Website'],['instagram','Instagram'],['soundcloud','SoundCloud'],['mixcloud','Mixcloud'],['bandcamp','Bandcamp'],['link','Other']];
const DEFAULT_ACCENT='#2997ff',BUDGET_SHOWN_FROM=16*1048576;
const CONFLICT=/changed since they were loaded|changed while its track was written/;
const CONFLICT_TEXT="This USB's profiles changed since you opened them. Reload and try again.";
const TRACK_STEPS=[['where','Where'],['create','Create'],['rekordbox','Add it to rekordbox']];
const TRACK_LEAD='Some newer players block reading extra files from the USB. A profile track is a silent song that carries your name, logos and links, so it reaches them through rekordbox like any other track.';
const W=setupWizard.state,host=$('djp-host');
const P={phase:'no-usb',volume:null,data:null,error:null,saved:null,draft:null,track:null};

const mb=bytes=>(bytes/1048576).toFixed(bytes>=10485760?0:1)+' MB';
const kb=bytes=>bytes<1048576?Math.max(1,Math.round(bytes/1024))+' KB':mb(bytes);
const kindLabel=kind=>LINK_KINDS.find(([id])=>id===kind)?.[1]||kind;
const baseName=path=>String(path||'').split(/[\\/]/).pop();
const urlHost=url=>{try{return new URL(url).host;}catch{return url;}};
const savedProfiles=()=>(P.data?.profiles||[]).filter(profile=>profile.source!=='folder');
const driveName=()=>setupWizard.driveLabel();

function set(patch){Object.assign(P,patch);render();}
function failure(response,fallback){
  if(!response)return {kind:'failed',message:'Another task is still running. Try again when it finishes.'};
  const message=response.error||fallback;
  return CONFLICT.test(message)?{kind:'conflict',message:CONFLICT_TEXT}:{kind:'failed',message};
}
function call(request){return run(request,'DJ profile',null,()=>{});}

// --- reading and the wizard's USB ---

function summarise(data){
  if(!data.revision)return {count:0,active_name:null};
  const listed=data.profiles.filter(profile=>profile.source==='vjtools'),broken=data.problems.filter(problem=>problem.dj_id&&!problem.dj_id.startsWith('folder:'));
  const active=listed.find(profile=>profile.active)||broken.find(problem=>problem.dj_id===data.active);
  return {count:listed.length+broken.length,active_name:active?.dj_name||null};
}
function publish(data){
  if(!W.inspection||W.usb!==P.volume)return;
  const next=summarise(data);
  if(JSON.stringify(next)===JSON.stringify(W.inspection.dj_profiles))return;
  W.inspection.dj_profiles=next;setupWizard.render();
}
let reading=null;
async function load(volume,after='list'){
  reading=volume;set({phase:'loading',volume,error:null});
  const response=await call({method:'read_dj_profiles',volume});
  if(reading===volume)reading=null;
  if(P.volume!==volume)return;
  if(!response?.ok){set({phase:after==='editing'&&P.draft?'editing':'list',data:after==='editing'?P.data:null,error:failure(response,'The USB could not be read.')});return;}
  P.data=response.result;publish(P.data);
  set({phase:after==='editing'&&P.draft?'editing':'list'});
}
// Reads only while the page shows, so a background read never holds the one job slot another page is about to use.
const visible=()=>!$('dj-profile').classList.contains('hidden');
function sync(){
  const usb=W.inspection?W.usb:null;
  if(usb!==P.volume){
    Object.assign(P,{phase:usb?'loading':'no-usb',volume:usb,data:null,error:null,saved:null,draft:null,track:null});
    if(visible())usb?load(usb):render();
    return;
  }
  if(P.phase==='no-usb'&&visible())render();
}
function show(){if(P.phase==='loading'&&!reading&&P.volume)load(P.volume);else render();}

// --- the draft and its checks ---

function draftFrom(profile){
  const folder=profile?.source==='folder',first=!savedProfiles().length;
  return {dj_id:profile&&!folder?profile.dj_id:null,source:profile?.source||'new',original:profile?.dj_name||'',
    dj_name:profile?.dj_name||'',tagline:profile?.tagline||'',accent:profile?.accent||P.data?.stick_colour||DEFAULT_ACCENT,
    aliases:(profile?.aliases||[]).join('\n'),note:profile?.note||'',page_url:profile?.page_url||null,
    links:profile?.links?.length?profile.links.map(link=>({...link})):[{kind:'media-kit',url:''}],
    logos:(profile?.logos||[]).map(logo=>({path:logo.path,label:logo.label||'',note:logo.note||'',width:logo.width,height:logo.height,bytes:logo.bytes,preview:logo.preview})),
    first,makeActive:first||Boolean(profile?.active&&!folder),wasActive:Boolean(profile?.active&&!folder),moreOpen:false,logoError:''};
}
const bytesOf=text=>new TextEncoder().encode(text).length;
const control=text=>/[\u0000-\u001f\u007f-\u009f]/.test(text);
const noteControl=text=>/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f-\u009f]/.test(text);
function urlProblem(url){
  const text=url.trim();
  if(!text)return '';
  if(/^http:\/\//i.test(text))return 'Use an https:// address. This one starts with http://.';
  if(!/^https:\/\/[^/?#]+/.test(text))return 'Start the address with https://';
  if(/\s/.test(text)||control(text))return 'Addresses cannot contain spaces.';
  if(text.length>LIMITS.url)return 'This address is longer than '+LIMITS.url+' characters.';
  return '';
}
function aliasList(d){return d.aliases.split('\n').map(alias=>alias.trim()).filter(Boolean);}
function logoBytes(d){return d.logos.reduce((sum,logo)=>sum+(logo.bytes||0),0);}
// Field -> message for every problem; save is the first one, shown beside the disabled Save button.
function validate(d){
  const name=d.dj_name.trim(),aliases=aliasList(d),problems={};
  if(name.length>LIMITS.name)problems.name='Use at most '+LIMITS.name+' characters.';
  else if(control(name))problems.name='Use letters, numbers and symbols only.';
  if(d.tagline.trim().length>LIMITS.tagline)problems.tagline='Use at most '+LIMITS.tagline+' characters.';
  if(!/^#[0-9a-fA-F]{6}$/.test(d.accent))problems.accent='Use a colour like #2997ff.';
  d.links.forEach((link,index)=>{const problem=urlProblem(link.url);if(problem)problems['link-'+index]=problem;});
  if(d.links.filter(link=>link.url.trim()).length>LIMITS.links)problems.links='A profile holds at most '+LIMITS.links+' links.';
  if(aliases.length>LIMITS.aliases)problems.aliases='Use at most '+LIMITS.aliases+' other names.';
  else if(aliases.some(alias=>alias.length>LIMITS.alias||control(alias)))problems.aliases='Each name can be at most '+LIMITS.alias+' characters.';
  if(bytesOf(d.note)>LIMITS.noteBytes||noteControl(d.note))problems.note='Keep the note under 8 KB of plain text.';
  if(logoBytes(d)>LIMITS.totalBytes)problems.logos='These logos add up to more than 24 MB. Remove one or use smaller files.';
  const save=!name?'Add your DJ name to save.':Object.values(problems)[0]||'';
  return {problems,save};
}
function saveRequest(d){
  return {method:'save_dj_profile',volume:P.volume,expected_revision:P.data.revision,
    profile:{dj_id:d.dj_id,dj_name:d.dj_name.trim(),tagline:d.tagline.trim(),accent:d.accent.toLowerCase(),aliases:aliasList(d),
      links:d.links.filter(link=>link.url.trim()).map(link=>({kind:link.kind,url:link.url.trim()})),page_url:d.page_url,note:d.note},
    logos:d.logos.map(logo=>({path:logo.path,label:logo.label.trim(),note:logo.note})),make_active:d.first||d.makeActive};
}

// --- actions ---

function edit(profile){set({phase:'editing',draft:draftFrom(profile),error:null,saved:null});}
async function save(){
  const d=P.draft;if(validate(d).save)return;
  set({phase:'saving',error:null});
  const response=await call(saveRequest(d));
  if(!response?.ok){set({phase:'editing',error:failure(response,'The profile was not saved.')});return;}
  P.data=response.result;publish(P.data);
  set({phase:'list',draft:null,saved:response.result.saved});
}
async function makeActive(profile){
  set({error:null});
  const response=await call({method:'set_active_dj_profile',volume:P.volume,dj_id:profile.dj_id,expected_revision:P.data.revision});
  if(!response?.ok){set({error:failure(response,profile.dj_name+' was not made active.')});return;}
  P.data=response.result;publish(P.data);
  set({saved:null});
}
async function addLogo(){
  const d=P.draft;
  if(d.logos.length>=LIMITS.logos)return;
  let path;
  try{path=await invoke('pick_path',{kind:'image'});}catch(error){d.logoError=String(error);render();return;}
  if(!path)return;
  if(d.logos.some(logo=>logo.path===path)){d.logoError='That logo is already in the list.';render();return;}
  const response=await call({method:'read_logo',path});
  if(!response?.ok){d.logoError=response?.error||'That file could not be read as a logo.';render();return;}
  const logo=response.result;
  d.logos.push({path:logo.path,label:'',note:'',width:logo.width,height:logo.height,bytes:logo.bytes,preview:logo.preview});
  d.logoError='';render();
}
function openTrack(profile){set({phase:'track',error:null,track:{dj_id:profile.dj_id,name:profile.dj_name,links:profile.links||[],step:'where',folder:null,usbCopy:false,running:false,result:null,usb:null,error:null}});}
async function writeTrack(){
  const t=P.track;
  Object.assign(t,{step:'create',running:true,error:null,result:null,usb:null});render();
  const request={method:'write_profile_track',volume:P.volume,dj_id:t.dj_id,destination:'folder'};
  if(t.folder)request.folder=t.folder;
  const response=await call(request);
  if(!response?.ok){Object.assign(t,{running:false,error:failure(response,'The profile track was not made.')});render();return;}
  t.result=response.result;
  if(t.usbCopy){
    const copy=await call({method:'write_profile_track',volume:P.volume,dj_id:t.dj_id,destination:'usb'});
    if(copy?.ok)t.usb=copy.result;else t.error=failure(copy,'The copy on the USB was not made.');
  }
  t.running=false;render();
}
async function pickTrackFolder(){
  try{const folder=await invoke('pick_path',{kind:'folder'});if(folder){P.track.folder=folder;render();}}
  catch(error){P.track.error={kind:'failed',message:String(error)};render();}
}
async function copyPath(button,path){
  try{await navigator.clipboard.writeText(path);button.textContent='Copied';}
  catch{const field=button.parentElement.querySelector('input');field?.select();button.textContent='Press Ctrl+C';}
}

// --- shared pieces ---

function tileImage(profile){return profile.tile?.preview||profile.logos?.[0]?.preview||null;}
function tile(preview,name,accent,{fit=false}={}){
  return el('span',{class:'djp-tile',dataset:{fit:fit?'1':null},style:'--accent:'+(accent||DEFAULT_ACCENT)},
    preview?el('img',{src:preview,alt:''}):el('span',{class:'djp-mono',text:djMonogram(name)||'DJ'}));
}
function banner(error,actions){
  if(!error)return null;
  return el('div',{class:'djp-banner',dataset:{kind:error.kind},role:'alert'},el('p',{text:error.message}),
    el('div',{class:'actions'},error.kind==='conflict'?el('button',{type:'button',class:'primary',text:'Reload',onclick:()=>load(P.volume,P.phase==='editing'?'editing':'list')}):null,...(actions||[])));
}
function deviceTheme(){
  const theme=DEVICE_THEMES[W.settings?.theme]||DEVICE_THEMES[0];
  return '--dv-bg:'+theme.bg+';--dv-ink:'+theme.ink+';--dv-accent:'+theme.accent+';--dv-alarm:'+theme.alarm;
}

// --- views ---

function viewNoUsb(){
  const list=W.usbs;
  return el('section',{class:'wz-pane'},
    el('h2',{text:'Your DJ profile'}),
    el('p',{class:'wz-lead',text:'A DJ profile lives on your USB. Find the USB first to see or make one.'}),
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'djp-find',disabled:W.finding?true:null,text:W.finding?'Looking…':list?'Look again':'Find my USB',onclick:event=>setupWizard.findAndChoose(event.currentTarget)})),
    list&&!list.length?el('p',{class:'notice',role:'status',text:'No USB drive was found. Plug in the USB, wait a moment, then choose Look again.'}):null,
    list?.length>1?el('div',{class:'wz-usb-list',role:'list'},...list.map(setupWizard.usbCard)):null);
}
function viewLoading(){
  return el('section',{class:'wz-pane'},el('h2',{text:'DJ profiles on '+driveName()}),el('p',{class:'wz-lead',role:'status',text:'Reading the profiles on this USB…'}));
}
function card(profile){
  const folder=profile.source==='folder',canActivate=profile.source==='vjtools'&&!profile.active;
  return el('article',{class:'djp-card',role:'listitem',dataset:{id:profile.dj_id,source:profile.source,active:profile.active?'1':null,highlight:profile.dj_id===P.saved?'1':null}},
    tile(tileImage(profile),profile.dj_name,profile.accent,{fit:!profile.tile}),
    el('div',{class:'djp-card-text'},el('strong',{text:profile.dj_name}),profile.tagline?el('span',{text:profile.tagline}):null,
      folder?el('small',{class:'djp-hand',text:'Made by hand · Save to edit'}):null,
      profile.track?el('small',{text:'Profile track on this USB · '+kb(profile.track.bytes)}):null),
    el('div',{class:'djp-card-foot'},
      profile.active?el('span',{class:'djp-active'},el('span',{class:'tree-dot',dataset:{tone:'ok'},'aria-hidden':'true'}),'Active on the XZ'):null,
      canActivate?el('button',{type:'button',class:'text-button',text:'Make active',onclick:()=>makeActive(profile)}):null,
      el('button',{type:'button',class:'secondary djp-edit',text:'Edit',dataset:{edit:profile.dj_id},onclick:()=>edit(profile)})));
}
function trackPanel(){
  const saved=savedProfiles();
  if(!saved.length)return null;
  const lead=saved.find(profile=>profile.active)||saved[0];
  return el('div',{class:'djp-track-panel'},
    el('h3',{text:'Profile track for newer players'}),
    el('p',{class:'wz-lead',text:TRACK_LEAD}),
    el('div',{class:'djp-track-rows',role:'list'},...saved.map(profile=>el('div',{class:'djp-track-row',role:'listitem',dataset:{id:profile.dj_id}},
      el('span',{class:'djp-track-name'},el('strong',{text:profile.dj_name}),el('small',{text:profile.track?'Profile track on this USB · '+kb(profile.track.bytes):'No profile track yet'})),
      el('button',{type:'button',class:profile===lead&&!P.saved?'primary':'secondary',text:profile.track?'Make a new profile track':'Create profile track',onclick:()=>openTrack(profile)})))));
}
function viewList(){
  if(!P.data)return el('section',{class:'wz-pane'},el('h2',{text:'DJ profiles on '+driveName()}),
    banner(P.error||{kind:'failed',message:'The USB could not be read.'},[el('button',{type:'button',class:'secondary',text:'Try again',onclick:()=>load(P.volume)})]));
  const data=P.data,empty=!data.profiles.length&&!data.problems.length,saved=data.profiles.find(profile=>profile.dj_id===P.saved);
  const newButton=el('button',{type:'button',class:saved?'secondary':'primary',id:'djp-new',text:'New DJ profile',onclick:()=>edit(null)});
  return el('section',{class:'wz-pane djp-list'},
    el('h2',{text:'DJ profiles on '+driveName()}),
    banner(P.error),
    saved?el('div',{class:'djp-saved',role:'status'},el('p',{text:'Saved '+saved.dj_name+' to the USB.'+(saved.active?' It is the active profile on the XZ.':'')+' Next, make its profile track for newer players.'}),
      el('div',{class:'actions'},el('button',{type:'button',class:'primary',text:'Create profile track',onclick:()=>openTrack(saved)}))):null,
    empty?el('p',{class:'wz-lead',text:'No DJ profile on this USB yet. A profile puts your name and logo on the XZ and lets VJ software on the same network pick them up.'}):
      el('p',{class:'wz-lead',text:'The XZ shows each profile as a logo tile. The active one is the DJ the XZ and VJ software see.'}),
    empty?null:el('div',{class:'djp-cards',role:'list'},...data.profiles.map(card)),
    data.problems.length?el('ul',{class:'djp-problems'},...data.problems.map(problem=>el('li',{text:(problem.dj_name||'A profile')+' could not be read: '+problem.error}))):null,
    el('div',{class:'actions'},newButton,el('span',{class:'wz-usb-now',text:'USB '+P.volume})),
    trackPanel());
}

// The editor keeps its inputs across keystrokes: typing updates P.draft and refreshes only the checks and the preview.
let editorParts=null;
function field(label,input,{key,hint}={}){
  return el('div',{class:'djp-field'},el('label',{class:'djp-label'},el('span',{text:label}),input),hint?el('small',{class:'djp-hint',text:hint}):null,key?el('p',{class:'djp-err',dataset:{err:key},role:'status'}):null);
}
function input(props,update){return el('input',{...props,oninput:event=>{update(event.target.value);refreshEditor();}});}
function logoRow(logo,index){
  const d=P.draft,main=index===0;
  return el('div',{class:'djp-logo',role:'listitem',dataset:{main:main?'1':null}},
    tile(logo.preview,d.dj_name,d.accent,{fit:true}),
    el('div',{class:'djp-logo-text'},
      el('span',{class:'djp-logo-head'},main?el('span',{class:'djp-main',text:'Main logo'}):el('button',{type:'button',class:'text-button',text:'Make main',onclick:()=>{d.logos.unshift(...d.logos.splice(index,1));render();}}),
        el('small',{text:(logo.width&&logo.height?logo.width+' × '+logo.height+' px · ':'')+kb(logo.bytes||0)})),
      el('input',{type:'text',class:'djp-logo-label',placeholder:'Label (optional)',value:logo.label,maxlength:String(LIMITS.name),'aria-label':'Logo label',oninput:event=>{logo.label=event.target.value;}})),
    el('button',{type:'button',class:'djp-remove','aria-label':'Remove this logo',title:'Remove',onclick:()=>{d.logos.splice(index,1);d.logoError='';render();}},'×'));
}
function linkRow(link,index){
  const d=P.draft,kinds=LINK_KINDS.some(([id])=>id===link.kind)?LINK_KINDS:[...LINK_KINDS,[link.kind,link.kind]];
  return el('div',{class:'djp-link',role:'listitem'},
    el('div',{class:'djp-link-row'},
      el('select',{'aria-label':'Link kind',onchange:event=>{link.kind=event.target.value;refreshEditor();}},...kinds.map(([id,text])=>el('option',{value:id,selected:id===link.kind?true:null,text}))),
      input({type:'url',value:link.url,placeholder:link.kind==='media-kit'||link.kind==='files'?'https://drive.google.com/…':'https://…','aria-label':kindLabel(link.kind)+' address',spellcheck:'false'},value=>{link.url=value;}),
      el('button',{type:'button',class:'djp-remove','aria-label':'Remove this link',title:'Remove',onclick:()=>{d.links.splice(index,1);render();}},'×')),
    el('p',{class:'djp-err',dataset:{err:'link-'+index},role:'status'}));
}
function preview(){
  const d=P.draft,name=d.dj_name.trim(),main=d.logos[0],links=d.links.filter(link=>link.url.trim()&&!urlProblem(link.url));
  return el('div',{class:'djp-preview-body'},
    el('div',{class:'djp-device',style:deviceTheme()},
      el('div',{class:'djp-dev-head'},el('span',{class:'dv-title'},el('small',{text:'XZ MODS'}),el('strong',{text:'GENERAL'})),
        profileChip({name:name||null,usb:'USB · '+driveName(),preview:main?.preview||null,accent:d.accent})),
      el('div',{class:'djp-dev-body'},tile(main?.preview,name,d.accent,{fit:true}),
        el('div',{class:'djp-dev-text'},el('small',{text:'DJ PROFILE'}),el('strong',{text:name||'YOUR DJ NAME'}),d.tagline.trim()?el('span',{text:d.tagline.trim()}):null))),
    el('p',{class:'djp-caption',text:'The header chip and the 160 × 160 logo tile, as the XZ draws them.'}),
    el('h3',{text:'What VJ software sees'}),
    el('p',{class:'djp-vj'},el('span',{},el('strong',{text:name||'Your DJ name'}),d.tagline.trim()?' · '+d.tagline.trim():''),
      links.length?el('span',{class:'djp-vj-links'},...links.map(link=>el('span',{text:kindLabel(link.kind)+' '+urlHost(link.url.trim())}))):el('span',{class:'djp-vj-links',text:'No links yet'})));
}
function viewEditor(){
  const d=P.draft,saving=P.phase==='saving',others=savedProfiles().filter(profile=>profile.dj_id!==d.dj_id).length;
  const title=d.source==='folder'?'Save '+d.original+' as a profile':d.dj_id?'Edit '+d.original:'New DJ profile';
  const lead=d.source==='folder'?'Made by hand on the USB. Saving keeps your folder and adds a profile the XZ and VJ software can read.':'Saved on the USB only. Nothing is uploaded.';
  const total=logoBytes(d);
  const fields=el('div',{class:'djp-fields'},
    field('DJ name',input({type:'text',id:'djp-name',value:d.dj_name,autocomplete:'off'},value=>{d.dj_name=value;}),{key:'name'}),
    field('Tagline',input({type:'text',id:'djp-tagline',value:d.tagline,placeholder:'Optional',autocomplete:'off'},value=>{d.tagline=value;}),{key:'tagline',hint:'Optional. Up to 80 characters.'}),
    el('div',{class:'djp-field'},el('span',{class:'djp-label'},el('span',{text:'Accent colour'})),
      el('div',{class:'djp-colour'},
        el('input',{type:'color',id:'djp-accent-pick','aria-label':'Accent colour',value:/^#[0-9a-fA-F]{6}$/.test(d.accent)?d.accent.toLowerCase():DEFAULT_ACCENT,oninput:event=>{d.accent=event.target.value;$('djp-accent').value=d.accent;refreshEditor();}}),
        input({type:'text',id:'djp-accent',value:d.accent,maxlength:'7','aria-label':'Accent colour hex',spellcheck:'false'},value=>{d.accent=value.trim();if(/^#[0-9a-fA-F]{6}$/.test(d.accent))$('djp-accent-pick').value=d.accent.toLowerCase();})),
      el('p',{class:'djp-err',dataset:{err:'accent'},role:'status'})),
    el('div',{class:'djp-group'},
      el('div',{class:'djp-group-head'},el('h3',{text:'Logos'}),el('small',{text:'The main logo goes on the XZ tile and in the header.'})),
      d.logos.length?el('div',{class:'djp-logos',role:'list'},...d.logos.map(logoRow)):null,
      el('div',{class:'actions'},el('button',{type:'button',class:'secondary',id:'djp-add-logo',disabled:d.logos.length>=LIMITS.logos?true:null,text:'Add logo',onclick:addLogo}),
        d.logos.length>=LIMITS.logos?el('small',{class:'djp-hint',text:'8 logos is the most a profile holds.'}):el('small',{class:'djp-hint',text:'PNG or JPEG, up to 8 MB each.'})),
      total>=BUDGET_SHOWN_FROM?el('small',{class:'djp-hint',id:'djp-budget',text:total>LIMITS.totalBytes?'Over the 24 MB logo limit by '+mb(total-LIMITS.totalBytes)+'.':mb(LIMITS.totalBytes-total)+' of 24 MB left for logos.'}):null,
      el('p',{class:'djp-err',dataset:{err:'logos'},role:'status',text:d.logoError})),
    el('div',{class:'djp-group'},
      el('div',{class:'djp-group-head'},el('h3',{text:'Links'}),el('small',{text:'Link to your EPK or files (Google Drive, Dropbox…). https:// only.'})),
      d.links.length?el('div',{class:'djp-links',role:'list'},...d.links.map(linkRow)):null,
      el('div',{class:'actions'},el('button',{type:'button',class:'secondary',id:'djp-add-link',disabled:d.links.length>=LIMITS.links?true:null,text:'Add link',
        onclick:()=>{d.links.push({kind:d.links.some(link=>link.kind==='media-kit')?'website':'media-kit',url:''});render();}})),
      el('p',{class:'djp-err',dataset:{err:'links'},role:'status'})),
    el('details',{class:'djp-more',open:d.moreOpen?true:null,ontoggle:event=>{d.moreOpen=event.target.open;}},
      el('summary',{text:'More: other names and a note'}),
      el('div',{class:'djp-more-body'},field('Other names',el('textarea',{rows:'3',id:'djp-aliases',placeholder:'One per line',oninput:event=>{d.aliases=event.target.value;refreshEditor();}},d.aliases),{key:'aliases',hint:'Names VJ software should also match, like a B2B alias. Up to 8.'}),
      field('Note',el('textarea',{rows:'3',id:'djp-note',oninput:event=>{d.note=event.target.value;refreshEditor();}},d.note),{key:'note',hint:'For VJs. Plain text.'}))),
    d.first?el('p',{class:'djp-hint',text:'This is the first profile on the USB, so the XZ shows it.'}):
      others&&!d.wasActive?el('label',{class:'check'},el('input',{type:'checkbox',id:'djp-make-active',checked:d.makeActive?true:null,onchange:event=>{d.makeActive=event.target.checked;}}),' Make this the active profile on the XZ'):null);
  const status=el('span',{class:'djp-save-why',role:'status'});
  const saveButton=el('button',{type:'button',class:'primary',id:'djp-save',text:saving?'Saving…':'Save to USB',onclick:save});
  const pane=el('section',{class:'wz-pane djp-edit',dataset:{busy:saving?'1':null}},
    el('h2',{text:title}),el('p',{class:'wz-lead',text:lead}),
    banner(P.error),
    el('div',{class:'djp-editor'},fields,el('div',{class:'djp-preview',role:'complementary','aria-label':'Preview'},el('h3',{text:'On the XZ'}),el('div',{class:'djp-preview-slot'}))),
    el('div',{class:'actions djp-save-row'},saveButton,el('button',{type:'button',class:'secondary',text:'Cancel',disabled:saving?true:null,onclick:()=>set({phase:'list',draft:null,error:null})}),status));
  editorParts={pane,saveButton,status};
  return pane;
}
function refreshEditor(){
  if(!editorParts||!P.draft)return;
  const {pane,saveButton,status}=editorParts,{problems,save}=validate(P.draft),saving=P.phase==='saving';
  for(const line of pane.querySelectorAll('[data-err]')){const key=line.dataset.err;line.textContent=key==='logos'?problems.logos||P.draft.logoError:problems[key]||'';}
  saveButton.disabled=saving||Boolean(save);status.textContent=saving?'':save;
  pane.querySelector('.djp-preview-slot').replaceChildren(preview());
}

function trackSteps(current){
  const index=TRACK_STEPS.findIndex(([id])=>id===current);
  return el('ol',{class:'wz-steps djp-steps','aria-label':'Profile track steps'},...TRACK_STEPS.map(([id,text],i)=>el('li',{},
    el('span',{class:'wz-step',dataset:{state:i===index?'current':i<index?'done':'todo'},'aria-current':i===index?'step':null},el('span',{class:'wz-step-n',text:i<index?'✓':String(i+1)}),el('span',{text})))));
}
function pathLine(path){
  return el('div',{class:'djp-path'},el('input',{type:'text',readonly:true,value:path,'aria-label':'Track file'}),
    el('button',{type:'button',class:'secondary',text:'Copy path',onclick:event=>copyPath(event.currentTarget,path)}));
}
function trackWhere(t){
  return [el('p',{class:'wz-lead',text:TRACK_LEAD}),
    el('div',{class:'djp-dest'},
      el('div',{class:'djp-dest-row',dataset:{dest:'folder'}},el('span',{class:'tree-dot',dataset:{tone:'ok'},'aria-hidden':'true'}),
        el('span',{class:'djp-dest-text'},el('strong',{text:'Save to this computer for rekordbox'}),el('small',{id:'djp-track-folder',text:t.folder||'Music › DJ Profile in your user folder'})),
        el('button',{type:'button',class:'text-button',text:'Choose folder',onclick:pickTrackFolder})),
      el('label',{class:'djp-dest-row'},el('input',{type:'checkbox',id:'djp-usb-copy',checked:t.usbCopy?true:null,onchange:event=>{t.usbCopy=event.target.checked;}}),
        el('span',{class:'djp-dest-text'},el('strong',{text:'Also put a copy on this USB'}),el('small',{text:'In DJPROFILE, next to the profile. VJ software on the network can read it from there.'})))),
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'djp-track-create',text:'Create profile track',onclick:writeTrack}),
      el('button',{type:'button',class:'secondary',text:'Cancel',onclick:()=>set({phase:'list',track:null})}))];
}
function trackCreate(t){
  if(t.running)return [el('p',{class:'wz-lead',role:'status',text:'Writing the track and checking every sample…'})];
  if(!t.result)return [banner(t.error,[el('button',{type:'button',class:t.error?.kind==='conflict'?'secondary':'primary',text:'Try again',onclick:writeTrack}),el('button',{type:'button',class:'secondary',text:'Back',onclick:()=>{t.step='where';t.error=null;render();}})])];
  const r=t.result,links=t.links.map(link=>kindLabel(link.kind));
  const facts=[['File',baseName(r.path)],['Length',Math.round(r.duration_seconds)+' seconds of silence'],
    ['Logos',r.logos_embedded?plural(r.logos_embedded,'logo')+' embedded at full resolution':'No logo embedded']];
  if(r.logos_skipped?.length)facts.push(['Left out',r.logos_skipped.join(', ')+' (no room in the track)']);
  facts.push(['Links',links.length?links.join(', '):'None']);
  facts.push(['Sound','Every sample is silent. Checked.'+(r.ffmpeg_verified?' ffmpeg played it back and heard nothing.':'')]);
  if(t.usb)facts.push(['On the USB','A copy is in DJPROFILE on '+driveName()+'.']);
  return [el('h3',{text:'Your profile track is ready'}),
    el('dl',{class:'djp-facts'},...facts.flatMap(([term,text])=>[el('dt',{text:term}),el('dd',{text})])),
    pathLine(r.path),
    banner(t.error),
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'djp-track-next',text:'Next: add it to rekordbox',onclick:()=>{t.step='rekordbox';render();}}))];
}
function trackRekordbox(t){
  const file=baseName(t.result.path);
  return [el('p',{class:'wz-lead',text:'rekordbox puts the track on the USB like any other song. Players that block extra files still read your name, logo and links from it.'}),
    el('ol',{class:'djp-howto'},
      el('li',{text:'Open rekordbox on this computer.'}),
      el('li',{},'Drag ',el('b',{text:file}),' into Collection.'),
      el('li',{},'Make a playlist named ',el('b',{text:'DJ PROFILE'}),' and put the track in it.'),
      el('li',{text:'Export the DJ PROFILE playlist to this USB ('+driveName()+').'}),
      el('li',{text:'Done. Eject the USB and plug it into the player.'})),
    pathLine(t.result.path),
    el('div',{class:'actions'},el('button',{type:'button',class:'primary',id:'djp-track-done',text:'Done',onclick:()=>{P.track=null;P.saved=null;load(P.volume);}}))];
}
function viewTrack(){
  const t=P.track,body=t.step==='where'?trackWhere(t):t.step==='create'?trackCreate(t):trackRekordbox(t);
  return el('section',{class:'wz-pane djp-track',dataset:{step:t.step}},el('h2',{text:'Profile track for '+t.name}),trackSteps(t.step),...body);
}

const VIEWS={'no-usb':viewNoUsb,loading:viewLoading,list:viewList,editing:viewEditor,saving:viewEditor,track:viewTrack};
function render(){
  editorParts=null;
  host.replaceChildren(VIEWS[P.phase]());
  refreshEditor();
}
setupWizard.onRender(sync);
window.djProfilePage={state:P,show,render,load,validate,saveRequest};
render();
})();
