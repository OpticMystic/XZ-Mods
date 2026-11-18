# xz_sync_verify.py: Pro DJ Link and Ableton Link, checked against stock

One Python file, standard library only (Python 3.9 or newer, Windows, macOS or Linux). Run it on a PC on the same
LAN as the XDJ-XZ. It prints a table of PASS, FAIL, SKIP and INFO rows and saves the same as JSON. Exit status 1
when any row is FAIL.

The reference is the unit itself: decks 3 and 4 are judged against the same unit's stock decks 1 and 2 in the
same run ("as good as stock").

## What it sends

| Option | Packets it sends |
|---|---|
| (none) | Nothing. It listens to keep-alives (UDP 50000) and beat packets (UDP 50001), which every device broadcasts. |
| `--link` | Link pings, about 5 a second, unicast to the unit's Link helper, to measure the session clock. It listens to Link announcements (multicast 224.76.78.75:20808) but never announces itself, so it is not a peer and cannot change the session. |
| `--announce` | Joins Pro DJ Link as a virtual player: claim packets, then a keep-alive every 1.5 s (broadcast), and a status packet every 200 ms to each device's address saying nothing is loaded. Players send status packets only to devices that announce themselves, so every status check needs this. The number is the lowest free one in 7-15 (`--device N` to choose). |
| `--handoff` | Asks the current tempo master for master, holds it, then waits up to 30 s for a deck to ask for it back (press MASTER on the deck; `--handback-deck N` says which deck must ask) and hands it over. While it holds master its status reports the old master's tempo, so synced decks do not change speed. |
| `--sync-command on\|off\|master` | One sync control packet to the unit's address. |
| `--metadata` | TCP to the unit's database server (port lookup on 12523, then the dbserver port) for the title of each deck's track. |

`--handoff`, `--sync-command` and `--metadata` need `--announce`. Never run the active options while the unit is
used for a performance: they change master and sync state.

Close rekordbox, PRO DJ LINK Bridge, Resolume and any other DJ Link software on the PC first. On Windows two
programs on UDP 50002 split the unicast status packets between them.

## Track titles stay out of the output

Track titles are never printed or saved unless `--show-titles` is given; `--metadata` records only whether a title
came back and its length. Track ids (rekordbox database numbers) and device addresses are saved, because they are
what the checks are about. The JSON file is written in the current folder (`--out` to choose); keep it with the
test record, not in a repository.

## Typical runs

```
python xz_sync_verify.py --unit 192.168.12.147 --seconds 60
python xz_sync_verify.py --unit 192.168.12.147 --seconds 90 --announce --link --expect-decks --expect-link
python xz_sync_verify.py --unit 192.168.12.147 --announce --guided 1,3
python xz_sync_verify.py --unit 192.168.12.147 --announce --handoff --handback-deck 3 --seconds 20
python xz_sync_verify.py --unit 192.168.12.147 --announce --sync-command on --seconds 10
python xz_sync_verify.py --unit 192.168.12.147 --replay capture.json      (a harness/passive_capture.py file)
python xz_sync_verify.py --self-test
```

`--guided DECKS` prints prompts every 12 s (`--guided-step-seconds`): load a track, play, move the tempo fader, press
MASTER, pause, for each deck named. It then reports when each change showed in the deck's status, how soon beat
packets started after PLAY and stopped after pause, and how soon the beat packets carried a new pitch. Run it for a
stock deck and a software deck (`--guided 1,3`) to compare them in one table.

## The checks

Devices (passive):
- every device the unit announces, with number, name, address, MAC and keep-alive interval;
- stock devices 1, 2 and 33 present; decks 3 and 4 present at the unit's address with the name XDJ-XZ
  (`--expect-decks` makes their absence a FAIL, not a SKIP); keep-alive cadence of 3/4 within 0.3 s of stock;
- no number defence packets.

Beat packets (passive), for each deck that plays: tempo (track BPM times pitch), timing jitter of each interval
against that tempo (p50, p99, max), missing beats, beat-in-bar sequence, and the next-beat field against
60000/BPM (it is stated at 0 % pitch). Decks 3/4 PASS when their p99 jitter is no worse than the worse stock deck's
(or 5 ms, whichever is larger), their beat-in-bar sequence has no error, and the next-beat field is within 2 ms.
Times are taken when packets reach the PC, so LAN jitter is in all numbers alike.

Status packets (`--announce`): rate, every field decoded, and for decks 3/4:
- every field the stock decks fill is filled (track source, rekordbox id, play state, flags, pitch, tempo, beat
  number, beat in bar, firmware, counters);
- rate within half to twice the stock rate;
- for every playing deck, the status agrees with its beat packets: tempo, play flag, beat counter growth, bar
  position 1-4, the tempo-valid marker of a rekordbox track;
- never two tempo masters at once; every change (load, play, pause, tempo, master, sync, on-air) with its time.

Ableton Link (`--link`):
- the unit's Link node seen; its clock measured by ping (offset spread under 2 ms);
- session tempo and how often the shown 0.01 BPM value changed;
- **discontinuities**: each new session timeline whose beat at the moment it arrived differs from the previous
  timeline's by more than 2 ms. FAIL above 0.5 per 10 s; the r29 failure was about 10 per 10 s. A seek, a loop
  jump off the bar, a new master or a new track each give one, and that is right;
- with a known master deck (from `--announce` status, or `--master-device N`): Link tempo within 0.01 BPM of the
  master's, and the Link bar phase against the master's beat packets (median within 10 ms; the spread includes the
  stock beat packets' own send jitter, about 16 ms p99 on stock deck 1 in the r29 capture).

## Lab test

`lab/run_verify_lab.py` (WSL Ubuntu, as root) runs the verifier against stand-ins in private network namespaces
with no route out: `stock_standin.py` for the stock player, the real `xz-djlink` helper and its bus stand-in for
decks 3/4, and for Link the real player half with the real `xz-link-peer`. 22 Pro DJ Link expectations (presence,
beats, status fields and agreement, both handoff directions, every guided step, sync control) and 4 Link ones
(clock, tempo, no jump while a master stands still at its track end, about one jump a second with the r29 policy).

```
python3 lab/run_verify_lab.py --out /tmp/xzv --djlink --link --old-policy <r29 link_policy.c>
```

It needs `python ../djlink/build.py` and `python ../linkpeer/build.py` first (host builds of the helpers).
