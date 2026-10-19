# USB settings codec

This standalone POSIX module reads and updates `MYSETTING.DAT`, `MYSETTING2.DAT`,
`DJMMYSETTING.DAT` and `DEVSETTING.DAT` under `PIONEER/`. It does not load settings
into the running player, discover devices, create directories or write to hardware
by itself. The runtime must choose the correct file kind and explicitly call save.
The two player files have the same length, so length alone cannot distinguish them.

## API and preservation contract

Include `my_settings.h` and compile `my_settings.c` as C11. `struct ms_settings`
contains a typed union with `player`, `player2`, `mixer` and `device` members, plus
the complete original bytes. Each known field uses an enum. `ms_field_info` exposes
its file kind, body offset, display/behaviour classification and every supported
numeric choice with its label. `ms_choice_label` returns NULL for unknown choices.
The label "Digital" names jog mode value 0x80; "Library setting" names hot-cue
auto-load value 0x82.

- `ms_parse(kind, bytes, size, out)` validates exact framing, header text termination
  and checksum before replacing `out`. Failures leave `out` untouched.
- `ms_validate(settings)` also rejects unsupported edited values. Existing unknown
  enum bytes remain valid and visible numerically, including zero-filled fields
  from older writers. This is lossless validation, not proof the current player
  implements every setting or choice.
- `ms_get` and `ms_set` access a field by its stable ID. `ms_set` accepts documented
  choices only and rejects fields belonging to another file kind.
- `ms_serialize` overlays typed fields and updates the CRC. It preserves every other
  byte, including producer/software/version text, string padding, body constants,
  reserved bytes, unknown field choices and the two footer bytes after the CRC.
  An unmodified parsed object serializes byte-exact. Treat `raw`, `size` and `kind`
  as immutable after parsing. Reparse serialized output to establish a new baseline.
- `ms_read_file` reads at most 161 bytes and rejects truncated or oversized files.
- `ms_write_atomic(path, settings)` validates before any write, checks `ST_RDONLY`,
  creates a unique temporary file in the destination directory, writes fully,
  syncs and closes the file, renames it, then syncs the directory. Existing regular
  file permission bits are retained; new files use 0600. It rejects nonregular and
  symlink destinations. Pre-rename failures remove the temporary file and preserve
  the destination. `MS_ERR_READ_ONLY` covers both the mount check and `EROFS` from
  later operations. `MS_ERR_DURABILITY` means rename succeeded but directory sync
  failed; the new bytes are already visible. Other filesystem errors return
  `MS_ERR_IO` with the original errno. Use `ms_error_string` for UI error text.

The caller must keep the selected device mounted and its directory stable during
save, and serialize competing saves. The helper assumes a trusted directory; it
does not prevent another process swapping parent directories or ejecting a device.
A successful rename is atomic for readers, but power-loss durability still depends
on the filesystem and device. No fallback performs a non-atomic overwrite.

## Binary layout and checksum

All multibyte integers are little-endian. The 104-byte header is common:

| Absolute offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Length of three text slots, must be 96 |
| 0x04 | 32 | Producer text and original padding |
| 0x24 | 32 | Export software text and original padding |
| 0x44 | 32 | Version text and original padding |
| 0x64 | 4 | Body size, must match the selected kind |
| 0x68 | body size | Settings body |
| 0x68 + body size | 2 | CRC, low byte first |
| 0x6a + body size | 2 | Unknown footer, retained |

| Kind | Filename | Body bytes | Total bytes | CRC input |
| --- | --- | --- | --- | --- |
| MS_PLAYER | MYSETTING.DAT | 40 | 148 | Body only |
| MS_PLAYER2 | MYSETTING2.DAT | 40 | 148 | Body only |
| MS_MIXER | DJMMYSETTING.DAT | 52 | 160 | Header and body |
| MS_DEVICE | DEVSETTING.DAT | 32 | 140 | Body only |

CRC-16/XMODEM uses polynomial 0x1021, initial value 0, no input/output reflection,
and no final XOR. Check value for ASCII `123456789` is 0x31c3. The CRC and footer
are excluded from the input. Header text contents and body magic/version words
are retained rather than constrained to a particular software or firmware version.
This permits differing producer/version strings and future reserved-byte use.
Header corruption outside CRC coverage cannot be detected unless it breaks framing.

