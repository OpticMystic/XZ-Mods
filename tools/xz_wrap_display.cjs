const fs=require('node:fs'); const acorn=require('./vendor/acorn.cjs');
for(const file of process.argv.slice(2)) {
 const source=fs.readFileSync(file,'utf8'); const ast=acorn.parse(source,{ecmaVersion:'latest'}); const edits=[];
 function walk(n,active=false,parent=null) {
  if(!n || typeof n!=='object') return;
  if(n.type==='AssignmentExpression') {
   const prop=n.left?.property;
   const display=n.left?.type==='MemberExpression' && ['textContent','innerText'].includes(prop?.name || prop?.value);
   walk(n.left,false,n);walk(n.right,display,n);return;
  }
  if(n.type==='TaggedTemplateExpression' && n.tag?.name==='XZTemplate') return;
  if(n.type==='ConditionalExpression') { walk(n.test,false,n);walk(n.consequent,active,n);walk(n.alternate,active,n);return; }
  if(n.type==='MemberExpression') return;
  if(n.type==='BinaryExpression' && n.operator!=='+') return;
  if(n.type==='Property') { walk(n.value, ['text','title','badge','note'].includes(n.key?.name),n);return; }
  if(n.type==='CallExpression') {
   if(['XZText','XZTemplate'].includes(n.callee?.name)) return;
   if(['FunctionExpression','ArrowFunctionExpression'].includes(n.callee?.type)) walk(n.callee,false,n);
   n.arguments.forEach((a,i)=>walk(a,(n.callee?.name==='node' && i===1) || (['el','h','svgEl'].includes(n.callee?.name) && i>=2) || (['plainButton','next'].includes(n.callee?.name) && i===0),n));return;
  }
  if(active && n.type==='TemplateLiteral') { edits.push([n.start,n.start,'XZTemplate']); return; }
  if(active && n.type==='Literal' && typeof n.value==='string' && /[A-Za-z]/.test(n.value) && (/[A-Za-z][ .!?…][A-Za-z ]/.test(n.value)||/^[A-Z][a-z]+$/.test(n.value)))
   edits.push([n.start,n.end,'XZText('+source.slice(n.start,n.end)+')']);
  for(const [key,child] of Object.entries(n)) {
   if(key==='loc')continue;
   if(Array.isArray(child))child.forEach(x=>walk(x,active,n));
   else if(child && typeof child==='object')walk(child,active,n);
  }
 }
 walk(ast); let result=source;
 for(const [start,end,text] of edits.sort((a,b)=>b[0]-a[0])) result=result.slice(0,start)+text+result.slice(end);
 fs.writeFileSync(file,result); console.log(file+': '+edits.length+' authored display literals');
}
