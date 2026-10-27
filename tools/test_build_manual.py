"""Checks build_manual.py's freshness rules on a throwaway manual: a changed, vanished or new screen, an unexplained
setting and a changed watched file each fail check, and accept clears only what was reviewed.
Run: python tools/test_build_manual.py"""
import hashlib,json,sys,tempfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parent))
import build_manual as bm
REAL_MODS=bm.MODS

def write(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(text if isinstance(text,bytes) else text.encode('utf-8'))

def quiet(fn,*args):
    with redirect_stdout(StringIO()):return fn(*args)

with tempfile.TemporaryDirectory(prefix='xz-manual-test-') as temp:
    root=Path(temp);render=root/'render';manual=root/'manual';mods=root/'mods'
    bm.APP,bm.MANUAL,bm.MODS,bm.LOCK=root,manual,mods,manual/'shots.lock.json'
    bm.STATIC={'app':manual/'app-shots','capture':manual/'captures','hw':manual/'hardware'}
    write(render/'shell/play-full.png',b'play')
    write(render/'shell/play-edge-case.png',b'edge')
    write(render/'ui/theme-0-home.png',b'home0')
    write(render/'ui/theme-24-home.png',b'home24')
    write(render/'ui/theme-0-stems.png',b'stems0')
    write(render/'ui/theme-24-stems.png',b'stems24')
    write(mods/'settings-schema.json',json.dumps({'fields':[{'key':'stems','label':'Stem audio','kind':'boolean','default':1,'min':0,'max':1}]}))
    write(root/'src/menu_key.c','table v1')
    write(manual/'manual.json',json.dumps({'title':'T','summary':'S','chapters':[{'slug':'one','group':'G','title':'One','summary':'S'}]}))
    write(manual/'coverage.json',json.dumps({'screens_not_documented':[{'glob':'modern:*-edge-case','why':'stress fixture'},{'glob':'classic:theme-24-*','why':'other theme'}],
        'watch':[{'path':'src/menu_key.c','chapter':'one','why':'MENU table'}]}))
    chapter='---\ninterface: both\nsettings: stems\n---\n## Play {modern}\n```figure modern:play-full The play screen\nCaption line.\n1 @ 10,20 Deck | The deck strip.\n```\n## Menu\n```figure classic:theme-{look}-home\n1 @ 5,5 Back | Goes back.\n```\n'
    write(manual/'chapters/one.md',chapter)

    _,chapters=bm.load_manual(render)
    figs=chapters[0]['figures']
    assert figs[0]==('figure',['modern:play-full'],'Caption line.',[('1','10','20','Deck','The deck strip.')]),figs[0]
    assert figs[1][1]==['classic:theme-24-home','classic:theme-0-home'],figs[1]
    print('ok figures parse callouts and caption, and {look} names both interfaces\' screens')

    found=bm.problems(render)
    assert any('play-full changed since' in p for p in found) and any('theme-0-home changed' in p for p in found),found
    assert any('MODS page stems: no chapter shows it' in p for p in found),found
    assert any('new screen classic:theme-0-stems' in p for p in found),found
    assert not any('edge-case' in p or 'theme-24-stems' in p for p in found),found
    assert any('src/menu_key.c changed' in p for p in found),found
    print('ok an unreviewed manual is stale, and excluded screens are not reported')

    write(manual/'chapters/one.md',chapter+'```figure classic:theme-{look}-stems\nStems.\n```\n')
    quiet(bm.accept,render,set())
    assert bm.problems(render)==[],bm.problems(render)
    print('ok accept records the reviewed screens and watched files')

    write(render/'shell/play-full.png',b'play v2')
    write(root/'src/menu_key.c','table v2')
    found=bm.problems(render)
    assert len(found)==2 and 'play-full changed' in found[0] and 'menu_key.c changed' in found[1],found
    quiet(bm.accept,render,{'modern:play-full'})
    assert [p for p in bm.problems(render)]==[next(p for p in found if 'menu_key' in p)],bm.problems(render)
    print('ok a changed screen or watched file fails until that item alone is accepted')

    write(render/'shell/play-new.png',b'new')
    (render/'shell/play-full.png').unlink()
    found=bm.problems(render)
    assert any('play-full no longer renders' in p for p in found) and any('new screen modern:play-new' in p for p in found),found
    print('ok a vanished screen and a new screen both fail')

    write(mods/'settings-schema.json',json.dumps({'fields':[{'key':'stems','label':'Stem audio'},{'key':'gate','label':'Gate cue'}]}))
    assert any('setting gate (Gate cue)' in p for p in bm.problems(render)),bm.problems(render)
    print('ok a setting no chapter explains fails')

bm.BASE='/manual/'
html,toc=bm.blocks('## Stems {classic}\nPress [[MENU]] {experimental}, see [pads](hardware#pads).\n1. One\n2. Two\n',{'figure':None})
assert '<h2 id="stems" data-iface="classic">' in html and '<kbd>MENU</kbd>' in html and 'tag--experimental' in html,html
assert 'href="/manual/hardware#pads"' in html and '<ol class="steps">' in html and toc[0][3]=='classic',html
print('ok markdown: interface headings, keys, tags, chapter links and steps')

bm.MODS=REAL_MODS
table=bm.menu_key_table()
assert table.count('\n')>=14 and '| The play screen | Opens MODS' in table,table
print('ok the MENU table is read from ui/menu_key.c')
