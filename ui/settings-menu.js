// The USB settings editor drawn like the XZ's own MODS menu: header, five-tab rail, two-column rows,
// info dock. Pages and rows are one table (PAGES); the on-device tree in mods/ui/ui.c is the reference.
// createSettingsMenu(options) returns {el, render, setValues}. Options: schema, values, onChange(values), save
// ({label, tone, action}) for the dock's right side, usbLabel, onGames() for the EXTRAS > GAMES card.
(function(){
const STEM_ORDERS=[[2,1,0],[2,0,1],[1,2,0],[1,0,2],[0,2,1],[0,1,2]];
const STEM_NAMES=['DRUMS','HARMONICS','VOCALS'];
const STEM_COLOR_VALUES=[null,'#2997ff','#ff3b30','#30d158','#ffa000','#af52de','#32ade6','#ff2d55','#ffffff'];
const JUMP_SIZES=['1 BEAT','2 BEATS','1 BAR','2 BARS','4 BARS','8 BARS','16 BARS','16 BARS','32 BARS','64 BARS'];
const RAIL=[['general','GENERAL'],['controls','CONTROLS'],['appearance','APPEARANCE'],['advanced','ADVANCED'],['extras','EXTRAS']];
// Device labels for saved fields. Schema labels are the desktop wording; the XZ shows these.
const LABEL={stems:'STEM AUDIO',gate:'GATE CUE',smart:'SMART CUE',stem_page:'PAD MODE',shift_pages:'SHIFT + PAD MODES',pad_feedback:'PAD LIGHTS',
  shift_keysync:'SHIFT + SYNC',fb_takeover:'VJ.TOOLS VIEW',takeover_assign:'SCREEN SWITCH BUTTON',jump_enabled:'CUSTOM BEAT JUMP',jump_shift_page:'SHIFT + BEAT JUMP',
  stems_overlay:'STEM ROW LAYOUT',wave_mode:'SCROLLING WAVEFORM',browse_1:'BROWSE COLUMN 1',browse_2:'BROWSE COLUMN 2',browse_3:'BROWSE COLUMN 3',
  browse_key_prefix:'KEY BEFORE TITLE',browse_traffic:'COLOR COMPATIBLE KEYS',key_format:'KEY FORMAT',key_feedback:'KEY FEEDBACK',ram_debug:'FREE RAM BOX',
  rekordbox_menu:'REKORDBOX KEY',metrics_enabled:'METRICS',stem_order:'STEM ORDER',theme:'THEME',stem_bank:'STEM PADS'};
const CHOICE_LABEL={takeover_assign:['LINK','REKORDBOX','ONSCREEN'],wave_mode:['STOCK WAVE','3-BAND','STEMS'],key_format:['AS XZ SETTING','CLASSIC','CAMELOT'],
  stem_page:['HOT CUE','BEAT LOOP','SLIP LOOP','BEAT JUMP'],stems_overlay:['HIDDEN','BELOW / FULL','BELOW / HALF','OUTSIDE / FULL','OUTSIDE / HALF']};
// What the info dock says about each control. One or two plain sentences, present tense.
const DOCK={
  general:'Turn mods on or off here. Open the settings beside a feature to customize it.',
  controls:'Pads, stem shortcuts and Beat Jump. The diagram shows the pads exactly as the XZ lights them.',
  beatjump:'Each pad pair jumps backward and forward. One bar is four beats.',
  appearance:'Pick the theme the XZ draws its screens in. The preview here uses the same colors.',
  stemcolors:'Colors for the three stem rows, pads and waveform layers. Theme default follows the chosen theme.',
  advanced:'Keys, display and RAM tracks. Changes here are rarely needed during a set.',
  keysetup:'How keys are shown in the browser and on the decks.',
  extras:'Games, VJ.Tools and network updates live under EXTRAS on the XZ.',
  vjtools:'The VJ.Tools screen view shows your VJ output on the XZ display.',
  browse:'Browse columns are read from the USB settings. The XZ shows them in the track list; they are edited here on the PC.',
  stems:'Plays prepared stems from this USB. Off leaves the XZ playing the original mix.',
  gate:'Hot cue pads play only while held.',smart:'Hot cues follow the beat grid when triggered.',
  stem_page:'Which pad mode gives four of its pads to stem shortcuts.',shift_pages:'Holding Shift reaches the stem shortcuts from the other pad modes too.',
  pad_feedback:'Pad lights show which stems are on.',shift_keysync:'Shift + Sync toggles Key Sync instead of Beat Sync.',
  fb_takeover:'Shows the VJ.Tools output on the XZ screen when the switch button is pressed.',takeover_assign:'Which button switches between the XZ screen and the VJ.Tools view.',
  jump_enabled:'Custom Beat Jump replaces the stock Beat Jump pad sizes with your own.',jump_shift_page:'Shift + Beat Jump opens the second page of sizes.',
  stems_overlay:'Where the stem rows sit on the waveform screen, and how tall.',wave_mode:'Stock draws the original waveform. 3-BAND and STEMS draw layered waveforms from the USB analysis.',
  browse_1:'First column in the track browser.',browse_2:'Second column in the track browser.',browse_3:'Third column in the track browser.',
  browse_key_prefix:'Shows the key before each track title.',browse_traffic:'Colors keys that mix well with the playing track.',key_format:'Key names as the XZ setting, classic notation or Camelot numbers.',
  key_feedback:'Shows the effective key and Key Sync state on the deck.',ram_debug:'Shows free RAM on the screen. For troubleshooting.',rekordbox_menu:'The REKORDBOX key opens and closes MODS. Turn off to keep the stock key.',
  metrics_enabled:'On-device metrics and temperature warnings in the top bar.',stem_order:'The order of the three stem rows and pads on screen. The diagram updates as you change it.',
  stem_bank:'Tap a pad to choose which four pads carry the stem shortcuts.',theme:'Tap a theme card to choose it. The XZ redraws every screen in it.',
  jump:'Tap a pair, then use the arrows to change its jump size.',games:'Games are chosen in the Games step of this wizard; on the XZ they open from EXTRAS > GAMES.'
};
const PAGES={
  general:{title:'GENERAL',rows:[['toggle','stems'],['link','STEM SETTINGS','controls'],['toggle','jump_enabled'],['link','JUMP SETTINGS','beatjump'],['toggle','gate'],['toggle','smart'],
    ['link','STEM COLORS','stemcolors'],['link','APPEARANCE','appearance'],['toggle','metrics_enabled'],['link','KEYS / DISPLAY / RAM','advanced']]},
  controls:{title:'CONTROLS',tabs:[['controls','STEM SHORTCUTS'],['beatjump','BEAT JUMP']],rows:[['cycle','stems_overlay',2],['pads'],['toggle','pad_feedback'],['cycle','stem_order',1],['choice','stem_page',2]]},
  beatjump:{title:'CONTROLS',tabs:[['controls','STEM SHORTCUTS'],['beatjump','BEAT JUMP']],rows:[['toggle','jump_shift_page'],['jumppads']]},
  appearance:{title:'APPEARANCE',rows:[['link','STEM COLORS','stemcolors'],['themes']]},
  stemcolors:{title:'STEM COLORS',rows:[['stemcolors']]},
  advanced:{title:'ADVANCED',rows:[['link','KEY SETUP','keysetup'],['link','BROWSE COLUMNS','browse'],['toggle','shift_keysync'],['toggle','key_feedback'],['toggle','ram_debug'],['toggle','shift_pages'],['toggle','rekordbox_menu'],['choice','wave_mode',2]]},
  keysetup:{title:'KEY SETUP',rows:[['toggle','shift_keysync'],['toggle','key_feedback'],['choice','key_format',2]]},
  browse:{title:'BROWSE COLUMNS',rows:[['cycle','browse_1',1],['cycle','browse_2',1],['cycle','browse_3',1],['toggle','browse_key_prefix'],['toggle','browse_traffic']]},
  extras:{title:'EXTRAS',rows:[['card','VJ.TOOLS','SCREEN VIEW AND LINK','vjtools'],['card','GAMES','CHOSEN IN THE GAMES STEP','games']]},
  vjtools:{title:'VJ.TOOLS',rows:[['toggle','fb_takeover'],['choice','takeover_assign',2]]}
};
const PARENT={controls:'general',beatjump:'controls',appearance:'general',stemcolors:'appearance',advanced:'general',keysetup:'advanced',browse:'advanced',extras:'general',vjtools:'extras'};
const ROOT={general:'general',controls:'controls',beatjump:'controls',appearance:'appearance',stemcolors:'appearance',advanced:'advanced',keysetup:'advanced',browse:'advanced',extras:'extras',vjtools:'extras'};

function el(tag,attrs,...children){
  const item=document.createElement(tag);
  for(const [key,value] of Object.entries(attrs||{})){
    if(value===undefined||value===null||value===false)continue;
    if(key==='class')item.className=value;else if(key==='text')item.textContent=value;else if(key.startsWith('on'))item[key]=value;
    else if(key==='dataset')Object.assign(item.dataset,value);else item.setAttribute(key,value===true?'':value);
  }
  for(const child of children)if(child!==null&&child!==undefined&&child!==false)item.append(child);
  return item;
}
function createSettingsMenu(options){
  const schema=options.schema,fields=new Map(schema.fields.map(field=>[field.key,field]));
  let values={...options.values},page='general',history=[],focus={kind:'page',id:'general'},jumpPair=0;
  const root=el('div',{class:'dv',role:'group','aria-label':'USB settings, laid out like the MODS menu on the XZ'});
  const label=key=>LABEL[key]||fields.get(key)?.label.toUpperCase()||key;
  const choices=key=>{const field=fields.get(key);return field.choices.map((choice,index)=>({value:choice.value,label:(CHOICE_LABEL[key]||[])[index]||choice.label.toUpperCase()}));};
  const chosen=key=>choices(key).find(choice=>choice.value===values[key])||choices(key)[0];
  function set(key,value){
    if(values[key]===value)return;
    values={...values,[key]:value};options.onChange?.(values);render();
  }
  function go(next){if(next===page)return;history.push(page);page=next;focus={kind:'page',id:next};render();}
  function back(){if(!history.length)return;page=history.pop();focus={kind:'page',id:page};render();}
  function state(key){
    const field=fields.get(key);if(!field)return '';
    if(field.kind==='boolean')return values[key]?'ON':'OFF';
    return chosen(key).label;
  }
  function dock(){
    const target=focus.kind==='page'?{title:PAGES[focus.id].title,chip:'OPEN',body:DOCK[focus.id]}:
      focus.kind==='field'?{title:label(focus.id),chip:state(focus.id),body:DOCK[focus.id]||fields.get(focus.id)?.label}:
      focus.kind==='link'?{title:focus.label,chip:'OPEN',body:DOCK[focus.id]}:{title:focus.title,chip:focus.chip||'',body:DOCK[focus.id]||''};
    const save=options.save?.()||null;
    return el('div',{class:'dv-dock'},
      el('div',{class:'dv-dock-main'},
        el('div',{class:'dv-dock-title'},el('span',{text:target.title}),target.chip?el('span',{class:'dv-chip',dataset:{tone:target.chip==='ON'||target.chip==='OPEN'?'on':target.chip==='OFF'?'off':'value'},text:target.chip}):null),
        el('p',{class:'dv-dock-body',text:target.body||''})),
      el('div',{class:'dv-dock-side'},
        save?el('span',{class:'dv-save',dataset:{tone:save.tone},text:save.label}):null,
        save?.action?el('button',{type:'button',class:'dv-primary',text:save.action.label,onclick:save.action.run,disabled:save.action.disabled?true:null}):null));
  }
  function toggle(key){
    const field=fields.get(key),on=values[key]===1;
    return el('button',{type:'button',class:'dv-row dv-toggle','aria-pressed':on?'true':'false',dataset:{key},onfocus:()=>peek('field',key),onmouseenter:()=>peek('field',key),
      onclick:()=>{focus={kind:'field',id:key};set(key,on?0:1);}},el('span',{class:'dv-row-label',text:label(key)}),el('span',{class:'dv-switch'},el('span',{class:'dv-knob'})));
  }
  function link(text,target){
    return el('button',{type:'button',class:'dv-row dv-link',dataset:{page:target},onfocus:()=>peek('link',target,text),onmouseenter:()=>peek('link',target,text),onclick:()=>go(target)},
      el('span',{class:'dv-row-label',text}),el('span',{class:'dv-chevron','aria-hidden':'true'}));
  }
  function cycle(key,span){
    const list=choices(key),index=list.findIndex(choice=>choice.value===values[key]),step=delta=>set(key,list[(index+delta+list.length)%list.length].value);
    const chips=key==='stem_order'?el('span',{class:'dv-chips'},...STEM_ORDERS[values[key]].map(stem=>el('span',{class:'dv-stem-chip',style:'--stem:'+stemColor(stem),text:STEM_NAMES[stem][0]}))):null;
    return el('div',{class:'dv-row dv-cycle',dataset:{key,span},onmouseenter:()=>peek('field',key)},
      el('button',{type:'button',class:'dv-arrow','aria-label':label(key)+': previous',onclick:()=>{focus={kind:'field',id:key};step(-1);}},'‹'),
      el('span',{class:'dv-cycle-body'},el('span',{class:'dv-row-label',text:label(key)}),el('span',{class:'dv-cycle-value'},chips,el('span',{text:chosen(key).label}))),
      el('button',{type:'button',class:'dv-arrow','aria-label':label(key)+': next',onclick:()=>{focus={kind:'field',id:key};step(1);}},'›'));
  }
  function choice(key,span){
    return el('div',{class:'dv-choice',role:'radiogroup','aria-label':label(key),dataset:{key,span},onmouseenter:()=>peek('field',key)},
      ...choices(key).map(option=>el('button',{type:'button',role:'radio',class:'dv-row dv-option','aria-checked':values[key]===option.value?'true':'false',dataset:{value:option.value},
        onclick:()=>{focus={kind:'field',id:key};set(key,option.value);}},el('span',{class:'dv-row-label',text:option.label}),el('span',{class:'dv-dot'}))));
  }
  function stemColor(stem){
    const key=['stem_drums_color','stem_harmonics_color','stem_vocals_color'][stem],pick=values[key];
    if(pick&&STEM_COLOR_VALUES[pick])return STEM_COLOR_VALUES[pick];
    return (DEVICE_THEMES[values.theme]||DEVICE_THEMES[0]).stems[stem];
  }
  function pads(){
    const order=STEM_ORDERS[values.stem_order]||STEM_ORDERS[0],roles=[...order,'BYPASS'];
    return el('div',{class:'dv-pads',dataset:{span:2},role:'group','aria-label':'Stem shortcut pads',onmouseenter:()=>peek('field','stem_bank')},
      ...Array.from({length:8},(_,pad)=>{
        const bank=Math.floor(pad/4),assigned=bank===values.stem_bank,role=roles[pad%4];
        return el('button',{type:'button',class:'dv-pad',dataset:{assigned:assigned?'1':'0'},'aria-label':'Pads '+(bank?'E to H':'A to D')+' carry the stem shortcuts','aria-pressed':assigned?'true':'false',
          style:assigned&&role!=='BYPASS'?'--stem:'+stemColor(role):null,onclick:()=>{focus={kind:'field',id:'stem_bank'};set('stem_bank',bank);}},
          el('strong',{text:String.fromCharCode(65+pad)}),el('span',{text:assigned?(role==='BYPASS'?'BYPASS':STEM_NAMES[role]):'STOCK'}));
      }));
  }
  function jumppads(){
    const pageTwo=jumpPair>=4,pairs=Array.from({length:4},(_,i)=>i+(pageTwo?4:0));
    const sizeKey='jump_'+(jumpPair+1),index=values[sizeKey];
    return el('div',{class:'dv-jump',dataset:{span:2},onmouseenter:()=>peek('custom','jump','BEAT JUMP PAIRS',JUMP_SIZES[index])},
      el('div',{class:'dv-choice',role:'radiogroup','aria-label':'Beat Jump page'},...[0,1].map(p=>el('button',{type:'button',role:'radio',class:'dv-row dv-option','aria-checked':pageTwo===(p===1)?'true':'false',
        onclick:()=>{jumpPair=p*4+(jumpPair%4);render();}},el('span',{class:'dv-row-label',text:'EDIT PAGE '+(p+1)}),el('span',{class:'dv-dot'})))),
      el('div',{class:'dv-pads dv-jump-pads'},...pairs.flatMap(pair=>{
        const key='jump_'+(pair+1),size=JUMP_SIZES[values[key]],selected=pair===jumpPair;
        return ['−','+'].map((sign,half)=>el('button',{type:'button',class:'dv-pad',dataset:{assigned:selected?'1':'0'},'aria-pressed':selected?'true':'false','aria-label':'Pair '+String.fromCharCode(65+pair*2+half)+' '+(half?'forward':'backward')+' '+size,
          onclick:()=>{jumpPair=pair;focus={kind:'field',id:key};render();}},el('strong',{text:String.fromCharCode(65+(pair%4)*2+half)}),el('span',{text:sign+' '+size})));
      })),
      el('div',{class:'dv-row dv-cycle',dataset:{span:2}},
        el('button',{type:'button',class:'dv-arrow','aria-label':'Smaller jump',onclick:()=>set(sizeKey,Math.max(0,index-1))},'‹'),
        el('span',{class:'dv-cycle-body'},el('span',{class:'dv-row-label',text:'PAIR '+String.fromCharCode(65+(jumpPair%4)*2)+'/'+String.fromCharCode(66+(jumpPair%4)*2)+' · PAGE '+(pageTwo?2:1)}),el('span',{class:'dv-cycle-value',text:fields.get(sizeKey).choices[index].label.toUpperCase()})),
        el('button',{type:'button',class:'dv-arrow','aria-label':'Larger jump',onclick:()=>set(sizeKey,Math.min(9,index+1))},'›')));
  }
  function themes(){
    return el('div',{class:'dv-themes',dataset:{span:2},role:'radiogroup','aria-label':'Theme',onmouseenter:()=>peek('field','theme')},
      ...DEVICE_THEMES.map((theme,index)=>el('button',{type:'button',role:'radio',class:'dv-theme','aria-checked':values.theme===index?'true':'false',dataset:{value:index},
        style:'--t-bg:'+theme.bg+';--t-ink:'+theme.ink+';--t-accent:'+theme.accent,onclick:()=>{focus={kind:'field',id:'theme'};set('theme',index);}},
        el('span',{class:'dv-theme-name',text:theme.label}),el('span',{class:'dv-swatches'},...[theme.accent,...theme.stems].map(color=>el('i',{style:'background:'+color}))))));
  }
  function stemcolors(){
    const keys=['stem_drums_color','stem_harmonics_color','stem_vocals_color'];
    return el('div',{class:'dv-stemcolors',dataset:{span:2}},
      ...keys.map((key,stem)=>el('div',{class:'dv-color-row',role:'radiogroup','aria-label':fields.get(key).label,onmouseenter:()=>peek('custom','stemcolors',STEM_NAMES[stem],fields.get(key).choices[values[key]].label.toUpperCase())},
        el('span',{class:'dv-row-label',text:STEM_NAMES[stem]}),
        ...fields.get(key).choices.map(option=>el('button',{type:'button',role:'radio',class:'dv-swatch','aria-checked':values[key]===option.value?'true':'false','aria-label':option.label,
          style:'--swatch:'+(option.value?STEM_COLOR_VALUES[option.value]:(DEVICE_THEMES[values.theme]||DEVICE_THEMES[0]).stems[stem]),dataset:{value:option.value,theme:option.value?'0':'1'},
          onclick:()=>{focus={kind:'custom',id:'stemcolors',title:STEM_NAMES[stem],chip:option.label.toUpperCase()};set(key,option.value);}})))),
      el('button',{type:'button',class:'dv-row dv-link dv-reset',onclick:()=>{values={...values,stem_drums_color:0,stem_harmonics_color:0,stem_vocals_color:0};options.onChange?.(values);render();}},el('span',{class:'dv-row-label',text:'RESET STEM COLORS'})));
  }
  function card(title,tagline,target){
    const games=target==='games';
    return el('button',{type:'button',class:'dv-card',dataset:{span:1,target},onmouseenter:()=>peek('custom',target,title,games?'STEP 3':'OPEN'),
      onclick:()=>{if(games)options.onGames?.();else go(target);}},el('span',{class:'dv-card-icon','aria-hidden':'true'}),el('span',{class:'dv-card-text'},el('strong',{text:title}),el('small',{text:tagline})),el('span',{class:'dv-chevron','aria-hidden':'true'}));
  }
  // Hover and focus preview a control in the dock without changing anything.
  function peek(kind,id,title,chip){
    const next=kind==='field'?{kind,id}:kind==='link'?{kind,id,label:title}:kind==='custom'?{kind,id,title,chip}:{kind,id};
    if(JSON.stringify(next)===JSON.stringify(focus))return;focus=next;const old=root.querySelector('.dv-dock');if(old)old.replaceWith(dock());
  }
  function content(){
    const spec=PAGES[page],rows=[];
    for(const row of spec.rows){
      const [kind,a,b,c]=row;
      if(kind==='toggle')rows.push(toggle(a));else if(kind==='link')rows.push(link(a,b));else if(kind==='cycle')rows.push(cycle(a,b));else if(kind==='choice')rows.push(choice(a,b));
      else if(kind==='pads')rows.push(pads());else if(kind==='jumppads')rows.push(jumppads());else if(kind==='themes')rows.push(themes());else if(kind==='stemcolors')rows.push(stemcolors());
      else if(kind==='card')rows.push(card(a,b,c));
    }
    return el('div',{class:'dv-content',dataset:{page}},
      spec.tabs?el('div',{class:'dv-tabs',role:'tablist'},...spec.tabs.map(([id,text])=>el('button',{type:'button',role:'tab',class:'dv-tab','aria-selected':id===page?'true':'false',onclick:()=>{if(id!==page){page=id;focus={kind:'page',id};render();}}},text))):null,
      el('div',{class:'dv-grid'},...rows));
  }
  function render(){
    const theme=DEVICE_THEMES[values.theme]||DEVICE_THEMES[0];
    root.style.cssText='--dv-bg:'+theme.bg+';--dv-ink:'+theme.ink+';--dv-accent:'+theme.accent+';--dv-alarm:'+theme.alarm;
    root.dataset.theme=theme.label;root.dataset.page=page;
    root.replaceChildren(
      el('div',{class:'dv-header'},
        el('button',{type:'button',class:'dv-back',disabled:history.length?null:true,onclick:back},el('span',{class:'dv-chevron dv-chevron-left','aria-hidden':'true'}),'BACK'),
        el('div',{class:'dv-title'},el('small',{text:'XZ MODS'}),el('strong',{text:PAGES[page].title})),
        el('span',{class:'dv-usb',text:options.usbLabel?'USB · '+options.usbLabel:'xzmods.com'})),
      el('nav',{class:'dv-rail','aria-label':'MODS sections'},...RAIL.map(([id,text])=>el('button',{type:'button',class:'dv-rail-tab','aria-current':ROOT[page]===id?'page':null,onclick:()=>{history=[];page=id;focus={kind:'page',id};render();}},
        el('span',{class:'dv-icon',dataset:{icon:id},'aria-hidden':'true'}),text))),
      content(),dock());
  }
  render();
  return {el:root,render,setValues(next){values={...next};render();},get values(){return values;}};
}
window.createSettingsMenu=createSettingsMenu;
window.SETTINGS_MENU_PAGES=PAGES;
window.SETTINGS_MENU_LABEL={...LABEL,stem_drums_color:'DRUMS COLOR',stem_harmonics_color:'HARMONICS COLOR',stem_vocals_color:'VOCALS COLOR',jump_1:'JUMP A/B',jump_2:'JUMP C/D',jump_3:'JUMP E/F',jump_4:'JUMP G/H',jump_5:'JUMP A/B (2)',jump_6:'JUMP C/D (2)',jump_7:'JUMP E/F (2)',jump_8:'JUMP G/H (2)'};
})();
