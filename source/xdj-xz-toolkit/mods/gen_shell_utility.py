"""Generate shell_utility_table.h from the deck application binary (read-only static analysis).

The stock UTILITY menu is a table of 56-byte line descriptors in .data:
  {label wchar*, choices wchar*[], count, current, index, ?, InitItem, SetLine, StartEdit,
   SelMenu, IsModified, EnterMenu, IsGrey, Reset}
The choice arrays are packed back to back, so the descriptor's count says where each one ends.
Util_SetLine_<item> shows choices[f(UiGetUtil<item>())] and Util_EnterMenu_<item> calls
UiSetUtil<item>(g(line index)). This script recognises the three forms the firmware uses and records,
for every choice, the setter argument g(index):
  direct  SetLine keeps r0 as the index, EnterMenu passes the index (mov r0, r3)
  plus1   SetLine subtracts 1 (sub r3, r0, #1), EnterMenu passes index + 1 (add r0, r3, #1)
  table   EnterMenu looks the argument up in a literal-pool table for low indices and passes 0 above
          (QUANTIZE BEAT VALUE, HOT CUE AUTO LOAD); the SetLine's compare chain is checked against it
The getter returns one of those arguments, so the runtime maps a reading back to the index by
searching the list. An item whose handlers match none of the forms, or whose SetLine disagrees with
its EnterMenu, is emitted with mapped = 0 and the runtime publishes it read-only.

Many SetLines also apply the highlighted choice live while the line is edited (Pub_GuiCom_Change<item>,
a thin wrapper of UiSet<item>, the mixer's or panel's current value); that address is recorded too and
the runtime calls it with the same argument before the setter, the sequence stock's menu produces.

EnterMenu must do nothing but call the setter (Language calls it twice, once through a one-instruction
alias), so calling the setter alone is all stock's menu does.

Every item also carries the shell item id it is published under, its UTILITY category, and the first
32 bytes of the getter and setter (the runtime refuses to call a function whose bytes differ).
Labels and choices are stock text with brand names replaced and non-ASCII language names spelled in
ASCII (the shell font is ASCII).

Usage: python gen_shell_utility.py RBP_BINARY OUTPUT_HEADER
Needs the ABI study helper xzelf.py (VJToolsLibrary docs/research/2026-10-18-xz-shell-abi).
"""
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'VJToolsLibrary/docs/research/2026-10-18-xz-shell-abi'))
from xzelf import Rbp  # noqa: E402

rbp = Rbp(sys.argv[1])
out = Path(sys.argv[2])

# Shell item ids (shell/shell_settings.h) for the items the shell already names; the rest get stable
# runtime ids XZ_SHRT_UTIL_ID(n) (shell_utility.h). Never renumber an existing entry.
SHELL_IDS = {
    'NeedleLock': 'XZ_SHELL_ITEM_NEEDLE_LOCK',  # was XZ_SHRT_UTIL_ID(0), which stays reserved
    'EjectLoadLock': 'XZ_SHELL_ITEM_LOAD_LOCK', 'QuantizeValue': 'XZ_SHELL_ITEM_QUANTIZE_BEAT',
    'HotCueAutoLoad': 'XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD', 'HotCueColor': 'XZ_SHELL_ITEM_HOT_CUE_COLOR',
    'AutoCueLevel': 'XZ_SHELL_ITEM_AUTO_CUE_LEVEL', 'SlipFlashing': 'XZ_SHELL_ITEM_SLIP_FLASHING',
    'OnAirDisplay': 'XZ_SHELL_ITEM_ON_AIR_DISPLAY', 'JogRingBrightness': 'XZ_SHELL_ITEM_JOG_RING_BRIGHTNESS',
    'VinylSpeedAdjust': 'XZ_SHELL_ITEM_VINYL_SPEED_ADJUST', 'ChFaderCurve': 'XZ_SHELL_ITEM_CH_FADER_CURVE',
    'CrossFaderCurve': 'XZ_SHELL_ITEM_CROSSFADER_CURVE', 'EqCurve': 'XZ_SHELL_ITEM_EQ_MODE',
    'MonoSplitStereo': 'XZ_SHELL_ITEM_HEADPHONE_CUE_MODE', 'Language': 'XZ_SHELL_ITEM_LANGUAGE',
    'LcdBrightness': 'XZ_SHELL_ITEM_LCD_BRIGHTNESS', 'JogLcdBrightness': 'XZ_SHELL_ITEM_JOG_LCD_BRIGHTNESS',
    'ScreenSaver': 'XZ_SHELL_ITEM_SCREEN_SAVER', 'AutoStandby': 'XZ_SHELL_ITEM_AUTO_STANDBY',
}
RUNTIME_IDS = ['NeedleLock', 'JogDisplayMode', 'JogRingIndicator', 'AutoPlayMode', 'MasterEqualizer', 'MasterAtt',
               'BoothMonitorAtt', 'PeakLimiter', 'MicOutToBooth', 'TalkOverMode', 'TalkOverLevel', 'UsbOutputLevel',
               'Ch3ControlTone', 'Ch4ControlTone', 'MixerMidiMessage', 'PadButtonBrightness', 'TouchPanelCalibration']