## Field map

Offsets below are relative to the body at absolute 0x68. "Display" includes
screen, language, illumination and indicator preferences. "Behaviour" includes
transport, search, audio, timing and control preferences. Choice lists describe
publicly decoded files across multiple devices, not a promise of XDJ-XZ support.

### MYSETTING.DAT

| Body offset | Typed field | Effect | Encoded choices |
| --- | --- | --- | --- |
| 0x08 | `player.on_air_display` | Display | 0x80 Off; 0x81 On |
| 0x09 | `player.lcd_brightness` | Display | 0x81 1; 0x82 2; 0x83 3; 0x84 4; 0x85 5 |
| 0x0a | `player.quantize` | Behaviour | 0x80 Off; 0x81 On |
| 0x0b | `player.auto_cue_level` | Behaviour | 0x80 -36 dB; 0x81 -42 dB; 0x82 -48 dB; 0x83 -54 dB; 0x84 -60 dB; 0x85 -66 dB; 0x86 -72 dB; 0x87 -78 dB; 0x88 Memory |
| 0x0c | `player.language` | Display | 0x81 English; 0x82 French; 0x83 German; 0x84 Italian; 0x85 Dutch; 0x86 Spanish; 0x87 Russian; 0x88 Korean; 0x89 Chinese simplified; 0x8a Chinese traditional; 0x8b Japanese; 0x8c Portuguese; 0x8d Swedish; 0x8e Czech; 0x8f Hungarian; 0x90 Danish; 0x91 Greek; 0x92 Turkish |
| 0x0e | `player.jog_ring_brightness` | Display | 0x80 Off; 0x81 Dark; 0x82 Bright |
| 0x0f | `player.jog_ring_indicator` | Display | 0x80 Off; 0x81 On |
| 0x10 | `player.slip_flashing` | Display | 0x80 Off; 0x81 On |
| 0x14 | `player.disc_slot_illumination` | Display | 0x80 Off; 0x81 Dark; 0x82 Bright |
| 0x15 | `player.eject_lock` | Behaviour | 0x80 Unlock; 0x81 Lock |
| 0x16 | `player.sync` | Behaviour | 0x80 Off; 0x81 On |
| 0x17 | `player.play_mode` | Behaviour | 0x80 Continue / On; 0x81 Single / Off |
| 0x18 | `player.quantize_beat_value` | Behaviour | 0x80 1 beat; 0x81 1/2 beat; 0x82 1/4 beat; 0x83 1/8 beat |
| 0x19 | `player.hotcue_autoload` | Behaviour | 0x80 Off; 0x81 On; 0x82 Library setting |
| 0x1a | `player.hotcue_color` | Display | 0x80 Off; 0x81 On |
| 0x1d | `player.needle_lock` | Behaviour | 0x80 Unlock; 0x81 Lock |
| 0x20 | `player.time_mode` | Display | 0x80 Elapsed; 0x81 Remain |
| 0x21 | `player.jog_mode` | Behaviour | 0x80 Digital; 0x81 Vinyl |
| 0x22 | `player.auto_cue` | Behaviour | 0x80 Off; 0x81 On |
| 0x23 | `player.master_tempo` | Behaviour | 0x80 Off; 0x81 On |
| 0x24 | `player.tempo_range` | Behaviour | 0x80 6 percent; 0x81 10 percent; 0x82 16 percent; 0x83 Wide |
| 0x25 | `player.phase_meter` | Display | 0x80 Type 1; 0x81 Type 2 |

### MYSETTING2.DAT

| Body offset | Typed field | Effect | Encoded choices |
| --- | --- | --- | --- |
| 0x00 | `player2.vinyl_speed_adjust` | Behaviour | 0x80 Touch and release; 0x81 Touch; 0x82 Release |
| 0x01 | `player2.jog_display_mode` | Display | 0x80 Auto; 0x81 Info; 0x82 Simple; 0x83 Artwork |
| 0x02 | `player2.pad_button_brightness` | Display | 0x81 1; 0x82 2; 0x83 3; 0x84 4 |
| 0x03 | `player2.jog_lcd_brightness` | Display | 0x81 1; 0x82 2; 0x83 3; 0x84 4; 0x85 5 |
| 0x04 | `player2.waveform_divisions` | Display | 0x80 Time scale; 0x81 Phrase |
| 0x0a | `player2.waveform` | Display | 0x80 Waveform; 0x81 Phase meter |
| 0x0c | `player2.beat_jump_beat_value` | Behaviour | 0x80 1/2 beat; 0x81 1 beat; 0x82 2 beats; 0x83 4 beats; 0x84 8 beats; 0x85 16 beats; 0x86 32 beats; 0x87 64 beats |

