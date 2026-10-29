// Every file XZ Mods knows about on a USB, as a read-only file tree. usbLoadout() turns an inspection into nodes;
// createUsbTree() draws them. States are a table so a row never decides its own wording or colour.
(function(){
const STATES={
  current:{badge:'Up to date',tone:'ok'},
  present:{badge:'On USB',tone:'ok'},
  outdated:{badge:'Update available',tone:'warn'},
  unknown:{badge:'Unknown version',tone:'warn'},
  missing:{badge:'Not on USB',tone:'off'},
  untouched:{badge:'Never touched',tone:'quiet'}
};
const LEGEND=[['ok','Up to date'],['warn','Update available'],['off','Not on USB']];
const STEMS='CDJMODS\\stems',REKORDBOX='PIONEER\\rekordbox',BACKUPS='VJ.Tools\\XZ-Mods-updates',SETTINGS='VJ.Tools\\XZ-Mods.cfg',DJPROFILE='DJPROFILE';
const mb=bytes=>(bytes/1048576).toFixed(bytes>=104857600?0:1)+' MB';
const gb=bytes=>String(+(bytes/1e9).toFixed(bytes>=1e11?0:1))+' GB';
const size=bytes=>bytes>=1e9?gb(bytes):mb(bytes);
const versionName=version=>version?.label||version?.id||'';

function relative(path,volume){
  const text=String(path||''),root=String(volume||'');
  return root&&text.toLowerCase().startsWith(root.toLowerCase())?text.slice(root.length).replace(/^[\\/]+/,''):text;
}
function node(id,depth,kind,path,label,state,detail,children=[]){return {id,depth,kind,path,label,state,detail,children};}

function usbLoadout(inspection,{driveLabel}={}){
  const ins=inspection,volume=ins.volume,saved=savedModVersion();
  const loaderNode=()=>{
    const recorded=ins.recorded_mod_version?.id?ins.recorded_mod_version:null,backups=ins.backups?.length||0;
    const state=!ins.loader.exists?'missing':!recorded?'unknown':!saved?'present':recorded.id===saved.id&&recorded.runtime_sha256===saved.runtime_sha256?'current':'outdated';
    const detail=ins.loader.exists?(versionName(recorded)||'Unknown build')+' · '+plural(backups,'backup')+' kept':'The file the XZ starts XZ Mods from';
    return node('loader',1,'file','autoexec.bin','XZ Mods loader',state,detail);
  };
  const settingsNode=()=>{
    const settings=ins.settings,exists=settings.revision?.exists!==false;
    const detail=!settings.editable?settings.reason:!exists?'Not written yet':plural(settings.schema?.fields?.length||Object.keys(settings.values||{}).length,'field');
    return node('settings',2,'file',relative(settings.path,volume)||SETTINGS,'Settings',exists?'present':'missing',detail);
  };
  const backupsNode=()=>{const count=ins.backups?.length||0;return count?node('backups',2,'folder',BACKUPS,'Loader backups','present',count+' kept'):null;};
  const gameNode=game=>node('game-'+game.id,2,'folder',relative(game.folder,volume),game.name,game.installed?'present':'missing',gameNote(GAMES.find(entry=>entry.id===game.id)||{},game));
  const profilesNode=()=>{
    const found=ins.dj_profiles||{count:0,active_name:null},count=found.count||0;
    return node('djprofile',1,'folder',DJPROFILE,'DJ profiles',count?'present':'missing',count?plural(count,'profile')+(found.active_name?' · active '+found.active_name:''):'No DJ profile yet');
  };
  const games=(ins.games||[]).filter(game=>GAMES.some(entry=>entry.id===game.id)),stems=ins.stems||{tracks:0,bytes:0},space=ins.space||{};
  const vjtools=[settingsNode(),backupsNode()].filter(Boolean),gameNodes=games.map(gameNode);
  const anyPresent=list=>list.some(item=>item.state!=='missing');
  const spaceText=space.free_bytes!=null&&space.total_bytes!=null?gb(space.free_bytes)+' free of '+gb(space.total_bytes):'';
  return [node('drive',0,'drive',volume,driveLabel||volume,null,[ins.filesystem,spaceText].filter(Boolean).join(' · '),[
    loaderNode(),
    node('vjtools',1,'folder','VJ.Tools','XZ Mods files',anyPresent(vjtools)?'present':'missing','Settings and loader backups',vjtools),
    node('games',1,'folder','VJTOOLS\\Games','Games',anyPresent(gameNodes)?'present':'missing','Free downloads, opened from EXTRAS › GAMES',gameNodes),
    profilesNode(),
    node('stems',1,'folder',STEMS,'Prepared stems',stems.tracks>0?'present':'missing',stems.tracks>0?plural(stems.tracks,'track')+' · '+size(stems.bytes):'None yet'),
    node('library',1,'folder',REKORDBOX,'Exported music library',ins.rekordbox?'untouched':'missing',ins.rekordbox?'Music and playlists, never touched':'Export your music to this USB from your DJ software first')
  ])];
}

function flatten(nodes){return nodes.flatMap(item=>[item,...flatten(item.children||[])]);}
function dot(tone){return el('span',{class:'tree-dot',dataset:{tone},'aria-hidden':'true'});}
function row(item){
  const state=STATES[item.state];
  return el('div',{class:'tree-row',role:'listitem',style:'--depth:'+item.depth,dataset:{id:item.id,kind:item.kind,state:item.state}},
    el('span',{class:'tree-indent','aria-hidden':'true'}),
    el('span',{class:'tree-icon',dataset:{kind:item.kind},'aria-hidden':'true'}),
    el('span',{class:'tree-name'},el('span',{class:'tree-title'},el('strong',{text:item.label}),el('small',{text:item.path})),
      el('span',{class:'tree-detail',title:item.detail||null,text:item.detail||''})),
    el('span',{class:'tree-status'},state?dot(state.tone):null,el('span',{class:'tree-badge',text:state?.badge||''})));
}
function createUsbTree({nodes}){
  return el('div',{class:'tree'},
    el('div',{class:'tree-rows',role:'list'},...flatten(nodes).map(row)),
    el('ul',{class:'tree-legend','aria-label':'Legend'},...LEGEND.map(([tone,text])=>el('li',{},dot(tone),el('span',{text})))));
}
Object.assign(window,{usbLoadout,createUsbTree});
})();
