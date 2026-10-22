"""Replay the real build commands and reject a link-only player profile."""
import ast
from contextlib import redirect_stderr
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

MODS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(MODS))
spec=importlib.util.spec_from_file_location('xz_build_profile',MODS/'build.py')
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
TREE=ast.parse((MODS/'build.py').read_text())
MAIN=next(n for n in TREE.body if isinstance(n,ast.FunctionDef) and n.name=='main')

def execute(nodes,namespace):
    exec(compile(ast.Module(body=nodes,type_ignores=[]),'actual-build-command-expressions','exec'),namespace)

def commands(counts,proof,jog=False,sync=False,omit_object_profile=False,omit_shared_profile=False):
    captured=[]
    ns={'args':SimpleNamespace(output=MODS/'never-created-profile-output',jog_counts_per_rev=counts,
                              xp_service_proof=proof,jog_display_proof=jog,sync_observation=sync),
        'ROOT':MODS,'compiler':'mock-cc','player_defines':builder.player_defines,
        'subprocess':SimpleNamespace(run=lambda c,**kw:captured.append(list(c))),
        'destination':MODS/'never-created-profile-output/libxz.so',
        'UI_SOURCES':[],'SDK_SOURCES':[],'SHELL_SOURCES':[],'lab_objects':[]}
    for name in ['transport_object','layered_object','key_object','audio_runtime_object','mp3_object','media_object','stream_object']:
        ns[name]=MODS/'never-created-profile-output'/(name+'.o')
    import copy
    body=copy.deepcopy(MAIN.body)
    player=next(i for i,n in enumerate(body) if isinstance(n,ast.Assign) and
                any(isinstance(t,ast.Name) and t.id=='player_object' for t in n.targets))
    selected=body[player:player+2]
    if omit_object_profile:
        literal=selected[1].value.args[0]
        literal.elts=[e for e in literal.elts if not isinstance(e,ast.Starred)]
    execute(selected,ns)
    link=next(i for i,n in enumerate(body) if isinstance(n,ast.Assign) and
              any(isinstance(t,ast.Name) and t.id=='command' for t in n.targets))
    end=next(i for i in range(link,len(body)) if isinstance(body[i],ast.Expr) and
             isinstance(body[i].value,ast.Call) and ast.unparse(body[i].value.func)=='subprocess.run')
    selected=body[link:end+1]
    if omit_shared_profile:
        selected=[n for n in selected if not (isinstance(n,ast.Assign) and
                  any(isinstance(t,ast.Subscript) and isinstance(t.value,ast.Name) and t.value.id=='command' for t in n.targets))]
    execute(selected,ns)
    assert len(captured)==2
    return captured

def qualified(captured,counts,proof):
    for command in captured:
        if command.count('-DXZ_XP_JOG_TICKS_PER_REV='+str(counts))!=1:return False
        if command.count('-DXZ_XP_SERVICE_PROOF=1')!=int(proof):return False
    return True

class PlayerProfileTests(unittest.TestCase):
    def test_actual_commands_share_requested_profile(self):
        for counts in [6600,7777]:
            for proof in [False,True]:
                for jog in [False,True]:
                    for sync in [False,True]:
                        with self.subTest(counts=counts,proof=proof,jog=jog,sync=sync):
                            captured=commands(counts,proof,jog,sync)
                            self.assertTrue(qualified(captured,counts,proof))
                            self.assertEqual(sum(str(x).endswith('extra_player.c') for x in captured[0]),1)
                            self.assertFalse(any(str(x).endswith('extra_player.c') for x in captured[1]))
                            self.assertEqual(sum(str(x).endswith('extra-player-vfp.o') for x in captured[1]),1)
                            self.assertEqual(captured[1].count('-DXZ_SYNC_OBSERVATION=1'),int(sync))
                            self.assertEqual(captured[1].count('-DXZ_JOG_DISPLAY_PROOF'),int(jog))

    def test_link_only_and_object_only_mutants_are_rejected(self):
        self.assertFalse(qualified(commands(6600,True,omit_object_profile=True),6600,True))
        self.assertFalse(qualified(commands(6600,True,omit_shared_profile=True),6600,True))

    def test_cli_proof_is_opt_in(self):
        limit=next(i for i,n in enumerate(MAIN.body) if isinstance(n,ast.If) and
                   '100 <= args.jog_counts_per_rev <= 100000' in ast.unparse(n.test))
        fn=ast.FunctionDef(name='parse_only',args=ast.arguments(posonlyargs=[],args=[],kwonlyargs=[],kw_defaults=[],defaults=[]),
             body=MAIN.body[:limit+1]+[ast.Return(ast.Name(id='args',ctx=ast.Load()))],decorator_list=[])
        ns=dict(vars(builder));execute([ast.fix_missing_locations(fn)],ns)
        for flag in [False,True]:
            argv=['build.py','--zig','mock-cc','--output','not-created']+(['--xp-service-proof','--sync-observation','--jog-display-proof'] if flag else [])
            with patch.object(sys,'argv',argv),redirect_stderr(io.StringIO()):
                args=ns['parse_only']()
            self.assertEqual(args.xp_service_proof,flag)
            self.assertEqual(args.sync_observation,flag)
            self.assertEqual(args.jog_display_proof,flag)
            self.assertEqual(args.jog_counts_per_rev,6600)

    def test_final_export_must_match_profile_and_reader(self):
        with tempfile.TemporaryDirectory(prefix='xz-profile-') as folder:
            binary=Path(folder)/'own-mock.so';binary.write_bytes(b'fixture')
            for name,reader,size in [
                ('xz_mods_xp_service_proof_v1','read_xp_service_proof.py',13360),
                ('xz_mods_sync_observation_v1','read_sync_observation.py',2260),
                ('xz_mods_jog_latch_v2','read_jog_display_latch.py',4184)]:
                def symbol(kind='STT_OBJECT',length=size,bind='STB_GLOBAL',visibility='STV_DEFAULT'):
                    return {'st_info':{'type':kind,'bind':bind},'st_other':{'visibility':visibility},'st_size':length}
                for enabled,symbols,ok in [
                    (False,None,True),(False,[symbol()],False),(True,None,False),
                    (True,[symbol(kind='STT_FUNC')],False),(True,[symbol(length=4)],False),
                    (True,[symbol(bind='STB_LOCAL')],False),(True,[symbol(visibility='STV_HIDDEN')],False),
                    (True,[symbol(),symbol()],False),(True,[symbol()],True)]:
                    table=SimpleNamespace(get_symbol_by_name=lambda _: symbols)
                    elf=SimpleNamespace(get_section_by_name=lambda _:table)
                    with self.subTest(name=name,enabled=enabled,symbols=symbols),patch.object(builder,'ELFFile',return_value=elf):
                        if ok:
                            self.assertEqual(builder.proof_export(binary,enabled,name,reader),{name:size} if enabled else {})
                        else:
                            with self.assertRaises(ValueError):builder.proof_export(binary,enabled,name,reader)

if __name__=='__main__':unittest.main()
