"""Breaks one rule at a time in copies of the recorder seam and the tracklist core, writer and runtime, and checks
that the host tests then fail. The sources are never edited. Run in WSL from mods/:
    python3 tests/tracklist/mutate.py <out-dir>"""
import os, subprocess, sys
out = sys.argv[1] if len(sys.argv) > 1 else 'out/mutations'
os.makedirs(out, exist_ok=True)
def read(path): return open(path, encoding='utf-8').read()
SOURCES = {'core': 'tracklist.c', 'file': 'tracklist_file.c', 'runtime': 'tracklist_runtime.c', 'recorder': 'recorder_state.c'}
BS = chr(92)
NL = chr(10)
M = [
 ('core', 'threshold ignored', 'd->run.heard_ms<threshold(t,now))return;', '0)return;'),
 ('core', 'no dedupe of doubles', 'if(r&&r->seq==f->seq&&(uint32_t)(now-r->last_ms)<=t->cfg.repeat_ms){d->listed_seq=f->seq;r->last_ms=now;return;}', ''),
 ('core', 'no hold-back ordering', '  if(!all&&e->start_ms>earliest&&(uint32_t)(now-e->committed_at)<t->cfg.hold_max_ms)break;\n', ''),
 ('core', 'hold-back without limit', '&&(uint32_t)(now-e->committed_at)<t->cfg.hold_max_ms)break;', ')break;'),
 ('core', 'continued never marked', 'e->continued=before;', 'e->continued=0;'),
 ('core', 'no gap bridging', '(uint32_t)(now-r->last_ms)>t->cfg.gap_ms)memset', '1)memset'),
 ('core', 'on-air ignored', '(air_mode(t,now)?air==1:1)', '1'),
 ('core', 'no on-air hold', '(uint32_t)(now-d->air_at)<=t->cfg.air_hold_ms', '(uint32_t)(now-d->air_at)<=0'),
 ('core', 'no tail rule', 'd->run.heard_file_ms>=t->cfg.tail_ms', '0'),
 ('core', 'resume treated as fresh', 't->want_resume=!(edge||(rec->elapsed_known&&rec->elapsed_ms<5000u));', 't->want_resume=0;'),
 ('core', 'reload is a new entry', 'r->last_ms=now;return;}\n commit', 'r->last_ms=now;}\n commit'),
 ('core', 'text not sanitised', "out[at++]=(char)(c<0x20||c==0x7f?' ':c);", 'out[at++]=(char)c;'),
 ('core', 'failed start creates a file', '(uint32_t)(now-t->want_origin)>=1000u||', '1||'),
 ('core', 'no CUE quote escape', "for(char *p=ti;*p;p++)if(*p=='" + '"' + "')*p='" + BS + "'';", ''),
 ('core', 'at_end writes lines live', 'if(!t->cfg.at_end)line(t,XZ_TL_OP_APPEND', 'line(t,XZ_TL_OP_APPEND'),
 ('core', 'finish leaves the file open', ' close_file(t,now,footer,0);\n}\nvoid xz_tl_turn_off', '}\nvoid xz_tl_turn_off'),
 ('core', 'resumed at_end list replaces the file', 'f->resumed?XZ_TL_OP_RESUME:XZ_TL_OP_WHOLE', 'XZ_TL_OP_WHOLE'),
 ('core', 'heard time not capped per step', 'if(dt>t->cfg.step_max_ms)dt=t->cfg.step_max_ms;', ''),
 ('core', 'a recorder blip ends the list', 'if((uint32_t)(now-t->rec_lost_at)<t->cfg.rec_hold_ms){', 'if(0){'),
 ('core', 'turning off leaves the list open', 'snprintf(footer,sizeof footer,EOL "RECORDING TRACKLIST was turned off at about %s; the list ends here." EOL,clock);\n close_file(t,now,footer,0);',
  'snprintf(footer,sizeof footer,EOL "RECORDING TRACKLIST was turned off at about %s; the list ends here." EOL,clock);'),
 ('core', 'a recording running at turn-on is listed', 'if(rec->recording){t->handled=1;t->handled_generation=rec->generation;t->want_open=0;}', ''),
 ('file', 'no cut-back on a failed append', 'if(before>=0&&ftruncate(fd,before)==0)(void)do_sync(w,fd);', ''),
 ('file', 'writes without a recording', 'if(!present){error=ENOENT;goto done;}', ''),
 ('file', 'RECTMP.WAV not accepted', 'if(!present)present=!fstatat(dfd,XZ_TL_RECORDING_NAME', 'if(0)present=!fstatat(dfd,XZ_TL_RECORDING_NAME'),
 ('file', 'follows links', 'int flags=O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC;', 'int flags=O_WRONLY|O_CREAT|O_CLOEXEC;'),
 ('file', 'failed file not skipped', "if(w->failed&&w->failed_seq==op->file_seq){w->skipped++;return XZ_TL_SKIPPED;}", ''),
 ('file', 'root fs not refused', 'if(w->forbid_dev_set&&st.st_dev==w->forbid_dev){error=EXDEV;goto done;}', ''),
 ('runtime', 'channel order reversed', 'out[ch]=(value>>(8*(3-ch))&0xffu)==1u;', 'out[ch]=(value>>(8*ch)&0xffu)==1u;'),
 ('runtime', 'route ignored for DECK 3/4', 'd->on_air=!route?0:air[2+i];', 'd->on_air=air[2+i];'),
 ('runtime', 'unstable root accepted', '!word(0x1c8981c,&check)||check!=mgr||', ''),
 ('runtime', 'loading deck counted as loaded', 'd->loaded=state!=7&&state!=9&&id&&id!=UINT32_MAX;', 'd->loaded=id&&id!=UINT32_MAX;'),
 ('runtime', 'the test variable does not win', 'if(armed&&env_mode<0){', 'if(armed){'),
 ('runtime', 'the setting before start is lost', 'int mode=env_mode>=0?env_mode:wanted?MODE_AT_END:MODE_NONE;', 'int mode=env_mode>=0?env_mode:MODE_NONE;'),
 # USB STOP: the tracklist holds its stick (test_tracklist.c test_stick_stop, test_tracklist_stop.c).
 ('core', 'USB STOP does not end the list', ' close_file(t,now,footer,1);\n return 1;', ' return 1;'),
 ('core', 'the list ended by USB STOP stays open (a second footer)', ' close_file(t,now,footer,1);\n return 1;',
  ' {struct file keep=t->f;close_file(t,now,footer,1);t->f=keep;}\n return 1;'),
 ('core', 'USB STOP ends a list on the other stick', 'if(!f->open||!n||strncmp(f->wav,mount,n))return 0;', 'if(!f->open||!n)return 0;'),
 ('runtime', 'the sampler does not end the list at USB STOP', 'P.stop_lists+=(uint32_t)xz_tl_stick_stopped(core,mount,(uint32_t)(micros(CLOCK_MONOTONIC)/1000u));', ''),
 ('runtime', 'USB STOP waits for the next sample', ' xz_wake_set(&sampler_wake);\n return 1;', ' return 1;'),
 ('runtime', 'the stop write waits in the queue while recording', 'if(urgent||want||(ring_n&&!held)||!live)break;', 'if(want||(ring_n&&!held)||!live)break;'),
 ('runtime', 'the stop write waits for a quiet stick', '  if(urgent_slot())return;', '  if(0)return;'),
 ('runtime', 'the stop write at nice 19', 'int ok=!setpriority(PRIO_PROCESS,(id_t)tid,urgent?0:19);', 'int ok=!setpriority(PRIO_PROCESS,(id_t)tid,19);'),
 ('runtime', 'the stop write at idle I/O', 'io_class_of(tid,!urgent);', 'io_class_of(tid,1);'),
 ('runtime', 'the usual priority not restored after a stop', ' writer_priority(0,0);\n note_writer(w);', ' note_writer(w);'),
 ('runtime', 'no answer after a failed write', 'if(stop&&xz_usb_release_answer)xz_usb_release_answer(stop_holder,slot,stop);' + NL + ' writer_priority(0,0);',
  'if(stop&&!error&&xz_usb_release_answer)xz_usb_release_answer(stop_holder,slot,stop);' + NL + ' writer_priority(0,0);'),
 ('runtime', 'a stop answered twice', 'if(stop&&xz_usb_release_answer)xz_usb_release_answer(stop_holder,slot,stop);' + NL + ' writer_priority(0,0);',
  'if(stop&&xz_usb_release_answer)xz_usb_release_answer(stop_holder,slot,stop);if(stop)xz_usb_release_answer(stop_holder,slot,stop);' + NL + ' writer_priority(0,0);'),
 ('runtime', 'USB STOP asks a tracklist that is off', 'if(!__atomic_load_n(&running,__ATOMIC_ACQUIRE)||!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;',
  'if(!__atomic_load_n(&running,__ATOMIC_ACQUIRE))return 0;'),
 ('runtime', 'USB STOP of the other stick is answered', '__atomic_load_n(&open_slot,__ATOMIC_ACQUIRE)!=slot&&', '!__atomic_load_n(&open_slot,__ATOMIC_ACQUIRE)&&'),
 ('runtime', 'a queued list does not count at USB STOP', '&&!__atomic_load_n(&pending[slot-1],__ATOMIC_ACQUIRE))return 0;', ')return 0;'),
 ('runtime', "a USB STOP's write syncs each file", ' w->no_sync=1;\n', ' w->no_sync=0;\n'),
 ('recorder', 'START window not held', '    if (kind != PENDING_NONE) {', '    if (0) {'),
 ('recorder', 'closing window not held',
  '    else if (recording && t->seen && t->was_recording && file_no == t->file_no && written < t->written) kind = PENDING_CLOSE;', ''),
 ('recorder', 'a held window never ends', 'if ((uint32_t)(t->now_ms - t->pending_ms) < XZ126_REC_SETTLE_MS) {', 'if (1) {'),
 ('recorder', 'text read crosses a page', '        if (chunk > page_left) chunk = page_left;\n', ''),
 ('recorder', 'chain not checked again', ' ||\n        read(ctx, input + XZ126_REC_INPUT_WRITER, &again, 4) || again != writer) {', ') {'),
 ('recorder', 'other firmware retried', 'if (abi < 0) t->abi_refused = 1; ', ''),
 ('recorder', 'file number not matched', '    return n == file_no;', '    return 1;'),
 ('recorder', 'moved text accepted', '    if (read(ctx, writer + W_PATH, &again, 4) || again != text) { out[0] = 0; return 0; }\n', ''),
]
flags = ['-std=c11', '-D_GNU_SOURCE', '-O1', '-w']
bad = 0
for where, name, before, after in M:
    original = read(SOURCES[where])
    if original.count(before) != 1:
        print('MUTATION DID NOT APPLY:', name); bad += 1; continue
    folder = os.path.join(out, where)
    os.makedirs(folder, exist_ok=True)
    mutated = os.path.join(folder, SOURCES[where])
    open(mutated, 'w', encoding='utf-8').write(original.replace(before, after))
    pick = lambda kind: mutated if where == kind else SOURCES[kind]
    if where == 'recorder':
        builds = [['-DXZ_RECORDER_STATE_NO_RUNTIME', pick('recorder'), 'tests/test_recorder_state.c']]
    elif where == 'runtime':
        # The USB STOP test (real threads, a folder as the stick, about 25 s) runs only when the quick one passed.
        builds = [['-pthread', '-I', folder, '-I', '.', 'tests/tracklist/test_tracklist_runtime.c', 'tracklist.c', 'tracklist_file.c'],
                  ['-pthread', '-DXZ_USB_RELEASE_NO_RUNTIME', '-I', folder, '-I', '.', 'tests/tracklist/test_tracklist_stop.c',
                   'tracklist.c', 'tracklist_file.c', 'usb_release.c']]
    else:
        builds = [['tests/tracklist/test_tracklist.c', pick('core')],
                  ['tests/tracklist/test_tracklist_file.c', pick('file'), pick('core')]]
    caught = False
    for i, sources in enumerate(builds):
        exe = os.path.join(folder, f't{i}')
        if subprocess.run(['gcc', *flags, *sources, '-I', '.', '-o', exe]).returncode:   # a copy's own -I comes first
            print('BUILD FAILED:', name); bad += 1; caught = None; break
        args = [os.path.join(folder, 'stick')] if 'tests/tracklist/test_tracklist_stop.c' in sources else []
        try:
            caught = caught or subprocess.run([exe, *args], capture_output=True, timeout=180).returncode != 0
        except subprocess.TimeoutExpired:
            caught = True
    os.remove(mutated)
    if caught is None: continue
    print(('caught   ' if caught else 'MISSED   ') + where + ': ' + name)
    bad += not caught
print('mutations:', len(M), 'missed or broken:', bad)
sys.exit(1 if bad else 0)
