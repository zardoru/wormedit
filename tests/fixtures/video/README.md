# Video decoder fixtures

All three show red for the first second, then green, then blue, at 16x16. Regenerate them with
`generate.sh` using FFmpeg when a checked-in fixture needs to change.

| Fixture | What it exercises |
|---|---|
| `three-colors.mkv` | One frame per second, lossless FFV1: exact colors and frame intervals. |
| `three-colors-bframes.avi` | MPEG-4 with B-frames at 25 fps: the first decoded frame is timed after the stream start, so a clock at zero precedes every frame. |
| `three-colors-late-seek.mpg` | MPEG-1 program stream at 30 fps with a 0.53 s start time and seeded noise: FFmpeg's seek lands after the requested time (0.5 s lands at 0.8 s). Colors are noisy; compare channels rather than exact values. |
