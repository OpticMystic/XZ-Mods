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
    # A one-screen menu map, captured from src/screen.c as it is now.
    write(root/'src/screen.c','draw v1')
    def tree(chapter='one#menu',source=b'draw v1'):
        return {'format':'xz-menu-tree/1','interfaces':['modern','classic'],'roots':[{'id':'m-1','label':'Play screen'}],
                'nodes':[{'id':'m-1','interface':'modern','title':'HOME','path':['MODS','HOME'],'kind':'page','image':'nodes/m-1.webp','focusables':[],'chapter':chapter}],
                'edges':[],'sources':{'src/screen.c':hashlib.sha256(source).hexdigest()}}
    write(manual/'menu-tree/menu-tree.json',json.dumps(tree()))
    write(manual/'menu-tree/nodes/m-1.webp',b'webp')

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
    write(mods/'settings-schema.json',json.dumps({'fields':[{'key':'stems','label':'Stem audio','kind':'boolean','default':1,'min':0,'max':1}]}))

    menu=lambda:[p for p in bm.problems(render) if p.startswith('menu map')]
    assert menu()==[],menu()
    write(root/'src/screen.c','draw v2')
    assert len(menu())==1 and 'src/screen.c' in menu()[0] and 'changed since its screens were captured' in menu()[0],menu()
    (root/'src/screen.c').unlink()
    assert len(menu())==1,menu()
    print('ok the menu map is stale when a firmware source it was captured from changes or goes')

    incoming=root/'crawl'
    write(incoming/'menu-tree.json',json.dumps(tree('one#no-such-heading',b'draw v3')))
    write(incoming/'nodes/m-1.webp',b'webp v3')
    assert quiet(bm.import_menu_tree,incoming)==1 and json.loads((manual/'menu-tree/menu-tree.json').read_text())==tree(),'a bad tree is refused and the old one kept'
    write(incoming/'menu-tree.json',json.dumps(tree('one',b'draw v3')))
    write(root/'src/screen.c','draw v3')
    assert quiet(bm.import_menu_tree,incoming)==0 and (manual/'menu-tree/nodes/m-1.webp').read_bytes()==b'webp v3' and menu()==[],menu()
    (manual/'menu-tree/menu-tree.json').unlink()
    assert len(menu())==1 and 'is missing' in menu()[0],menu()
    print('ok menu-tree refuses a tree that links to a missing heading, imports a good one, and a missing tree fails')

bm.BASE='/manual/'
html,toc=bm.blocks('## Stems {classic}\nPress [[MENU]] {experimental}, see [pads](hardware#pads).\n1. One\n2. Two\n',{'figure':None})
assert '<h2 id="stems" data-iface="classic">' in html and 'tag--experimental' in html,html
assert '<span class="xzb" data-key="menu"><img class="xzb-g" src="/manual/buttons/menu.svg" alt=""' in html and '<span class="xzb-n">MENU</span>' in html,html
assert 'href="/manual/hardware#pads"' in html and '<ol class="steps">' in html and toc[0][3]=='classic',html
print('ok markdown: interface headings, key glyphs with their names as text, tags, chapter links and steps')
key=bm.inline('[[SYNC:off]], [[LOOP IN]] and [[NO SUCH KEY]]')
assert 'src="/manual/buttons/sync.off.svg"' in key and 'data-key="in-cue"' in key and '<span class="xzb-n">LOOP IN</span>' in key and '<kbd>NO SUCH KEY</kbd>' in key,key
found=bm.key_problems([{'slug':'one','body':'Press [[SHIFT]] and [[PLAYLIST]].'}],{'title':'[[MENU]]'})
assert [f for f in found if 'has no button glyph' in f]==['one: [[PLAYLIST]] has no button glyph. Use the name printed on the player, or add the glyph or an alias in tools/xz_buttons.py'],found
print('ok keys: a lamp can be drawn off, aliases name the printed control, and check fails on a name with no glyph')
found=bm.key_problems([{'slug':'one','body':'Press [[rekordbox]], [[CDJ]] or [[BUTTON ABOVE LINK]].'}],{})
assert [f for f in found if 'brand name' in f]==['one: [[CDJ]] is a brand name. Name the control by what it is, for example [[BUTTON ABOVE LINK]]',
    'one: [[rekordbox]] is a brand name. Name the control by what it is, for example [[BUTTON ABOVE LINK]]'],found