CATEGORY = {
    'DJ_SETTING': ['EjectLoadLock', 'NeedleLock', 'QuantizeValue', 'HotCueAutoLoad', 'HotCueColor', 'AutoCueLevel',
                   'VinylSpeedAdjust', 'AutoPlayMode'],
    'DISPLAY_LCD': ['JogDisplayMode', 'LcdBrightness', 'JogLcdBrightness', 'ScreenSaver', 'Language'],
    'DISPLAY_INDICATOR': ['SlipFlashing', 'OnAirDisplay', 'JogRingBrightness', 'JogRingIndicator', 'PadButtonBrightness'],
    'SYSTEM': ['AutoStandby', 'TouchPanelCalibration'],
}
# Language names the shell font cannot draw, by their English names.
ASCII_NAMES = {'РУССКИЙ': 'RUSSIAN', '한국어': 'KOREAN', '日本語': 'JAPANESE', '中文(简体)': 'CHINESE (SIMPLIFIED)',
               '中文(繁體)': 'CHINESE (TRADITIONAL)', 'ΕΛΛΗΝΙΚΑ': 'GREEK'}


def ascii_text(text):
    text = text.replace('rekordbox', 'LIBRARY')
    if text in ASCII_NAMES:
        return ASCII_NAMES[text]
    plain = unicodedata.normalize('NFKD', text).encode('ascii', 'ignore').decode()
    if not plain.strip() or len(plain) < len(text) / 2:
        raise SystemExit(f'no ASCII name for {text!r}: add it to ASCII_NAMES')
    return plain


def wide(address):
    chars = []
    for i in range(128):
        c = rbp.u16(address + 2 * i)
        if not c:
            break
        chars.append(chr(c))
    return ''.join(chars)


names = {}
for name in rbp.by_name:
    if name.startswith(('Util_', 'UiGetUtil', 'UiSetUtil', 'wcUtil')):
        names[rbp.sym(name)] = name


def code_of(function):
    start, size = rbp.func(function)[1:3]
    return list(rbp.disasm(start, size))


def call_index(code, prefix):
    for i, ins in enumerate(code):
        if ins.mnemonic == 'bl' and ins.op_str.startswith('#0x') and names.get(int(ins.op_str[1:], 16), '').startswith(prefix):
            return i, names[int(ins.op_str[1:], 16)]
    return None, None


def imm(text):
    m = re.fullmatch(r'#(0x[0-9a-f]+|\d+)', text.strip())
    return int(m.group(1), 0) if m else None


def setter_args(enter, count):
    """The argument EnterMenu passes for each line index, or None for an unrecognised form."""
    code = code_of(enter)
    i, _ = call_index(code, 'UiSetUtil')
    if i is None:
        return None
    before = [f'{x.mnemonic} {x.op_str}' for x in code[max(0, i - 8):i]]
    if before[-1] == 'mov r0, r3':
        return list(range(count))
    if before[-1] in ('add r0, r3, #1',) or 'add r0, r3, #1' in before[-3:]:
        # Language stores the index and then calls a second setter alias with the same index + 1.
        return list(range(1, count + 1))
    # table: [cmp idx, #k ; ... ldrls rB, [pc, #lit] ; movhi r0, #0 ; addls ... lsl #2 ; ldrls r0, [r, #off]]
    joined = ' ; '.join(before)
    m_off = re.search(r'ldrls r0, \[r\d, #(0x[0-9a-f]+|\d+)\]', joined)
    m_lit = [x for x in code[max(0, i - 8):i] if x.mnemonic == 'ldrls' and '[pc' in x.op_str]
    m_cmp = re.search(r'cmp r\d, #(\d+)', joined)
    if not (m_off and m_lit and m_cmp and 'movhi r0, #0' in joined):
        return None
    lit = m_lit[0]
    pool = lit.address + 8 + int(re.search(r'#(0x[0-9a-f]+|\d+)\]', lit.op_str).group(1), 0)
    base = rbp.u32(pool) + int(m_off.group(1), 0)
    limit = int(m_cmp.group(1))
    shift = 1 if re.search(r'sub r\d, r3, #1', joined) else 0
    args = []
    for idx in range(count):
        k = idx - shift
        args.append(rbp.u32(base + 4 * k) if 0 <= k <= limit else 0)
    return args