### DJMMYSETTING.DAT

| Body offset | Typed field | Effect | Encoded choices |
| --- | --- | --- | --- |
| 0x0c | `mixer.channel_fader_curve` | Behaviour | 0x80 Steep top; 0x81 Linear; 0x82 Steep bottom |
| 0x0d | `mixer.crossfader_curve` | Behaviour | 0x80 Constant power; 0x81 Slow cut; 0x82 Fast cut |
| 0x0e | `mixer.headphones_pre_eq` | Behaviour | 0x80 Post EQ; 0x81 Pre EQ |
| 0x0f | `mixer.headphones_mono_split` | Behaviour | 0x80 Stereo; 0x81 Mono split |
| 0x10 | `mixer.beat_fx_quantize` | Behaviour | 0x80 Off; 0x81 On |
| 0x11 | `mixer.mic_low_cut` | Behaviour | 0x80 Off; 0x81 On |
| 0x12 | `mixer.talk_over_mode` | Behaviour | 0x80 Advanced; 0x81 Normal |
| 0x13 | `mixer.talk_over_level` | Behaviour | 0x80 -24 dB; 0x81 -18 dB; 0x82 -12 dB; 0x83 -6 dB |
| 0x14 | `mixer.midi_channel` | Behaviour | 0x80 1; 0x81 2; 0x82 3; 0x83 4; 0x84 5; 0x85 6; 0x86 7; 0x87 8; 0x88 9; 0x89 10; 0x8a 11; 0x8b 12; 0x8c 13; 0x8d 14; 0x8e 15; 0x8f 16 |
| 0x15 | `mixer.midi_button_type` | Behaviour | 0x80 Toggle; 0x81 Trigger |
| 0x16 | `mixer.display_brightness` | Display | 0x80 White; 0x81 1; 0x82 2; 0x83 3; 0x84 4; 0x85 5 |
| 0x17 | `mixer.indicator_brightness` | Display | 0x80 1; 0x81 2; 0x82 3 |
| 0x18 | `mixer.channel_fader_curve_long` | Behaviour | 0x80 Exponential; 0x81 Smooth; 0x82 Linear |

### DEVSETTING.DAT

| Body offset | Typed field | Effect | Encoded choices |
| --- | --- | --- | --- |
| 0x09 | `device.overview_waveform_type` | Display | 0x01 Half waveform; 0x02 Full waveform |
| 0x0a | `device.waveform_color` | Display | 0x01 Blue; 0x03 RGB; 0x04 3 band |
| 0x0c | `device.key_display_format` | Display | 0x01 Classic; 0x02 Alphanumeric |
| 0x0d | `device.waveform_current_position` | Display | 0x01 Center; 0x02 Left |

Every body byte omitted from these tables remains opaque. In particular,
`MYSETTING.DAT` body 0x00..0x07, 0x0d, 0x11..0x13, 0x1b..0x1c,
0x1e..0x1f and 0x26..0x27 are retained. `MYSETTING2.DAT` body 0x05..0x09,
0x0b and 0x0d..0x27 are retained. `DJMMYSETTING.DAT` body 0x00..0x0b and
0x19..0x33 are retained. `DEVSETTING.DAT` body 0x00..0x08, 0x0b and
0x0e..0x1f are retained.

### Personal/background colour

No verified byte in these four files maps to personal/background colour. The
module deliberately exposes no invented colour offset. The local v1.26 firmware
has `dbcl_SetBackGroundColor` at 0x210248 and `DsqlProp_SetBackGroundColor` at
0x126d74. The former accepts IDs 0..8 and dispatches a database operation; the
latter uses a database property operation and caches the result. This is evidence
of a separate database path, not proof that no file extension ever carries colour.
A future integration must trace that property rather than overwrite a reserved byte.