assert 'data-key="above-link"' in bm.inline('[[the button above LINK]]') and not any('rekordbox' in k.lower() for k in bm.xz_buttons.by_name())
import check_trademarks as tm
assert tm.find('Export it from Rekordbox') and tm.find('a CDJ-3000') and tm.find('Pioneer DJ') and tm.find('CDJs') and not tm.find('XDJ-XZ')
assert not tm.find('rekordbox_press=2') and not tm.find('PIONEER/rekordbox/export.pdb') and not tm.find('CDJMODS/stems')
assert tm.find('REKORDBOX BUTTON') and tm.find('the rekordbox library')
assert [s for _, s in tm.code_strings("// rekordbox's comment\nconst a='Open rekordbox', b=\"x\"; /* Serato */")]==['Open rekordbox','x']
assert [s for _, s in tm.c_strings('xz_log("REKORDBOX hook");\n_Static_assert(1&&\n 1,"one REKORDBOX numbering");\nlabel(c,"REKORDBOX BUTTON");')]==['REKORDBOX BUTTON']
assert [s for _, s in tm.html_text('<p id="repair-rekordbox-block" title="Rekordbox">Close Rekordbox</p>')]==['Rekordbox','Close Rekordbox']
assert [s for _, s in tm.py_strings('"""Rekordbox docstring."""\nkey="rekordbox"\nraise ValueError("Close Rekordbox first")\n')]==['"Close Rekordbox first"']
print('ok trademarks: [[...]] refuses a brand name, glyphs carry none, and the guard reads only what a user reads')

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
    assert 'src=\\"assets/buttons/shift.svg\\"' in raw and bm.BUTTON_SRC=='/manual/buttons/',raw[:400]
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

# Navigation videos (tools/build_nav_videos.py): the steps a timeline turns into, the close-up layout, the nav block's
# HTML, and the freshness rules check applies.
import build_nav_videos as nv
def ev(t,control,action,detail='',point=None,effects=()):
    return {'t_ms':t,'control':control,'action':action,'detail':detail,'point':point,'effects':list(effects)}
timeline=[ev(0,'clock','redraw'),ev(500,'BROWSE','press'),ev(620,'BROWSE','release'),
          *[ev(1000+200*k,'ROTARY SELECTOR','turn','+1 detent') for k in range(3)],ev(1600,'ROTARY SELECTOR','push','SCREEN'),ev(1720,'ROTARY SELECTOR','release'),
          ev(2000,'touch screen','press','keyboard A',[10,20]),ev(2120,'touch screen','release','keyboard A',[10,20]),
          ev(2500,'touch screen','press','keyboard SHIFT',[5,5]),ev(2620,'touch screen','release','keyboard SHIFT',[5,5]),
          ev(3000,'touch screen','press','keyboard b',[30,20]),ev(3120,'touch screen','release','keyboard b',[30,20]),
          ev(3500,'left deck SHIFT','press'),ev(3620,'left deck SYNC','press','SYNC'),ev(3740,'left deck SYNC','release','SYNC'),ev(3900,'left deck SHIFT','release'),
          ev(4500,'right deck SHIFT','press'),ev(4620,'right deck SHIFT','release'),ev(4800,'right deck SHIFT','press'),ev(4920,'right deck SHIFT','release'),
          ev(5500,'MENU','press'),ev(6500,'MENU','hold'),ev(6620,'MENU','release'),
          ev(7000,'button above LINK','press'),ev(7120,'button above LINK','release'),
          ev(7500,'touch screen','press','CLASSIC',[200,140]),ev(7620,'touch screen','release','CLASSIC',[200,140]),
          ev(8000,'PLAY','press','',None,[{'host':'software-player','deck':3}])]
manifest={'controls':{c:{'timeline_control':[s]} for c,s in [('browse','BROWSE'),('rotary-selector','ROTARY SELECTOR'),('touch-screen','touch screen'),('left-shift','left deck SHIFT'),
          ('left-sync','left deck SYNC'),('right-shift','right deck SHIFT'),('menu','MENU'),('rekordbox-button-above-link','button above LINK'),('right-play-pause','right deck PLAY/PAUSE')]}}
