// The MODS header's top-right corner as the XZ draws it, for the DJ Profile preview.
(function(){
function djMonogram(name){return String(name||'').trim().split(/\s+/).filter(Boolean).slice(0,2).map(word=>[...word][0]).join('').toUpperCase();}
// The active DJ like a signed-in user: logo in a circle, name, the USB, a caret.
// With no profile the XZ shows a silhouette and TAP TO SET UP.
function profileChip({name,usb,preview,accent,onClick}){
  const avatar=el('span',{class:'dv-avatar',dataset:{empty:name?null:'1'},style:accent?'--avatar:'+accent:null},preview?el('img',{src:preview,alt:''}):name?el('span',{text:djMonogram(name)}):null);
  return el(onClick?'button':'span',{type:onClick?'button':null,class:'dv-profile',title:onClick?XZText('Open DJ Profile'):null,onclick:onClick},
    avatar,el('span',{class:'dv-profile-text'},el('strong',{text:name||XZText('TAP TO SET UP')}),usb?el('span',{class:'dv-usb',text:usb}):null),el('span',{class:'dv-caret','aria-hidden':'true'}));
}
Object.assign(window,{profileChip,djMonogram});
})();