def getter_index(setline, args, count):
    """Checks the SetLine maps every setter argument back to its own index."""
    code = code_of(setline)
    i, _ = call_index(code, 'UiGetUtil')
    if i is None:
        return False
    nxt = [f'{x.mnemonic} {x.op_str}' for x in code[i + 1:i + 5]]
    if 'sub r3, r0, #1' in nxt[:1] or (nxt[0] == 'mov r3, r0' and 'sub r3, r3, #1' in nxt):
        return args == list(range(1, count + 1))
    if (nxt[0].startswith('mov r') and nxt[0].endswith(', r0') or nxt[0].startswith('str r0')) and             not any(x.startswith(('sub', 'add', 'cmp')) for x in nxt[:2]):
        return args == list(range(count))
    # Compare chain: cmp r0, #v ; moveq r3, #index ... ; the final branch handles v == 0 and "other".
    mapping, other = {}, None
    rest = code[i + 1:]
    for j, x in enumerate(rest):
        if x.mnemonic == 'cmp' and x.op_str.startswith('r0, #'):
            v = imm(x.op_str.split(', ')[1])
            for y in rest[j + 1:j + 4]:
                if y.mnemonic == 'moveq' and y.op_str.startswith('r3, #'):
                    mapping[v] = imm(y.op_str.split(', ')[1])
                    break
                if y.mnemonic == 'streq' and y.op_str.startswith('r0, ['):
                    mapping[v] = v
                    break
        if x.mnemonic == 'movne' and x.op_str == 'r7, #0':
            other = 0  # any other reading shows choice 0
        if x.mnemonic == 'b' and mapping:
            break
    if not mapping:
        return False
    for idx, a in enumerate(args):
        want = mapping.get(a, other)
        if want != idx:
            return False
    return True


def only_setter(enter, setter):
    """EnterMenu does nothing but call the setter: every call goes to it or to a one-instruction alias of it."""
    target = rbp.sym(setter)
    for x in code_of(enter):
        if x.mnemonic in ('bl', 'blx', 'b') and x.op_str.startswith('#0x'):
            to = int(x.op_str[1:], 16)
            start, size = rbp.func(to)[1:3] if rbp.func(to) else (to, 0)
            if x.mnemonic == 'b' and start == rbp.func(enter)[1]:
                continue  # a branch inside EnterMenu itself
            alias = size == 4 and next(iter(rbp.disasm(start, 4))).op_str == f'#0x{target:x}'
            if to != target and not alias:
                return False
        elif x.mnemonic == 'blx':
            return False
    return True


def live_apply(setline, args, count):
    """The Pub_GuiCom_Change<item> call a SetLine makes while the line is being edited (stock applies the
    highlighted choice live, then EnterMenu stores it). Returns its address, 0 when the SetLine has none,
    or None when its argument is not the setter argument."""
    code = code_of(setline)
    for k, x in enumerate(code):
        if x.mnemonic == 'bl' and (rbp.name(int(x.op_str[1:], 16)) or '').startswith('Pub_GuiCom_Change'):
            ctx = [f'{y.mnemonic} {y.op_str}' for y in code[max(0, k - 4):k]]
            if ctx[-1].startswith('mov r0, r') and ctx[-1] != 'mov r0, r0':
                ok = args == list(range(count))
            elif any(c.startswith('addne r0, r') and c.endswith(', #1') for c in ctx):
                ok = args == list(range(1, count + 1))
            else:
                ok = False
            return int(x.op_str[1:], 16) if ok else None
    return 0


anchor = rbp.literal_refs(rbp.sym('Util_SetLine_NeedleLock'))[0][0] - 7 * 4
STRIDE = 56


