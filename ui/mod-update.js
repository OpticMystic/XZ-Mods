// What changes on the XZ between the loader on a USB and the mod version about to be written. Each build in
// MOD_CHANGES lists the change ids its source contains, so an update is a set difference between two builds.
(function(){
const KINDS=[['new','New'],['improved','Improved'],['fixed','Fixed']];
const LIMIT=6;
const versionName=version=>version?.label||version?.id||'';

function buildIds(version){
  const sha=String(version?.source_commit||'').toLowerCase(),builds=window.MOD_CHANGES?.builds||{};
  if(!/^[0-9a-f]{7,40}$/.test(sha))return null;
  const key=Object.keys(builds).find(build=>build.startsWith(sha)||sha.startsWith(build));
  return key?builds[key]:null;
}
const changes=ids=>ids.map(id=>({id,...window.MOD_CHANGES.changes[id]}));

function modUpdate(from,to){
  const target=buildIds(to),source=from?buildIds(from):null;
  if(!target)return {kind:'unavailable',from,to,added:[],removed:[]};
  if(!source)return {kind:'unknown',from,to,added:changes(target),removed:[]};
  const had=new Set(source),has=new Set(target);
  const added=changes(target.filter(id=>!had.has(id))),removed=changes(source.filter(id=>!has.has(id)));
  return {kind:added.length?'upgrade':removed.length?'downgrade':'reinstall',from,to,added,removed};
}
const changeCount=update=>['upgrade','downgrade'].includes(update.kind)?update.added.length+update.removed.length:0;

function item(change){return el('li',{class:'mu-item'},el('span',{class:'mu-area',text:change.area}),el('span',{text:change.text}));}
// {expanded, onExpand} keep "Show all" open across re-renders; the caller owns that flag.
function modUpdatePanel(update,{expanded,onExpand}){
  const to=versionName(update.to);
  if(update.kind==='unavailable')return el('p',{class:'mu-quiet',text:'Notes for '+to+' are not available yet.'});
  if(update.kind==='reinstall')return null;
  const shown=expanded?update.added:update.added.slice(0,LIMIT);
  const groups=KINDS.map(([kind,title])=>[title,shown.filter(change=>change.kind===kind)]).filter(([,list])=>list.length);
  return el('section',{class:'mu','aria-label':'What changes on your XZ'},
    el('div',{class:'mu-head'},el('h4',{text:'What changes on your XZ'}),el('small',{text:(versionName(update.from)||'Unknown version')+' → '+to})),
    update.kind==='unknown'?el('p',{class:'mu-note',text:'The version of the loader on this USB is not known. It was written on another computer or by an older app. Here is what '+to+' brings.'}):null,
    el('p',{class:'mu-note',text:'This list comes from what the build contains. Not every change has been tested on a deck.'}),
    ...groups.map(([title,list])=>el('div',{class:'mu-group',dataset:{group:title}},el('h5',{text:title}),el('ul',{},...list.map(item)))),
    update.added.length>LIMIT&&!expanded?el('button',{type:'button',class:'text-button mu-more',text:'Show all '+update.added.length+' changes',onclick:onExpand}):null,
    update.removed.length?el('div',{class:'mu-group',dataset:{group:'removed'}},el('h5',{},el('span',{class:'tree-dot',dataset:{tone:'warn'},'aria-hidden':'true'}),el('span',{text:'Not in '+to})),
      el('ul',{},...update.removed.map(item))):null);
}
Object.assign(window,{modUpdate,modUpdatePanel,modChangeCount:changeCount});
})();
