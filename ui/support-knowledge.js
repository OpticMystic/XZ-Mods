// "Is this it?" matching for the report wizard. Runs on this computer against the bundled knowledge base;
// the reporter's text is never sent anywhere for suggestions. An entry needs one of its `need` words, none of
// its `not` words (so SYNC labels never answer a KEY SYNC question), and enough of its other words.
'use strict';
(root => {
  const normalize = text => ' ' + String(text || '').toLowerCase().replace(/[‘’`]/g, "'").replace(/[^a-z0-9'/+-]+/g, ' ').replace(/\s+/g, ' ').trim() + ' ';
  const has = (text, term) => text.includes(normalize(term));
  function match(kb, text, limit = 3) {
    const typed = normalize(text);
    if (typed.trim().length < 3) return [];
    const found = [];
    kb.entries.forEach((entry, index) => {
      if (entry.not.some(term => has(typed, term)) || !entry.need.some(term => has(typed, term))) return;
      const words = new Set([...entry.need, ...entry.terms].map(term => normalize(term)));
      let score = 0;
      for (const word of words) if (typed.includes(word)) score += word.trim().includes(' ') ? 2 : 1;
      if (score >= 2) found.push({ entry, score, index });
    });
    return found.sort((a, b) => b.score - a.score || a.index - b.index).slice(0, limit).map(item => item.entry);
  }
  root.supportKnowledge = Object.freeze({ normalize, match });
  if (typeof module !== 'undefined') module.exports = root.supportKnowledge;
})(typeof window !== 'undefined' ? window : globalThis);
