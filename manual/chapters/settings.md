---
interface: both
settings:
---

XZ Mods keeps its settings on your USB, so they travel with you from player to player. This page explains where they are kept and what happens when the USB changes, then lists every setting with its values, its default and the chapter that explains it.

## Where settings are kept

Your settings live in one file on the USB: `VJ.Tools/XZ-Mods.cfg`. You change them in MODS, or on your computer with the [XZ Mods app](desktop-app). There is no save button. Every change saves on its own to the USB you are browsing, and the bottom of the MODS page says what happened for a few seconds.

| Message | Meaning |
|---|---|
| SAVED TO USB 1 (or USB 2) | The change is on that USB. |
| NOT SAVED TO USB 1: CHECK USB | The USB could not be written. Check that it is not full or locked, then change the setting again. |
| SAVED ON THIS XZ / NO USB | No USB is in. The change is kept on this player only. |
| USB REMOVED: SETTINGS KEPT | You pulled the USB. The player keeps your settings for the rest of the set. |
| USB 1 HAS NO SETTINGS: KEPT CURRENT | The USB you browsed has no settings file. The player keeps the settings it has. |
| USB 1 SETTINGS INVALID: KEPT CURRENT | The settings file on that USB could not be read. The player keeps the settings it has. |
| NOT SAVED: CHANGES LAST UNTIL POWER OFF. | This start of XZ Mods cannot save. Your changes work until you switch the player off. |

## When the USB changes

The player follows the same rules as its own MY SETTINGS:

- Browsing a USB in USB 1 or USB 2 loads that USB's settings. Browsing a computer or a LINK source never changes them.
- A USB without settings, or with settings the player cannot read, leaves your current settings as they are. Nothing resets to defaults in the middle of a set.
- The player remembers the last settings it used. Started without a USB, it uses those. A USB with its own settings takes over again once you browse it.

The player's own MY SETTINGS, which rekordbox writes to the USB, are a separate set. XZ Mods settings never change them, and loading MY SETTINGS does not change XZ Mods settings.

MODERN's own display choices are settings too: its waveform colour, waveform position and zoom, how it writes keys, the TRACK COLOURS choice, the LAMPS AND TITLES and BYPASS BUTTON switches, and the columns of its track list. So are the 3-band colours. They are saved to the USB with everything else and come back after the player restarts.

Things you do during a set, such as muting a stem, BYPASS, a key shift, KEY SYNC or a RAM copy, are not settings. They start fresh every time the player starts.

## Going back to the defaults

There is no single button that resets every setting. Set each one back in MODS, using the Default column below. Some pages have their own way back: PAGE 1: STOCK for beat jump page 1, RESET STEM COLORS for the stem colours, and RESET 3-BAND COLOURS for the 3-band colours.

> **Note.** Deleting the settings file from the USB does not reset anything. The player keeps the settings it has, and saves them to the USB again with your next change.

## Every setting

Each row names the chapter that explains the setting.
