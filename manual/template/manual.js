// XZ Mods manual: interface filter, callout linking, lightbox, search and the on-this-page tracker. No dependencies.
(() => {
  const $ = (s, el = document) => el.querySelector(s)
  const $$ = (s, el = document) => [...el.querySelectorAll(s)]
  const base = document.body.dataset.base
  const root = document.documentElement

  function setIface(iface) {
    root.dataset.iface = iface
    try { localStorage.setItem('xz-manual-iface', iface) } catch {}
    for (const b of $$('[data-iface-set]')) b.setAttribute('aria-checked', String(b.dataset.ifaceSet === iface))
  }
  for (const b of $$('[data-iface-set]')) b.addEventListener('click', () => setIface(b.dataset.ifaceSet))
  // A link to a section that only exists in the other interface switches to that interface.
  const target = location.hash && document.getElementById(decodeURIComponent(location.hash.slice(1)))
  const owner = target && target.closest('[data-iface]')
  setIface(owner ? owner.dataset.iface : root.dataset.iface)

  const menu = $('.top__menu'), sidebar = $('#sidebar')
  menu.addEventListener('click', () => {
    const open = sidebar.classList.toggle('is-open')
    menu.setAttribute('aria-expanded', String(open))
  })
  sidebar.addEventListener('click', e => { if (e.target.closest('a')) { sidebar.classList.remove('is-open'); menu.setAttribute('aria-expanded', 'false') } })

  for (const figure of $$('.figure')) {
    const screen = $('.device__screen', figure)
    const focus = n => {
      screen.classList.toggle('has-focus', n != null)
      for (const el of $$('[data-n]', figure)) el.classList.toggle('is-on', el.dataset.n === n)
    }
    for (const el of $$('.legend li', figure)) {
      el.addEventListener('mouseenter', () => focus(el.dataset.n))
      el.addEventListener('mouseleave', () => focus(null))
    }
    for (const mark of $$('.mark', figure)) {
      mark.addEventListener('mouseenter', () => focus(mark.dataset.n))
      mark.addEventListener('mouseleave', () => focus(null))
      mark.addEventListener('focus', () => focus(mark.dataset.n))
      mark.addEventListener('blur', () => focus(null))
      mark.addEventListener('click', () => {
        const row = $(`.legend li[data-n="${mark.dataset.n}"]`, figure)
        if (!row) return
        row.scrollIntoView({ block: 'nearest', behavior: 'smooth' })
        focus(mark.dataset.n)
      })
    }
  }

  const lightbox = $('.lightbox')
  for (const b of $$('[data-zoom]')) b.addEventListener('click', () => {
    const img = $('img', lightbox)
    img.src = b.dataset.zoom
    img.width = b.dataset.w * 2; img.height = b.dataset.h * 2
    img.alt = (b.closest('figure')?.querySelector('img')?.alt) || ''
    lightbox.showModal()
  })
  lightbox.addEventListener('click', () => lightbox.close())

  const dialog = $('dialog.search'), input = $('input', dialog), results = $('.search__results', dialog), empty = $('.search__empty', dialog)
  let index = null, selected = 0
  const open = () => {
    if (!index) index = fetch(base + 'search.json').then(r => r.json())
    dialog.showModal(); input.select(); run()
  }
  for (const b of $$('[data-open-search]')) b.addEventListener('click', open)
  document.addEventListener('keydown', e => {
    if (dialog.open) return
    const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement?.tagName)
    if ((e.key === '/' && !typing) || (e.key === 'k' && (e.metaKey || e.ctrlKey))) { e.preventDefault(); open() }
  })
  const escape = s => s.replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c])
  const highlight = (text, terms) => escape(text).replace(new RegExp(`(${terms.map(t => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|')})`, 'gi'), '<mark>$1</mark>')
  async function run() {
    const terms = input.value.toLowerCase().split(/\s+/).filter(Boolean)
    const all = await index
    let hits = []
    if (terms.length) for (const entry of all) {
      const title = entry.t.toLowerCase(), text = (entry.x || '').toLowerCase()
      if (!terms.every(t => title.includes(t) || text.includes(t) || entry.c.toLowerCase().includes(t))) continue
      const score = terms.reduce((s, t) => s + (title.startsWith(t) ? 8 : title.includes(t) ? 5 : 0) + (text.includes(t) ? 1 : 0), entry.x ? 0 : 1)
      let snippet = ''
      if (entry.x) {
        const at = text.indexOf(terms[0])
        if (at >= 0) snippet = (at > 50 ? '…' : '') + entry.x.slice(Math.max(0, at - 50), at + 110) + '…'
      }
      hits.push({ entry, score, snippet })
    }
    hits = hits.sort((a, b) => b.score - a.score).slice(0, 30)
    selected = 0
    results.innerHTML = hits.map(({ entry, snippet }, i) =>
      `<li role="option" aria-selected="${i === 0}"><a href="${entry.u}">${highlight(entry.t, terms)}<span>${escape(entry.c)}</span>${snippet ? `<q>${highlight(snippet, terms)}</q>` : ''}</a></li>`).join('')
    empty.hidden = !terms.length || hits.length > 0
  }
  input.addEventListener('input', run)
  input.addEventListener('keydown', e => {
    const items = $$('li', results)
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault()
      selected = (selected + (e.key === 'ArrowDown' ? 1 : -1) + items.length) % Math.max(items.length, 1)
      items.forEach((li, i) => li.setAttribute('aria-selected', String(i === selected)))
      items[selected]?.scrollIntoView({ block: 'nearest' })
    } else if (e.key === 'Enter' && items[selected]) { e.preventDefault(); location.href = $('a', items[selected]).href; dialog.close() }
  })
  results.addEventListener('click', e => { if (e.target.closest('a')) dialog.close() })

  const heads = $$('.content h2[id], .content h3[id]')
  if (heads.length && 'IntersectionObserver' in window) {
    const visible = new Set()
    const mark = () => {
      const current = heads.find(h => visible.has(h)) || null
      for (const a of $$('.toc a, .nav__sections a')) a.classList.toggle('is-active', !!current && a.getAttribute('href') === '#' + current.id)
    }
    const io = new IntersectionObserver(entries => {
      for (const e of entries) e.isIntersecting ? visible.add(e.target) : visible.delete(e.target)
      mark()
    }, { rootMargin: '-70px 0px -65% 0px' })
    heads.forEach(h => io.observe(h))
  }
})()