## Local firmware evidence

Read-only inspection used the ELF at
`VJToolsLibrary/packages/xdj-xz-toolkit/vendor/decrypted_iso/pdj/extracted/pdj/rbp`,
SHA-256 `6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2`.
No firmware bytes or extracted code are included in this public module.

- The binary contains `/PIONEER/MYSETTING.DAT`, `/PIONEER/DEVSETTING.DAT`,
  hidden-directory alternatives, and the settings load/save task symbols.
- `CmnFunc_CmnInfo_DevSetInit` at 0x13ab58 initializes a 32-byte device body
  with magic 0x12345678, revision 1, and bytes 8..12 set to 1.
- `CmnFunc_CmnInfo_DevSetValidityCheck` at 0x13ab98 checks overview byte 9,
  waveform-colour byte 10 and key-display byte 12, matching the public offsets.
  It also checks byte 11 as a 1..2 field. Its meaning is not proven here and it
  stays opaque. This firmware accepts waveform-colour codes 1..3, while the
  current public map labels 1, 3 and 4. Existing unknown code 2 is preserved;
  it has no guessed label. A public 3-band choice does not establish XZ support.
- `TotalMySet_DevSetCopy_LocalDevice` at 0x190bd4 copies all 32 bytes and
  propagates bytes 12, 9, 10 and 11 into mounted-device state. This confirms
  track-source device preferences are distinct from a manual player-settings load.
- The local `vendor/tools/xz_settings_decode.py` documents internal
  `/root/settings/XdjSettings.dat`, a separate record format. This codec never
  touches it. Searches of tracked source and ignored vendor notes in both repos
  found no pre-existing USB-file codec.

This inspection establishes paths and selected consumers. It does not establish
byte-for-byte stock writer output, CRC acceptance on hardware, or the meaning of
all reserved bytes. No stock write routine was invoked.

## Verification and limits

Run on Windows with WSL Ubuntu and GCC, or directly on Linux with GCC:

```text
python source/xdj-xz-toolkit/mods/build.py --host-checks-only --output <local-output>
python source/xdj-xz-toolkit/mods/verify_my_settings.py --zig <zig> --output <local-output>
```

The normal `build.py` test dictionary also builds and ABI-inspects
`my-settings-test` alongside the other ARM tests. The standalone verification
script runs host checks and links both an ARM32 soft-float library and test
executable. The existing ELF gate rejects any GLIBC import newer than 2.4.
The module is not wired into shell or runtime settings application.

`test_my_settings.c` checks 13 unmodified public export samples, all 46 mapped
fields, every choice, 373 sample/choice combinations, every single-bit body/CRC
corruption, truncated and oversized files, header errors, unknown enum and footer
preservation, and simulated old-writer zero fields. Its independent nibble-table
CRC checks every edited byte sequence. POSIX tests create only disposable `/tmp`
files. Fault injection checks ENOSPC, file sync failure, rename failure, directory
sync failure, EINTR and short writes, including original-file preservation and
temporary-file cleanup. The first read-only mount available among `/sys`, `/tmp/.X11-unix` and
`/usr/lib/wsl/drivers` is used only for refusal before file creation. The test
reports a skip if none is read-only.

No USB volumes were mounted in Windows during this work. Read-only searches found
no pre-existing MYSETTING or DEVSETTING fixtures in either repo's `.tmp` tree
before downloading the public corpus. The fixtures
below are public export data, not captures from this owner's USB or deck. Older
firmware compatibility is checked synthetically with old-style version text and
zero/unknown fields; no captured older firmware export was available. The host tests and ARM tests passed, including execution under QEMU with a
Linux ARM sysroot and refusal on the read-only `/tmp/.X11-unix` host mount. Host
address and undefined-behaviour sanitizers also passed. The linked library and
test executable import only GLIBC_2.4 versions. Actual player load/save acceptance
and power-loss behaviour remain unverified.

## Exact public sources and fixture provenance

