#!/usr/bin/env python3
"""Generate wormedit's original CC0 default hitsounds, deterministically."""
import math
from pathlib import Path
import random
import struct
import wave


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    rate = 44100
    for set_index, sample_set in enumerate(("normal", "soft", "drum")):
        for kind_index, kind in enumerate(("hitnormal", "hitwhistle", "hitfinish", "hitclap")):
            rng = random.Random(100 + set_index * 4 + kind_index)
            duration = (0.065, 0.14, 0.24, 0.11)[kind_index]
            frequency = (1700, 2400, 830, 900)[kind_index] * (1, 0.8, 0.6)[set_index]
            pcm = bytearray()
            for frame in range(int(rate * duration)):
                t = frame / rate
                envelope = min(1, t * 1500) * math.exp(-t * (8 / duration))
                noise = rng.uniform(-1, 1)
                tone = math.sin(2 * math.pi * frequency * t)
                signal = (0.8 * noise + 0.2 * tone) if kind_index in (2, 3) else tone
                value = int(18000 * envelope * signal)
                pcm.extend(struct.pack("<hh", value, value))
            with wave.open(str(directory / f"{sample_set}-{kind}.wav"), "wb") as output:
                output.setparams((2, 2, rate, 0, "NONE", "not compressed"))
                output.writeframes(pcm)


if __name__ == "__main__":
    generate(Path(__file__).resolve().parents[1] / "assets" / "hitsounds")
