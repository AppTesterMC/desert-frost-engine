"""Judge a captured audio run.

Usage: dune_audio_level.py IN.raw OUT.wav LABEL

Reads raw 16-bit stereo 44100 Hz audio (what SDL's disk driver writes when the
engine runs with SDL_AUDIODRIVER=disk), writes it as a playable WAV, and prints
one verdict line: how many seconds carried sound and the peak level. Two loud
seconds are enough to say the engine is not silent; listen to the WAV when a
verdict looks wrong.
"""

import array
import math
import struct
import sys

RATE = 44100
THRESHOLD = 200  # RMS below this is silence or a faint tail.


def main(raw_path, wav_path, label):
    data = open(raw_path, 'rb').read()
    samples = array.array('h')
    samples.frombytes(data[:len(data) // 2 * 2])

    levels = []
    for start in range(0, len(samples), RATE * 2):  # one second, stereo
        block = samples[start:start + RATE * 2:7]   # every 7th sample is plenty
        if block:
            levels.append(int(math.sqrt(sum(x * x for x in block) / len(block))))

    with open(wav_path, 'wb') as wav:
        wav.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                  struct.pack('<IHHIIHH', 16, 1, 2, RATE, RATE * 4, 4, 16) +
                  b'data' + struct.pack('<I', len(data)) + data)

    loud = [level for level in levels if level > THRESHOLD]
    verdict = 'OK' if len(loud) >= 2 else 'SILENT - FIX BEFORE BUILDING THE IPA'
    print('%s audio: %s (%d/%d s with sound, peak %d)'
          % (label, verdict, len(loud), len(levels), max(levels or [0])))
    return 0 if len(loud) >= 2 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3]))
