"""Emulate Smart Cue against private XZ 1.26 code; never connects to hardware.

Requires pyelftools and unicorn==2.1.4. Firmware bytes remain an external input.
Reader/notification callbacks are fixtures; cue setter instructions are real.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPEXC, UC_ARM_REG_C1_C0_2

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rbp',type=Path,required=True)
parser.add_argument('--zig',type=Path,required=True)
parser.add_argument('--evidence',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parent
digest=hashlib.md5(args.rbp.read_bytes()).hexdigest()
assert digest in ('6a7ccb454e52afa26a73f3380706c9ca','627f39fbabb0b47f2d45f4aeac7e5899'),'Expected verified private XZ 1.26 executable'

def load(uc,path,with_symbols=False):
    with path.open('rb') as file:
        elf=ELFFile(file)
        symbols={s.name:int(s['st_value']) for s in elf.get_section_by_name('.symtab').iter_symbols()} if with_symbols else {}
        for seg in elf.iter_segments():
            if seg['p_type']!='PT_LOAD':continue
            address=int(seg['p_vaddr']);start=address&~4095
            size=(address+int(seg['p_memsz'])-start+4095)&~4095
            missing=[(start,start+size)]
            for low,high,_ in uc.mem_regions():
                pieces=[]
                for first,last in missing:
                    if high<first or low>=last:pieces.append((first,last))
                    else:
                        if first<low:pieces.append((first,low))
                        if last>high+1:pieces.append((high+1,last))
                missing=pieces
            for first,last in missing:uc.mem_map(first,last-first)
            uc.mem_write(address,seg.data())
        return symbols

with tempfile.TemporaryDirectory(prefix='xz-smart-cue-') as folder:
    binary=Path(folder)/'fixture.elf'
    linker=Path(folder)/'fixture.ld'
    linker.write_text('ENTRY(test_follow)\nSECTIONS { . = 0x30000000; .text : { *(.text*) } .rodata : { *(.rodata*) } .ARM.exidx : { *(.ARM.exidx*) } .data : { *(.data*) } .bss : { *(.bss*) } }')
    (Path(folder)/'string.h').write_text('#include <stddef.h>\nvoid *memcpy(void*,const void*,size_t);\nvoid *memset(void*,int,size_t);\nint memcmp(const void*,const void*,size_t);\n')
    subprocess.run([str(args.zig),'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-O2','-std=c11',
                    '-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-nostdlib','-static','-fno-pie',
                    '-I'+folder,'-Wl,-T,'+str(linker),str(root/'native.c'),str(root/'cue.c'),
                    str(root/'native_follow_fixture.c'),'-o',str(binary)],check=True)
    def exercise(deck=1,at=1234,slip=0,verified=1,entry='test_smart_cue',broken=None):
        uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        load(uc,args.rbp);fixture_symbols=load(uc,binary,True)
        uc.mem_map(0x10000000,0x100000);uc.mem_map(0x20000000,0x10000)
        physical=deck if deck in (0,1) else 0
        player=0x10001000+physical*0x4000;reader=0x10010000;cue=0x10012000;loop=0x10014000
        engine=0x10020000;players=0x10021000;vtable=0x10022000;info=0x10024000
        calls=[];events=[];reader_reads=0
        def word(address,value):uc.mem_write(address,struct.pack('<I',value&0xffffffff))
        def integer(address):return struct.unpack('<I',uc.mem_read(address,4))[0]
        word(0x10e9688,engine);word(engine+12,players);word(engine+16,players+8)
        word(players,0x10001000);word(players+4,0x10005000)
        word(player+0xc8,reader);word(player+0xed4,0x10016000)
        word(player+0xec8,0x10018000);word(player+0xed8,0x1001a000)
        uc.mem_write(player+0xf51,bytes([slip]));word(player+0xf54,1)
        uc.mem_write(player+0xe66,b'\x01')
        uc.mem_write(player+0xf2c,b'\x01')
        word(reader,vtable);word(vtable+0x40,0x20000010);word(vtable+0x38,0x20000020)
        word(vtable+0x44,0x20000030);word(vtable+0x2c,0x20000040)
        word(cue+0x24,44100);word(cue+0x30,88200)
        uc.mem_write(cue+0x40,b'\x01\x01')
        word(info+0x318,1000);word(info+0x324,2000);uc.mem_write(info+0x334,b'\x01')
        if broken=='engine_null':word(0x10e9688,0)
        if broken=='engine_alignment':word(0x10e9688,engine+1)
        if broken=='engine_wrap':word(0x10e9688,0xfffffffc)
        if broken=='range':word(engine+16,players+12)
        if broken=='player_null':word(players+physical*4,0)
        if broken=='reader_null':word(player+0xc8,0)
        if broken=='unloaded':uc.mem_write(player+0xe66,b'\0')
        before=bytes(uc.mem_read(player,0x2000))
        registers=(UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)
        def ret(value=0):uc.reg_write(UC_ARM_REG_R0,value&0xffffffff);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        forbidden={0x658fc:'cancelReservation',0x73c28:'endSlippingToStayFg',0x60b1c:'stopAudiblePause'}
        def hook(cpu,address,size,user):
            nonlocal reader_reads
            values=[cpu.reg_read(r) for r in registers]
            if address in forbidden:
                calls.append(forbidden[address]);ret(0)
            elif address==fixture_symbols['xz_read_memory']:
                if broken=='read':ret(-1);return
                if values[0]==player+0xc8:
                    reader_reads+=1
                    if broken=='changed_reader' and reader_reads>1:word(values[1],reader+0x100);ret(0);return
                try:cpu.mem_write(values[1],bytes(cpu.mem_read(values[0],values[2])));ret(0)
                except UcError:ret(-1)
            elif address==0x42fbc:
                cpu.mem_write(values[0],bytes(cpu.mem_read(info,0x338)))
                if values[3]:word(values[0]+0x318,integer(fixture_symbols['test_target_ms']))
                ret(values[0])
            elif address==0x20000010:ret(cue)
            elif address==0x20000020:ret(44100*600)
            elif address in (0x20000030,0x20000040):events.append([hex(address),values[1:3]]);ret(1)
            elif address==0x3ae08:ret(0x10026000)
            elif address==0xfba8:ret(0)
            elif address==0x8b244:ret(loop)
            elif address==0x5f70c:ret(0)
            elif address==0x80f54:ret(0)
            elif address==0xef84:cpu.mem_write(values[0],bytes([values[1]&255])*values[2]);ret(values[0])
            elif address==0xe918:
                value=struct.unpack('<d',struct.pack('<II',values[0],values[1]))[0]
                low,high=struct.unpack('<II',struct.pack('<d',float(math.floor(value))))
                cpu.reg_write(UC_ARM_REG_R0,low);cpu.reg_write(UC_ARM_REG_R1,high);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        uc.hook_add(UC_HOOK_CODE,hook)
        uc.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);uc.reg_write(UC_ARM_REG_FPEXC,0x40000000)
        uc.reg_write(UC_ARM_REG_SP,0x100ff000);uc.reg_write(UC_ARM_REG_LR,0x2000fff0)
        for reg,value in zip(registers,(deck,at,verified,0)):uc.reg_write(reg,value&0xffffffff)
        uc.emu_start(fixture_symbols[entry],0x2000fff0,count=100000)
        assert uc.reg_read(UC_ARM_REG_PC)==0x2000fff0,'Emulator instruction limit reached'
        result={'deck':deck,'at_ms':at,'slip_flags':slip,'result':uc.reg_read(UC_ARM_REG_R0),
                'transport_calls':calls,'player_unchanged':bytes(uc.mem_read(player,0x2000))==before,
                'cue_sample':integer(cue+0x24),'cue_out':integer(cue+0x30),'cue_loop_flags':list(uc.mem_read(cue+0x40,2)),
                'reader_callbacks':events,'failure_fixture':broken,'verified':verified}
        return result
    cases=[dict(deck=deck,at=at,slip=slip) for deck in (0,1) for at in (0,1,1000,1234,263421) for slip in (0,1,2,3)]
    cases += [dict(entry='test_follow',broken=broken) for broken in ('read','engine_null','engine_alignment','engine_wrap','range','player_null','reader_null','unloaded','changed_reader')]
    cases += [dict(entry='test_follow',verified=0),dict(entry='test_follow',at=-1),dict(entry='test_follow',deck=-1),dict(entry='test_follow',deck=2)]
    results=[exercise(**case) for case in cases]
    args.evidence.parent.mkdir(parents=True,exist_ok=True)
    args.evidence.write_text(json.dumps({'firmware_md5':digest,'cases':results},indent=2))
    for case,result in zip(cases,results):
        assert not result['transport_calls'],('Smart Cue must not cancel playback reservations or alter slip/transport',result)
        assert result['player_unchanged'],('Smart Cue changed player transport fields',result)
        if case.get('entry')=='test_follow':
            assert result['result']==0 and result['cue_sample']==44100 and not result['reader_callbacks'],result
        else:
            assert result['result']==1 and result['cue_sample']==int(case['at']*44.1),result
            assert result['cue_out']==0xffffffff and result['cue_loop_flags']==[0,0],result
    print(f'PASS {len(results)} real ARM cases: both decks, sample conversion, slip flags, no transport mutation, invalid/stale readers')
