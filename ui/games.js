// Every game a DJ can have on the XZ, in the order of the XZ's GAMES menu. Built-in games are compiled into the
// loader; downloads are files copied to VJTOOLS\Games and listed by the USB inspection. `bit` is the game's bit in
// the games_menu setting, which hides entries from MODS › EXTRAS › GAMES once the loader schema carries it.
(function(){
const GAMES=[
  {id:'doom',name:'Doom shareware',kind:'download',bit:4,blurb:'The original free first episode'},
  {id:'chex',name:'Chex Quest',kind:'download',bit:4,blurb:'The cereal promo game, with its patch'},
  {id:'freedoom',name:'Freedoom Phase 2',kind:'download',bit:4,blurb:'A complete free base game'},
  {id:'beat-arcade',name:'Beat Arcade',kind:'builtin',bit:0,blurb:'Pong and Breakout played to the beat with the jog wheels'},
  {id:'wave-rider',name:'Wave Rider',kind:'builtin',bit:1,blurb:'Surf the hills your song draws, solo or with a friend'},
  {id:'rhythm-lab',name:'Rhythm Lab',kind:'builtin',bit:2,blurb:'Ten short studies of ways to play along with a track'},
  {id:'static-surfer',name:'Static Surfer',kind:'builtin',bit:3,blurb:'Jump a glitch cat over the waveform on the beat'}
];
const GROUPS=[
  {kind:'builtin',title:'Your games',note:'Built into the loader.',fixed:'Built into the loader. Always in MODS › EXTRAS › GAMES.'},
  {kind:'download',title:'Free downloads',note:'Copied to VJTOOLS\\Games on the USB.'}
];
const mb=bytes=>(bytes/1048576).toFixed(bytes>=104857600?0:1)+' MB';
const gameNote=game=>game.blurb||'';

function dot(tone){return el('span',{class:'tree-dot',dataset:{tone},'aria-hidden':'true'});}
function menuSwitch(game,menu){
  const on=Boolean(menu.value&1<<game.bit);
  return el('label',{class:'games-switch'},el('input',{type:'checkbox',role:'switch',checked:on,dataset:{bit:game.bit},onchange:event=>menu.set(game.bit,event.target.checked)}),el('span',{text:'Show in GAMES menu'}));
}
// {status, tone, plan, control} for a row. Built-ins only depend on the loader; downloads on the inspection and the ticks.
function rowState(game,found,props){
  if(game.kind==='builtin')return {status:props.loaderOnUsb?'In the loader':'Comes with the loader',tone:props.loaderOnUsb?'ok':'off'};
  if(found.installed)return {status:'On USB',tone:'ok'};
  const ticked=props.games.has(game.id);
  return {status:ticked?'Will add':'Not on USB',tone:ticked?'plan':'off',plan:ticked,
    control:el('label',{class:'tree-check'},el('input',{type:'checkbox',checked:ticked,onchange:event=>props.toggleGame(game.id,event.target.checked)}),el('span',{text:'Add · '+mb(found.bytes)}))};
}
function gameRow(game,found,props){
  const state=rowState(game,found,props);
  return el('div',{class:'games-row',role:'listitem',dataset:{id:game.id,kind:game.kind,plan:state.plan?'1':null}},
    el('span',{class:'games-name'},el('strong',{text:game.name}),el('span',{class:'games-blurb',text:gameNote(game,found)})),
    el('span',{class:'tree-status'},dot(state.tone),el('span',{class:'tree-badge',text:state.status})),
    el('span',{class:'games-controls'},state.control||null,props.menu&&state.status!=='Not on USB'?menuSwitch(game,props.menu):null));
}
// Built-in games are compiled into the loader. Without games_menu they cannot be left out, so they are one line, not rows.
function builtinLine(){
  const names=GAMES.filter(game=>game.kind==='builtin').map(game=>game.name);
  return el('p',{class:'games-builtin-line',text:names.slice(0,-1).join(', ')+' and '+names.at(-1)+' are built into the loader and always come with it.'});
}
// props: {inspection, games:Set, loaderOnUsb, menu:{value,set(bit,on)}|null, toggleGame(id,on), allGames(), noGames()}
function createGamesChooser(props){
  const found=new Map((props.inspection.games||[]).map(game=>[game.id,game]));
  const offered=GAMES.filter(game=>game.kind==='builtin'||found.has(game.id));
  const missing=offered.some(game=>game.kind==='download'&&!found.get(game.id).installed);
  return el('div',{class:'games'},...GROUPS.map(group=>group.kind==='builtin'&&!props.menu?builtinLine():el('section',{class:'games-group',dataset:{kind:group.kind}},
    el('div',{class:'games-head'},el('h3',{text:group.title}),el('small',{class:'games-note',text:!props.menu&&group.fixed||group.note}),
      group.kind==='download'&&missing?el('span',{class:'games-bulk'},
        el('button',{type:'button',class:'text-button',text:'Add all free games',onclick:props.allGames}),
        el('button',{type:'button',class:'text-button',text:'None',onclick:props.noGames})):null),
    el('div',{class:'games-rows',role:'list'},...offered.filter(game=>game.kind===group.kind).map(game=>gameRow(game,found.get(game.id),props))))));
}
Object.assign(window,{GAMES,createGamesChooser,gameNote});
})();