gs=nv.assign_clips(nv.gestures(timeline),manifest)
assert [g.clip for g in gs if g.kind=='turn']==['rotary-selector']*3 and gs[-1].clip=='right-play-pause' and gs[-1].up==8120
assert [t for t,_ in nv.steps(gs)]==[500,1000,2000,3500,4500,5500,7000,7500,8000]
assert [s for _,s in nv.steps(gs)]==['Press [[BROWSE]]','Turn [[ROTARY SELECTOR]] to **SCREEN** and push it','Type **Ab** on the screen keyboard',
    'Hold [[SHIFT]] on the left deck and press [[SYNC]] on the left deck','Press [[SHIFT]] on the right deck twice quickly','Hold [[MENU]] for a second',
    'Press the button above [[LINK]]','Tap **CLASSIC**','Press [[PLAY/PAUSE]] on the right deck'],nv.steps(gs)
assert not any('rekordbox' in s.lower() for _,s in nv.steps(gs)),'no maker name in a caption'
lays=dict((tuple(l),t) for t,l in nv.layouts(gs))
assert ('left-shift','left-sync') in lays and lays[('left-shift','left-sync')]<=3500-nv.FADE_MS,'SHIFT held with SYNC splits the close-up, faded in before the press'
print('ok nav: a timeline becomes steps in key notation (turns to a label, typing, SHIFT combos, double SHIFT, holds, taps) and the SHIFT close-up splits')

with tempfile.TemporaryDirectory(prefix='xz-nav-test-') as temp:
    root=Path(temp);saved=nv.capture_inputs
    nv.capture_inputs=lambda:{'mods/a.c':'1'}
    try:
        write(root/'manual/nav/f.json','{"interface":"MODERN","steps":[]}')
        write(root/'ui/assets/manual/nav/f.mp4',b'video');write(root/'ui/assets/manual/nav/f.webp',b'poster')
        entry={'video':'assets/manual/nav/f.mp4','poster':'assets/manual/nav/f.webp','w':1280,'h':480,'seconds':2.0,'interface':'modern',
               'steps':[{'t_ms':500,'text':'Press [[MENU]]'}],'sha256':{'video':nv.sha256(root/'ui/assets/manual/nav/f.mp4'),'poster':nv.sha256(root/'ui/assets/manual/nav/f.webp'),
               'script':nv.text_sha256(root/'manual/nav/f.json')}}
        media={'format':nv.FORMAT,'capture_inputs_sha256':nv.digest({'mods/a.c':'1'}),'compositor_sha256':nv.text_sha256(Path(nv.__file__)),'flows':{'f':entry}}
        write(root/'manual/nav-media.json',json.dumps(media))
        flows={'f':['one']}
        assert nv.problems(flows,root)==[],nv.problems(flows,root)
        assert nv.chapter_flows([('one','## A\n```nav f Title | First\ng | Second\n```\n')],root)=={'f':['one'],'g':['one']}
        bm.NAV_MEDIA=root/'manual/nav-media.json'
        block=bm.nav_html('f Moving around | The keys {modern}',[],str)
        assert 'data-iface="modern"' in block and 'data-flow="f"' in block and 'data-title="The keys"' in block and 'aria-label="Moving around"' in block and 'data-t="500"' in block,block
        assert '<video' in bm.nav_html('f T',[],str,True) and '<video' not in block,'only the website gets a plain video element'
        write(root/'ui/assets/manual/nav/f.mp4',b'other')
        assert any('is not the file' in p for p in nv.problems(flows,root))
        write(root/'ui/assets/manual/nav/f.mp4',b'video');write(root/'manual/nav/f.json','{"interface":"MODERN","steps":[{"push":true}]}')
        assert any('flow script changed' in p for p in nv.problems(flows,root))
        write(root/'manual/nav/f.json','{"interface":"MODERN","steps":[]}')
        nv.capture_inputs=lambda:{'mods/a.c':'2'}
        assert any('firmware sources or flow scripts changed' in p for p in nv.problems(flows,root))
        nv.capture_inputs=lambda:{'mods/a.c':'1'}
        assert any('has no video' in p for p in nv.problems({'f':['one'],'g':['one']},root))
        assert any('no chapter shows' in p for p in nv.problems({},root) or nv.problems({'g':['one']},root))
    finally:
        nv.capture_inputs=saved
print('ok nav: check fails on a changed video, flow script or firmware source, a block with no video and a video no block shows; the block keeps its flows, title and interface')