def descriptor(at):
    words = [rbp.u32(at + 4 * k) for k in range(14)]
    if not names.get(words[7], '').startswith('Util_SetLine_'):
        return None
    return words


first = anchor
while descriptor(first - STRIDE):
    first -= STRIDE
items = []
at = first
while (words := descriptor(at)):
    setline = names[words[7]]
    enter = names.get(words[11], '')
    item = setline[len('Util_SetLine_'):]
    at += STRIDE
    if not words[0] or item.endswith('Category') or item in ('Blank', 'VersionNo'):
        continue
    label = ascii_text(wide(words[0]))
    count = min(words[2], 32)
    choices = [ascii_text(wide(rbp.u32(words[1] + 4 * k))) for k in range(count)] if words[1] else []
    gi, getter = call_index(code_of(words[7]), 'UiGetUtil')
    si, setter = call_index(code_of(words[11]), 'UiSetUtil') if enter.startswith('Util_EnterMenu_') else (None, None)
    args = setter_args(words[11], count) if setter else None
    apply = live_apply(words[7], args, count) if args is not None else 0
    mapped = bool(getter and setter and args is not None and getter_index(words[7], args, count) and
                  only_setter(words[11], setter) and apply is not None)
    items.append(dict(item=item, label=label, choices=choices or ['BY THE PLAYER'], args=args or [0] * max(count, 1),
                      getter=getter, setter=setter, mapped=mapped, apply=apply or 0,
                      get=rbp.sym(getter) if getter else 0, set=rbp.sym(setter) if setter else 0))

for it in items:
    if it['item'] in SHELL_IDS:
        it['id'] = SHELL_IDS[it['item']]
    elif it['item'] in RUNTIME_IDS:
        it['id'] = f'XZ_SHRT_UTIL_ID({RUNTIME_IDS.index(it["item"])})'
    else:
        raise SystemExit(f'{it["item"]}: give it an id in SHELL_IDS or RUNTIME_IDS')
    it['category'] = next((c for c, keys in CATEGORY.items() if it['item'] in keys), 'MIXER')


def c_string(text):
    data = text.encode('ascii')
    return '"' + ''.join(chr(b) if b not in (34, 92) else '\\' + chr(b) for b in data) + '"'


def guard(address):
    if not address:
        return '{0}'
    return '{' + ','.join(f'0x{b:02x}' for b in rbp.read(address, 32)) + '}'


version = rbp.sym('UiGetUtilVersion')
lines = ['/* SPDX-License-Identifier: MIT */',
         '/* Generated by gen_shell_utility.py from the deck application binary. Do not edit. */',
         '#ifndef XZ_SHELL_UTILITY_TABLE_H', '#define XZ_SHELL_UTILITY_TABLE_H', '#include "shell_utility.h"']
for i, it in enumerate(items):
    lines.append(f'static const char *const xz_util_choices_{i}[]={{' + ','.join(c_string(c) for c in it['choices']) + '};')
    lines.append(f'static const int32_t xz_util_args_{i}[]={{' + ','.join(str(a) for a in it['args']) + '};')
lines.append('static const struct xz_shut_def xz_shut_defs[]={')
for i, it in enumerate(items):
    lines.append(f' {{{c_string(it["item"])},{c_string(it["label"])},{it["id"]},XZ_SHELL_SETTING_CAT_{it["category"]},'
                 f'xz_util_choices_{i},xz_util_args_{i},{len(it["choices"])},'
                 f'0x{it["get"]:x}u,0x{it["set"]:x}u,0x{it["apply"]:x}u,{guard(it["get"])},{guard(it["set"])},{guard(it["apply"])},'
                 f'{1 if it["mapped"] else 0}}}, /* {it["getter"]} / {it["setter"]}'
                 + (f' / {rbp.name(it["apply"])}' if it['apply'] else '') + ' */')
lines += ['};', f'#define XZ_SHUT_DEFS {len(items)}',
          f'/* UiGetUtilVersion: returns the version text (char *). */',
          f'#define XZ_SHUT_VERSION_GET 0x{version:x}u',
          f'static const uint8_t xz_shut_version_code[32]={guard(version)};', '#endif', '']
out.write_bytes('\n'.join(lines).encode())
print(f'{len(items)} items: ' + ', '.join(f'{it["item"]}{"" if it["mapped"] else "*"}' for it in items))
for it in items:
    print(f'  {it["item"]}: {it["choices"]} -> {it["args"]}')