1. [Current field structures and enum definitions](https://github.com/Holzhaus/rekordcrate/blob/14d54eded7b0d2f6fe67c7871533dca35a73e5fa/src/setting.rs),
   `Setting`, `DevSetting`, `MySetting`, `MySetting2`, `DJMMySetting` and their enums.
   The codec was written independently from the binary layout facts.
2. [My-Setting format description](https://pyrekordbox.readthedocs.io/en/latest/formats/mysetting.html)
   and [checksum implementation](https://github.com/dylanljones/pyrekordbox/blob/master/pyrekordbox/mysettings/file.py).
   The documentation's `DJMSETTING.DAT` spelling in the checksum paragraph refers
   to the `DJMMYSETTING.DAT` mixer file.
3. [Deep Symmetry export analysis](https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html)
   and [analysis-file notes](https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html).
   Their [Kaitai directory](https://github.com/Deep-Symmetry/crate-digger/tree/main/src/main/kaitai)
   contains `rekordbox_pdb.ksy` and `rekordbox_anlz.ksy`. Those describe the database
   and waveform analysis, not these settings files. No settings Kaitai specification
   was found there; the settings offsets above come from source 1.
4. [CRC-16/XMODEM parameters](https://reveng.sourceforge.io/crc-catalogue/16.htm#crc.cat.crc-16-xmodem).

`my_settings_fixtures.h` embeds only binary export samples as byte arrays. Source
repository revision is `14d54eded7b0d2f6fe67c7871533dca35a73e5fa`, under its
[MPL-2.0 license](https://github.com/Holzhaus/rekordcrate/blob/14d54eded7b0d2f6fe67c7871533dca35a73e5fa/LICENSE).
The fixture order and SHA-256 digests are:

| Index | Path under upstream `data/` | SHA-256 |
| --- | --- | --- |
| 0 | `complete_export/demo_tracks/PIONEER/DEVSETTING.DAT` | `da2b3cc24a6e1b1d9458f9e6a4b9bc5d4816739d2da1588557b8bf05058986c5` |
| 1 | `complete_export/demo_tracks/PIONEER/DJMMYSETTING.DAT` | `aa3d445a92750f8b9765017377ac6d6454044211487bd14dc595a9c2f70230f4` |
| 2 | `complete_export/demo_tracks/PIONEER/MYSETTING.DAT` | `a9c6785b868be8cdc417bd71804713e671e1b1bec81a99d2767333002c4046a1` |
| 3 | `complete_export/demo_tracks/PIONEER/MYSETTING2.DAT` | `7e6d248e17e90a357b1839e8f07cf7c95dbaedb6176b17df10b446e2a0a421b8` |
| 4 | `complete_export/empty/PIONEER/DEVSETTING.DAT` | `da2b3cc24a6e1b1d9458f9e6a4b9bc5d4816739d2da1588557b8bf05058986c5` |
| 5 | `complete_export/empty/PIONEER/DJMMYSETTING.DAT` | `aa3d445a92750f8b9765017377ac6d6454044211487bd14dc595a9c2f70230f4` |
| 6 | `complete_export/empty/PIONEER/MYSETTING.DAT` | `a9c6785b868be8cdc417bd71804713e671e1b1bec81a99d2767333002c4046a1` |
| 7 | `complete_export/empty/PIONEER/MYSETTING2.DAT` | `7e6d248e17e90a357b1839e8f07cf7c95dbaedb6176b17df10b446e2a0a421b8` |
| 8 | `devsetting/keydisplayformat-alphanumeric/DEVSETTING.DAT` | `52be579f7303ad266b27aafa8067e6f5878a18c3ddeba9476c09ea82604a6a7a` |
| 9 | `devsetting/overviewwaveformtype-full/DEVSETTING.DAT` | `08cd4df855049f8bc720741369ac1b61ac23fe6f29028c89ed9d4baab1aa960f` |
| 10 | `devsetting/waveformcolor-3band/DEVSETTING.DAT` | `69a82ae55cd30992700d56a7125431d4e5303e361fdee720de78eba9d2b4ec25` |
| 11 | `devsetting/waveformcolor-rgb/DEVSETTING.DAT` | `87efdc9ac5fc81b99510bdf746e98742a25b4d0343805730f4d3b98bd89ad943` |
| 12 | `devsetting/waveformcurrentposition-left/DEVSETTING.DAT` | `c2a4026cea280fac87726bba314ae18074cbdb583312e82dc54c69506ad4bdc3` |
