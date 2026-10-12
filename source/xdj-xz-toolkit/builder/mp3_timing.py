"""Read the LAME tag of an MP3 without decoding it.

Players decode every MP3 frame. A gapless decoder (ffmpeg, libsndfile, iTunes)
removes the encoder delay plus its own 529-frame decoder delay at the start and
the encoder padding at the end, so audio from such a decoder starts
``encoder_delay + 529`` frames earlier than the player's full mix. The helper
keeps those frames; this probe lets the library classify bundles that were
written before it did, from the first few kilobytes of the source file.

The numbers mirror dr_mp3's parsing: ``delay_frames`` is what a gapless decoder
skips at the start, ``padding_frames`` what it drops at the end.
"""
from pathlib import Path

DECODER_DELAY = 529
BITRATES = {
    (1, 3): (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320),
    (2, 3): (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160),
}
RATES = {3: (44100, 48000, 32000), 2: (22050, 24000, 16000), 0: (11025, 12000, 8000)}
PROBE_BYTES = 64 * 1024


def _frame(data, at):
    """Header fields of a layer III frame at ``at``, or None."""
    if at + 4 > len(data) or data[at] != 0xFF or data[at + 1] & 0xE0 != 0xE0:
        return None
    version_bits = (data[at + 1] >> 3) & 3
    layer_bits = (data[at + 1] >> 1) & 3
    if version_bits == 1 or layer_bits != 1:
        return None
    mpeg = 1 if version_bits == 3 else 2
    bitrate_index, rate_index = data[at + 2] >> 4, (data[at + 2] >> 2) & 3
    if bitrate_index in (0, 15) or rate_index == 3:
        return None
    bitrate = BITRATES[(mpeg, 3)][bitrate_index] * 1000
    rate = RATES[version_bits][rate_index]
    padding = (data[at + 2] >> 1) & 1
    mono = (data[at + 3] >> 6) == 3
    samples = 1152 if mpeg == 1 else 576
    length = samples * bitrate // 8 // rate + padding
    side = (17 if mono else 32) if mpeg == 1 else (9 if mono else 17)
    protected = not (data[at + 1] & 1)
    return {'length': length, 'rate': rate, 'side': side + (2 if protected else 0)}


def _first_frame(data):
    at = 0
    limit = min(len(data) - 4, 16 * 1024)
    while at <= limit:
        frame = _frame(data, at)
        if frame and _frame(data, at + frame['length']):
            return at, frame
        at += 1
    return None, None


def probe(path):
    """``{'lame_tag', 'encoder_delay', 'encoder_padding', 'delay_frames', 'padding_frames', 'sample_rate'}`` or None."""
    with Path(path).open('rb') as source:
        head = source.read(10)
        if head[:3] == b'ID3' and len(head) == 10:
            size = head[6] << 21 | head[7] << 14 | head[8] << 7 | head[9]
            source.seek(10 + size + (10 if head[5] & 0x10 else 0))
            data = source.read(PROBE_BYTES)
        else:
            data = head + source.read(PROBE_BYTES - len(head))
    at, frame = _first_frame(data)
    if frame is None:
        return None
    tag = at + 4 + frame['side']
    result = {'lame_tag': False, 'encoder_delay': 0, 'encoder_padding': 0,
              'delay_frames': 0, 'padding_frames': 0, 'sample_rate': frame['rate']}
    if data[tag:tag + 4] not in (b'Xing', b'Info'):
        return result
    flags = data[tag + 7]
    lame = tag + 8 + (4 if flags & 1 else 0) + (4 if flags & 2 else 0) + (100 if flags & 4 else 0) + (4 if flags & 8 else 0)
    if lame + 24 > len(data) or not data[lame]:
        return result
    delay = data[lame + 21] << 4 | data[lame + 22] >> 4
    padding = (data[lame + 22] & 0xF) << 8 | data[lame + 23]
    result.update(lame_tag=True, encoder_delay=delay, encoder_padding=padding,
                  delay_frames=delay + DECODER_DELAY, padding_frames=max(0, padding - DECODER_DELAY))
    return result


def latency_pad_frames(mp3, source_rate, target_rate=96000):
    """Frames of encoder-side latency at the start of the stored roles, on the 96 kHz timeline."""
    if not mp3 or not mp3.get('lame_tag'):
        return 0
    return round(int(mp3['delay_frames']) * target_rate / int(source_rate))
