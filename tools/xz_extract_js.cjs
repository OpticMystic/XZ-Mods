/* Acorn is MIT licensed; see vendor/acorn.LICENSE. No application code executes. */
const fs = require('node:fs');
const acorn = require('./vendor/acorn.cjs');
const out = [];
for (const file of process.argv.slice(2)) {
  const ast = acorn.parse(fs.readFileSync(file, 'utf8'), {ecmaVersion: 'latest', locations: true});
  function walk(node, parent) {
    if (!node || typeof node !== 'object') return;
    if (node.type === 'Literal' && typeof node.value === 'string' &&
        !(parent?.type === 'Property' && parent.key === node && !parent.computed)) {
      const text = node.value;
      if (/[A-Za-z]/.test(text) && (/[A-Za-z][ .!?…][A-Za-z ]/.test(text) || /^[A-Z][a-z]+$/.test(text)))
        out.push({file, text, line: node.loc.start.line});
    }
    if (node.type === 'TemplateLiteral') {
      const text = node.quasis.map((q,i) => q.value.cooked + (i < node.expressions.length ? '{xz'+i+'}' : '')).join('');
      if (/[A-Za-z]/.test(text)) out.push({file, text, line: node.loc.start.line});
    }
    for (const [key, child] of Object.entries(node)) {
      if (key === 'loc') continue;
      if (Array.isArray(child)) child.forEach(x=>walk(x,node));
      else if (child && typeof child === 'object') walk(child,node);
    }
  }
  walk(ast, null);
}
console.log(JSON.stringify(out));
