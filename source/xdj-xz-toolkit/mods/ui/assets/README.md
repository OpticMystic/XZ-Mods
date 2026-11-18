# XZ Mods logo

`xz-mods-source.png` is the project owner's brand mark, supplied on 2026-11-16 for XZ Mods
0.5.1-Beta.3: a red "XZ" (now shown yellow, see below), white "MODS" and `xzmods.com` between rule-and-tick ornaments, on black.
It is a generated bitmap (soft edges, slight noise) and is kept only as the tracing input.

`xz-mods-mark.svg` is the clean artwork: the mark traced into straight-edged polygons in two flat
colours on black: the XZ in the old XZ Mods yellow `#f3e900` (the owner's choice on 2026-11-17; the
source draws it red, and `trace_logo.py` fills it yellow), MODS and the address in white `#ffffff`.
Every image of the logo is drawn from it at its final size; nothing is scaled down from a bitmap.

Where it shows:

| Place | Shows |
| --- | --- |
| Start-up screen (image 1446, 800 x 480) | the full lockup, yellow XZ, white MODS and address |
| Stock empty-deck logo (image 1487, 560 x 92) | the mark without the address, beside `xzmods.com` |
| ABOUT page (112 px) | the mark without the address, on the panel's colour |
| Empty PLAY screen (160 px square), when no USB DJ profile has a logo | the mark small (padding 30) and all in the shell's grey `#8c8c8c`, so it sits quietly in the middle of the deck screen |

A DJ's own logo from the USB replaces the built-in one on the empty PLAY screen, pixel for pixel.

| File | Made by | Used for |
| --- | --- | --- |
| `xz-mods-mark.svg` | `trace_logo.py` (once per new artwork) | input of everything below |
| `../logo_assets.h` | `build_logo.py` | built-in logo: empty PLAY screen (160 px), ABOUT (112 px) |
| `xz-mods.png` | `build_logo.py` | full lockup, 1536 x 1024 on black, for the desktop app and packs |
| `xz-mods-mark.png` | `build_logo.py` | mark without the address, 1024 x 1024 on black, for square slots |
| `branding/splash.rgb565`, `logo.rgb565` | `build_branding_assets.py` | start-up screen (image 1446) and the stock empty-deck logo (image 1487) |

Small or square places show the mark without the address line; the start-up screen shows the full
lockup.

```powershell
# New artwork from the owner: trace it, then regenerate.
python mods/ui/trace_logo.py mods/ui/assets/xz-mods-source.png mods/ui/assets/xz-mods-mark.svg
python mods/ui/build_logo.py                   # logo_assets.h and the PNG masters
python mods/ui/build_logo.py --check           # fails when they no longer match the SVG
python mods/ui/build_branding_assets.py . <runtime bundle>/branding
```

`trace_logo.py` needs OpenCV; the other scripts need only numpy and Pillow.

To prepare a private GUI pack from a locally obtained XZ 1.26 image pack:

```powershell
python mods/ui/build_branding.py --stock-pack <local-imagedata.dat> --output <new-imagedata.dat> --preview-dir <previews>
```

The command changes only loading images 1446 and 1487, verifies that all other
bytes remain unchanged, and renders previews decoded from the RGB565 pack.
The resulting pack contains locally supplied firmware graphics and must not
be distributed. `mods/private_usb.py --gui-pack <new-imagedata.dat>` includes
it in a private image whose loader binds the pack from RAM. Reboot clears the
RAM bind. This does not update a USB image already on the device.
