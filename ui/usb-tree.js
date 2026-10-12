// The USB loadout as a file tree. usbLoadout() turns an inspection plus the wizard's choices into nodes;
// createUsbTree() draws them. States and plans are tables so a row never decides its own wording or colour.
(function(){
const STATES={
  current:{badge:'Up to date',tone:'ok'},
  present:{badge:'On USB',tone:'ok'},
  outdated:{badge:'Update available',tone:'warn'},
  unknown:{badge:'Unknown version',tone:'warn'},
  missing:{badge:'Not on USB',tone:'off'},
  untouched:{badge:'Never changed by XZ Mods',tone:'quiet'}
};
const PLANS={add:'Will add',update:'Will update',change:'Will change'};
const LEGEND=[['ok','Up to date'],['warn','Update available'],['off','Not on USB'],['plan','Will be written']];
const GAME_NOTES={doom:'The original free first episode',chex:'The cereal promo game, with its patch',freedoom:'A complete free base game',myhouse:'A Doom II mod'};
const STEMS='CDJMODS\\stems',REKORDBOX='PIONEER\\rekordbox',BACKUPS='VJ.Tools\\XZ-Mods-updates',SETTINGS='VJ.Tools\\XZ-Mods.cfg';
const mb=bytes=>(bytes/1048576).toFixed(bytes>=104857600?0:1)+' MB';
const gb=bytes=>String(+(bytes/1e9).toFixed(bytes>=1e11?0:1))+' GB';
const size=bytes=>bytes>=1e9?gb(bytes):mb(bytes);
const versionName=version=>version?.label||version?.id||'';

function relative(path,volume){
  const text=String(path||''),root=String(volume||'');
  return root&&text.toLowerCase().startsWith(root.toLowerCase())?text.slice(root.length).replace(/^[\\/]+/,''):text;
}
function node(id,depth,kind,path,label,state,detail,plan=null,control=null,children=[]){return {id,depth,kind,path,label,state,detail,plan,control,children};}

// choices is null for the Done step: the tree then shows the USB as it is, with no plans or controls.
// choices: {loader, saved, games:Set, changes:[keys], settingsEditable, versionLine, setLoader, toggleGame, allGames, noGames, editSettings}
function usbLoadout(inspection,{driveLabel,choices=null}={}){
  const ins=inspection,volume=ins.volume,c=choices,saved=c?c.saved:savedModVersion();
  const planned=plan=>c?plan:null;
  const loaderNode=()=>{
    const recorded=ins.recorded_mod_version?.id?ins.recorded_mod_version:null,backups=ins.backups?.length||0;
    const state=!ins.loader.exists?'missing':!recorded?'unknown':!saved?'present':recorded.id===saved.id&&recorded.runtime_sha256===saved.runtime_sha256?'current':'outdated';
    const detail=ins.loader.exists?(versionName(recorded)||'Unknown build')+' · '+plural(backups,'backup')+' kept':'The file the XZ starts XZ Mods from';
    const writing=c&&c.loader!=='keep',savedName=versionName(saved);
    const plan=planned(writing?(state==='missing'?'add':'update'):null);
    let control=null;
    if(c){
      const toggle=on=>c.setLoader(on?(state==='missing'?'add':'update'):'keep');
      if(state==='missing')control={kind:'check',checked:writing,disabled:!saved,label:saved?'Add the loader ('+savedName+')':'Add the loader',onToggle:toggle};
      else if(state==='current'&&!writing)control={kind:'link',label:'Reinstall',onClick:()=>c.setLoader('update')};
      else control={kind:'check',checked:writing,disabled:!saved,label:saved?(state==='current'?'Reinstall ':'Update to ')+savedName:'Update the loader',onToggle:toggle};
    }
    const item=node('loader',1,'file','autoexec.bin','XZ Mods loader',state,detail,plan,control);
    if(c&&(writing||!saved))item.aside=c.versionLine;
    return item;
  };
  const settingsNode=()=>{
    const settings=ins.settings,exists=settings.revision?.exists!==false,changes=c?c.changes:[];
    let plan=null,detail;
    if(!settings.editable)detail=settings.reason;
    else if(changes.length){plan=planned('change');detail=changes.slice(0,3).map(key=>window.SETTINGS_MENU_LABEL?.[key]||key).join(', ')+(changes.length>3?' and '+(changes.length-3)+' more':'');}
    else if(!exists){plan=planned(c&&(c.loader!=='keep'||c.settingsEditable)?'add':null);detail=plan?'New file with defaults':'Not written yet';}
    else detail=plural(settings.schema?.fields?.length||Object.keys(settings.values||{}).length,'field')+' · matches USB';
    const item=node('settings',2,'file',relative(settings.path,volume)||SETTINGS,'Settings',exists?'present':'missing',detail,plan,c?{kind:'link',label:'Edit settings',onClick:c.editSettings}:null);
    if(plan==='change')item.planCount=changes.length;
    return item;
  };
  const backupsNode=()=>{
    const count=ins.backups?.length||0;
    return count?node('backups',2,'folder',BACKUPS,'Loader backups','present',count+' kept',null,c?{kind:'link',label:'Restore in Advanced tools',onClick:()=>showPage('advanced')}:null):null;
  };
  const gameNode=game=>{
    const ticked=Boolean(c?.games.has(game.id))&&!game.installed;
    const detail=(GAME_NOTES[game.id]||'')+(game.requires_gzdoom?' · Not playable on the XZ yet':'');
    const control=c&&!game.installed?{kind:'check',checked:ticked,label:'Add · '+mb(game.bytes),onToggle:on=>c.toggleGame(game.id,on)}:null;
    return node('game-'+game.id,2,'folder',relative(game.folder,volume),game.name,game.installed?'present':'missing',detail,planned(ticked?'add':null),control);
  };
  const games=ins.games||[],stems=ins.stems||{tracks:0,bytes:0},space=ins.space||{};
  const vjtools=[settingsNode(),backupsNode()].filter(Boolean),gameNodes=games.map(gameNode);
  const anyPresent=list=>list.some(item=>item.state!=='missing');
  const spaceText=space.free_bytes!=null&&space.total_bytes!=null?gb(space.free_bytes)+' free of '+gb(space.total_bytes):'';
  return [node('drive',0,'drive',volume,driveLabel||volume,null,[ins.filesystem,spaceText].filter(Boolean).join(' · '),null,null,[
    loaderNode(),
    node('vjtools',1,'folder','VJ.Tools','XZ Mods files',anyPresent(vjtools)?'present':'missing','Settings and loader backups',null,null,vjtools),
    node('games',1,'folder','VJTOOLS\\Games','Games',anyPresent(gameNodes)?'present':'missing','Free downloads, opened from EXTRAS › GAMES',null,
      c?[{kind:'link',label:'Add all free games',onClick:c.allGames},{kind:'link',label:'None',onClick:c.noGames}]:null,gameNodes),
    node('stems',1,'folder',STEMS,'Prepared stems',stems.tracks>0?'present':'missing',stems.tracks>0?plural(stems.tracks,'track')+' · '+size(stems.bytes):'None yet',null,
      c?{kind:'link',label:'Prepare stems',onClick:()=>showPage('stems')}:null),
    node('rekordbox',1,'folder',REKORDBOX,'Rekordbox library',ins.rekordbox?'untouched':'missing',ins.rekordbox?'Music and playlists, never touched':'Export from Rekordbox to this USB first')
  ])];
}

function flatten(nodes){return nodes.flatMap(item=>[item,...flatten(item.children||[])]);}
function dot(tone){return el('span',{class:'tree-dot',dataset:{tone},'aria-hidden':'true'});}
function controlEl(control){
  if(control.kind==='check')return el('span',{class:'tree-check'},el('input',{type:'checkbox',checked:control.checked,disabled:control.disabled,onchange:event=>control.onToggle(event.target.checked)}),el('span',{text:control.label}));
  return el('button',{type:'button',class:'text-button tree-link',text:control.label,onclick:event=>{event.preventDefault();control.onClick();}});
}
function row(item,mode){
  const state=STATES[item.state],plan=mode!=='result'&&item.plan?item.plan:null;
  const controls=mode==='edit'?[].concat(item.control||[]):[],check=controls.find(control=>control.kind==='check');
  const badge=plan?PLANS[plan]+(item.planCount?' · '+item.planCount:''):state?.badge||'';
  const tone=plan?'plan':state?.tone;
  return el(check?'label':'div',{class:'tree-row',style:'--depth:'+item.depth,dataset:{id:item.id,kind:item.kind,state:item.state,plan:plan?'1':null,dim:mode==='plan'&&!plan?'1':null}},
    el('span',{class:'tree-indent','aria-hidden':'true'}),
    el('span',{class:'tree-icon',dataset:{kind:item.kind},'aria-hidden':'true'}),
    el('span',{class:'tree-name'},el('span',{class:'tree-title'},el('strong',{text:item.label}),el('small',{text:item.path})),
      el('span',{class:'tree-detail',title:item.detail||null,text:item.detail||''})),
    el('span',{class:'tree-status'},tone?dot(tone):null,el('span',{class:'tree-badge',text:badge})),
    el('span',{class:'tree-control'},...controls.map(controlEl)));
}
function createUsbTree({nodes,mode}){
  const rows=flatten(nodes).flatMap(item=>[row(item,mode),mode==='edit'&&item.aside?el('div',{class:'tree-aside',style:'--depth:'+(item.depth+1)},item.aside):null]);
  return el('div',{class:'tree',dataset:{mode}},
    el('div',{class:'tree-rows',role:'list'},...rows),
    el('ul',{class:'tree-legend','aria-label':'Legend'},...LEGEND.map(([tone,text])=>el('li',{},dot(tone),el('span',{text})))));
}
Object.assign(window,{usbLoadout,createUsbTree});
})();
