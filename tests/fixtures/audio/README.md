# Synthetic audio fixtures

These tiny stereo audio files are synthesized specifically for Wormedit's Stage 0 fixtures. They contain no recorded or sampled commercial audio. The checked-in assets are the outputs of [`generate.sh`](generate.sh); that script is the authoritative recipe, using FFmpeg's built-in `sine`, `aevalsrc`, and `anullsrc` sources. It emits both Ogg Vorbis and MP3 versions at 44.1 kHz. The original generation environment used FFmpeg 8.0.1 with its native Vorbis encoder and `libmp3lame` enabled. Encoded bytes can vary across FFmpeg/codec versions; tests therefore check file presence, minimum size, and container/stream signatures, not decoded sample values.

| Asset pair | Fixture purpose |
| --- | --- |
| `keysound-c4.ogg`, `keysound-c4.mp3`; `keysound-e4.ogg`, `keysound-e4.mp3`; `keysound-g4.ogg`, `keysound-g4.mp3` | Short, distinct BMS-style keysound resources suitable for notes triggered simultaneously. |
| `polyphonic-chord.ogg`, `polyphonic-chord.mp3` | One short source containing simultaneous C4/E4/G4 tones, for polyphonic sample handling. |
| `sliced-seek-source.ogg`, `sliced-seek-source.mp3` | Twelve-second seek/stream source with a short tone in each two-second region and silence between regions. Regions rise through 220, 277, 330, 392, 440, and 523 Hz. This offers repeatable slice/seek locations without requiring a decoder-level test oracle. |
| `silence.ogg`, `silence.mp3` | One second of generated digital silence for empty/quiet resource cases. |

The short individual notes model a simultaneous three-key BMS/BMSON resource set. The long source is a BMSON-style sliced-resource stand-in; its approximate source windows begin at 0, 2, 4, 6, 8, and 10 seconds. Tests intentionally do not assert codec timing, silence thresholds, audible quality, or sample-accurate seeking.

To regenerate in this directory, run `./generate.sh`. The script overwrites only the twelve named audio outputs in this directory.

## Future decoder and transport assertions

The inventory test is deliberately limited to file presence, a minimum byte count, and Ogg/MP3 signatures. Once the cross-platform decoder and mixer exist, use these same fixtures for behavioral tests with a fake sample-frame clock and mixer:

- Decode sequentially at 44.1 kHz stereo and finish each file within 60 ms of its authored duration. The expected source durations are 0.22 s for each short key, 0.50 s for the chord, 12.0 s for the seek source, and 1.0 s for silence; the tolerance accommodates codec priming/padding.
- Keep streamed PCM in 16,384-frame decode blocks, return no more than one block per decode request, and cap the per-asset decoded-PCM LRU at four blocks. For these stereo float32 fixtures that is at most 65,536 frames / 512 KiB of decoded PCM per asset, in addition to fixed decoder state and the compressed source. Sequential playback and repeated random seeks must not accumulate older PCM blocks beyond that cap. Re-requesting a still-cached block must report a cache hit and return identical samples.
- Seek into `sliced-seek-source` at 0.20, 2.20, 4.20, 6.20, 8.20, and 10.20 seconds. A 250 ms analysis window centered on each seek point should find dominant frequencies 220, 277, 330, 392, 440, and 523 Hz respectively, within ±5 Hz, with RMS between -24 and -12 dBFS. Windows centered at 1, 3, 5, 7, 9, and 11 seconds should stay below -45 dBFS. These offsets leave margin around each 400 ms tone and the two-second boundaries.
- Complete waveform generation for the 12-second source, including bins over all six tone regions and the intervening quiet regions. Repeating a waveform request at the same resolution after cache fill must return identical per-channel peak bins and reuse cached analysis; seeking or decoding another region must not change earlier bins.
- Schedule `keysound-c4`, `keysound-e4`, and `keysound-g4` at the same sample frame. All three voices must start together and remain independently represented in the mix; same-position BMS keysounds and BMSON layers must not replace one another. The `polyphonic-chord` pair separately checks that a single decoded source can itself contain concurrent frequencies.
- Treat Stop as a hard cancellation boundary. Stopping with music, BGM, playable-note, and click-preview voices active must clear scheduled/decoded queues and request all voice stops; the very next mix callback must contain only exact digital silence. Complete a decode job tagged with the stopped session after that callback and assert that it is discarded and cannot restart or enqueue audio.

The numeric block/cache limits are the intended streamed-audio acceptance contract, not behavior asserted by today's container-level inventory test. Keep waveform memory separate from decoded PCM accounting because waveform peak levels are cached analysis data rather than decoded playback blocks.
