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


# The manual inside the app: the same chapters, generated into scripts and pictures, and checked for drift.
from PIL import Image
with tempfile.TemporaryDirectory(prefix='xz-manual-app-test-') as temp:
    root=Path(temp);render=root/'render';manual=root/'manual';mods=root/'mods'
    bm.APP,bm.MANUAL,bm.MODS,bm.LOCK=root,manual,mods,manual/'shots.lock.json'
    bm.STATIC={'app':manual/'app-shots','capture':manual/'captures','hw':manual/'hardware'}
    (render/'shell').mkdir(parents=True);Image.new('RGB',(800,480),(9,9,9)).save(render/'shell/play-full.png')
    write(mods/'settings-schema.json',json.dumps({'fields':[]}))
    write(manual/'manual.json',json.dumps({'title':'T','summary':'S','chapters':[{'slug':'one','group':'G','title':'One','summary':'S'},{'slug':'two','group':'G','title':'Two','summary':'S'}],
        'tour':[{'title':'Four','shot':'modern:play-full','crop':[0,0,400,240],'link':'one#setup','text':'Decks 3 and 4.'}]}))
    write(manual/'chapters/one.md','---\ninterface: both\nsettings:\n---\n## Setup {experimental}\n```explainer four-deck-setup Steps\n```\n1. Plug in.\n2. Press [[SHIFT]].\n'
          '```figure modern:play-full Play\nCaption.\n1 @ 400,240 Middle | The centre. See [two](two#end).\n```\n```explainer beat-jump Jumps\nPage 1 | Press [[BEAT JUMP]].\nPage 2 | Hold [[SHIFT]].\n```\n')
    write(manual/'chapters/two.md','## End\nBack to [one](one#setup), or [the site](https://xzmods.com/manual).\n')
    files=bm.app_manual(render,False)
    raw=files['chapters/one.js'].decode()
    assert raw.startswith('// Generated by tools/build_manual.py app') and 'style=' not in raw and '</' not in raw,'a generated script: no style attributes, no raw </'
    one=json.loads(raw.split(']=',1)[1].rstrip().rstrip(';'))
    assert '<h3 id="m-setup" data-anchor="setup">' in one,one[:300]
    assert 'data-mx="four-deck-setup" data-mode="steps"' in one and 'data-mx="beat-jump" data-mode="scenes"' in one and 'data-title="Page 1"' in one and 'data-v=' not in one,one
    assert 'data-x="50.000" data-y="50.000"' in one and 'aria-describedby="f-one-1-1"' in one and 'data-chapter="two" data-anchor="end"' in one,one
    assert r'm-url\">xzmods.com/manual<\/span>' in files['chapters/two.js'].decode()
    index=json.loads(files['index.js'].decode().split('=',1)[1].rstrip().rstrip(';'))
    tile=index['tour'][0]
    assert index['chapters'][0]['sections'][0][2]=='Setup' and tile['link']=='one#setup' and (tile['w'],tile['h'])==(400,240) and 'art' not in tile and index['draft'] is False,index
    shot=sorted((k for k in files if k.startswith('shots/')),key=lambda k:isinstance(files[k],tuple))
    assert len(shot)==2 and all(k.endswith('.webp') for k in shot) and isinstance(files[shot[0]],Path) and files[shot[1]][1]==(0,0,400,240) and tile['img']=='manual/'+shot[1],files.keys()
    assert bm.explainer_scenes([{'slug':'one','body':bm.parse_chapter(manual/'chapters/one.md')[1]}])=={'four-deck-setup':('one',2),'beat-jump':('one',2)}
    html_site,_=bm.blocks('```explainer beat-jump X\nA | b | c\n```\nText.\n',{'figure':None})
    assert html_site=='<p>Text.</p>',html_site
    print('ok app: chapters become scripts with hotspots, explainers, checked links, one hashed picture and a cropped tour tile; the site skips explainers')

    # Explainer clips: one per scene, or check fails.
    _,chapters=bm.load_manual(render)
    write(root/'ui/assets/manual/beat-jump/scene-1.mp4',b'x');write(root/'ui/assets/manual/beat-jump/scene-1.webp',b'x')
    clip={'video':'assets/manual/beat-jump/scene-1.mp4','poster':'assets/manual/beat-jump/scene-1.webp','seconds':3,'loop':False}
    assert bm.media_problems(chapters,{'explainers':{'beat-jump':{'w':8,'h':8,'scenes':[clip,clip]}}})==[]
    found=bm.media_problems(chapters,{'explainers':{'beat-jump':{'w':8,'h':8,'scenes':[clip]},'three-band':{'w':8,'h':8,'scenes':[clip]}}})
    assert any('beat-jump has 2 scenes in the chapter but 1 clips' in f for f in found) and any('three-band' in f and 'no chapter shows it' in f for f in found),found
    assert any('scene-2.mp4 is missing' in f for f in bm.media_problems(chapters,{'explainers':{'beat-jump':{'w':8,'h':8,'scenes':[clip,dict(clip,video='assets/manual/beat-jump/scene-2.mp4')]}}}))
    write(root/'ui/assets/manual/media.js',bm.MEDIA_PREFIX+json.dumps({'explainers':{'beat-jump':{'w':8,'h':8,'scenes':[clip]}}})+';\n')
    write(manual/'coverage.json',json.dumps({'screens_not_documented':[],'watch':[]}))
    assert any('but 1 clips' in f for f in bm.problems(render)),bm.problems(render)
    (root/'ui/assets/manual/media.js').unlink()
    print('ok explainer clips: a scene count that differs from the chapter, a clip for no chapter or a missing file fails check')

    out=root/'ui-manual'
    quiet(bm.app_build,render,out,False)
    assert bm.app_problems(render,out)==[],bm.app_problems(render,out)
    write(manual/'chapters/two.md','## End\nChanged.\n')
    found=bm.app_problems(render,out)
    assert any('chapters/two.js is out of date' in f for f in found),found
    Image.new('RGB',(800,480),(20,9,9)).save(render/'shell/play-full.png')
    found=bm.app_problems(render,out)
    assert any('is missing' in f and 'shots/' in f for f in found) and any('is no longer used' in f for f in found),found
    print('ok app: a changed chapter or screen makes the embedded copy stale until app runs again')

    for bad,why in (('[x](two#nowhere)','no such heading'),('```explainer nothing\nA | b\n```','unknown explainer'),('```explainer four-deck-setup\nA | b\n```','give it no lines'),
                    ('```explainer three-band\nA | b\n```','unknown explainer'),('```explainer beat-jump\nA | 1 2 | b\n```','`Title | caption`')):
        write(manual/'chapters/two.md','## End\n'+bad+'\n')
        try: bm.app_manual(render,False); raise AssertionError(bad)
        except SystemExit as error: assert why in str(error),error
    write(manual/'chapters/two.md','## End\n')
    book=json.loads((manual/'manual.json').read_text())
    for tile,why in (({'shot':'modern:play-edge'},'use a shot a chapter figure shows'),({'shot':'asset:../../manual/manual.json'},'not a picture under ui/assets'),
                     ({'shot':'modern:play-full','crop':[500,0,400,240]},'outside'),({'art':'four-decks'},'needs a shot')):
        write(manual/'manual.json',json.dumps({**book,'tour':[{'title':'Four','link':'one#setup','text':'T',**tile}]}))
        try: bm.app_manual(render,False); raise AssertionError(tile)
        except SystemExit as error: assert why in str(error),(why,error)
    print('ok app: a broken chapter link, an unknown or misused explainer, a data column, or a tour tile that is not a chapter render stops the build')
