// XDJ-XZ button glyphs in the app's own text, the same pictures as the manual's [[NAME]] (ui/assets/buttons, drawn by
// tools/xz_buttons.py; names and files in ui/assets/buttons/index.js). xzKey('SHIFT') is one glyph with its name kept
// as text for screen readers and copying; xzText('Press [[SHIFT]] twice') is that sentence as nodes for el(...).
// NAME:off draws the lamp off. A name with no glyph is a programming error: it shows as plain text and logs.
'use strict';
(() => {
  const B = window.XZ_BUTTONS;
  function xzKey(name) {
    const [base, state] = name.split(':').map(part => part.trim());
    const id = B.names[base.toUpperCase()];
    const span = document.createElement('span');
    if (!id) { console.error('xzKey: no glyph for ' + name); span.textContent = base; return span; }
    const files = B.glyphs[id].files, [file, width] = state === 'off' && files.off ? files.off : files.lit;
    const img = document.createElement('img'), text = document.createElement('span');
    span.className = 'xzb'; span.dataset.key = id;
    img.className = 'xzb-g'; img.src = 'assets/buttons/' + file; img.alt = ''; img.width = Math.round(width); img.height = B.height;
    text.className = 'xzb-n'; text.textContent = base;
    span.append(img, text);
    return span;
  }
  function xzText(text) {
    return String(text).split(/(\[\[[^\]]+\]\])/).filter(Boolean).map(part => /^\[\[.+\]\]$/.test(part) ? xzKey(part.slice(2, -2)) : part);
  }
  // Plain text of a string with [[NAME]] keys, for matching and for attributes that cannot hold a picture.
  const xzPlain = text => String(text).replace(/\[\[([^\]:]+)(?::\w+)?\]\]/g, '$1');
  Object.assign(window, { xzKey, xzText, xzPlain });
})();
