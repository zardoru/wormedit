# wormedit

**A fast, cross-platform rhythm chart editor for people who author BMS, StepMania, osu!mania and BMSON charts.**

Wormedit is built around one idea: editing a chart should feel as immediate as playing it. The editor is a native C++20 application on SDL3's GPU renderer, with an exact (rational) timing model underneath and a headless core that is tested independently of any UI.

> **Status:** early and incomplete (version 0.1.0). The editor is usable in parts but far from finished, and some features described below are still being built or verified. Progress is tracked as task cards in [`docs/chart-editor-plan/tasks/`](docs/chart-editor-plan/tasks/). Storyboards, Lua generation and Quaver support are deferred and not shipped.

## Why wormedit

Wormedit comes from a long-standing, concrete need: charting across several rhythm games without relearning a different tool for each one. Its requirements were gathered and refined over years by someone who actually edits charts, and every feature below traces back to that workflow rather than to a checklist of formats.

- **One editor, several formats.** Open, edit, preview, validate and save BMS-family charts (`.bms`, `.bme`, `.bml`, `.pms`), StepMania (`.sm`, `.ssc`), osu!mania (`.osu`) and BMSON (`.bmson`). Charts live in an editor-owned document model, so a chart is not tied to the quirks of one file format.
- **Note layers for charting by ear.** Define your own semantic layers with their own colors (vocals, kick, a synth line, a hunch you want to revisit) and tag notes with them. If you chart in a sample-bound style, you record *why* a note is there, and you can hand that intent to collaborators in the chart itself. Optional labels can be attached to individual notes as well.
- **Exact timing.** Positions are rational numbers, not floats, and the editor models BPM changes, scroll velocity, stops/pauses and warps in dedicated timing lanes. Tap tempo, a metronome and estimated-grid clicks help you find the BPM and offset of a song.
- **Built to be snappy.** Performance is a design goal, not an afterthought: the canvas draws through a dedicated batched GPU renderer (a handful of draw calls for the whole field), scenes and minimap textures are cached and reused between frames, waveform and audio work runs on worker threads, and playhead timing is lock-free. Frame time is instrumented and measured in isolated runs (during playback, workspace construction measured about 1.8 ms and CPU work outside rendering about 3.3 ms).
- **Hear while you edit.** Streaming MP3/Ogg playback, waveform previews, per-note audition, and adjustable playback rate from 0.1x to 1.0x with or without pitch change (WSOLA time-stretch).
- **A canvas that gets out of your way.** The Compose view supports range selection with Shift, Space to play/stop, a minimap with click-to-seek, undoable cut/copy/paste with ghost previews, and per-difficulty lane profiles with instrument-aware colors.
- **Virtual canvas.** Charts without a BGM track get a virtual measure range (64 measures by default), so you can start charting before you have final audio.
- **BMS depth.** BMS branches and BGM columns, encoding handling for legacy Shift-JIS files, and a decision to export UTF-8 for beatoraja.
- **Safe by default.** Atomic file writes, autosave and a recovery controller, plus validation that names the chart, measure and object when a file is invalid.
- **Multi-difficulty projects.** A native project format holds several charts, with a Project tab to create, duplicate, reorder and rate difficulties.
- **Tested.** Over a thousand automated tests cover the core, formats, audio and frontend state, and the real window is verified in an isolated virtual X server.

## Supported formats

All four families have a reader and a writer, and conversion between them is loss-aware: when a target cannot represent something, the result says so instead of silently dropping it.

| Format | Files | Highlights |
| --- | --- | --- |
| BMS family | `.bms` `.bme` `.bml` `.pms` | UTF-8 and legacy encodings (with explicit overrides), original bytes and unknown metadata preserved, `RANDOM`/`IF`/`SWITCH` branches, exact measure lengths, BGM lanes, automatic lane-profile detection (BME 7K/14K, PMS 5/9-button) |
| StepMania | `.sm` `.ssc` | Several difficulties per file, 3 to 10 column step types, taps, holds, rolls, mines, lifts, fakes, BPM, stops, delays, warps, scrolls and speeds; pinned StepMania 5.1 and Etterna profiles |
| osu!mania | `.osu` | 1K to 18K, hit objects in milliseconds mapped onto the exact timing grid, pinned format profiles |
| BMSON | `.bmson` | BMSON 1.0 with exact resolution, variable bar lines, sound-channel slices, BGM semantics and BGA events |
| Native project | wormedit project | Several charts in one project, plus semantic layers, labels, resources, bookmarks and retained source bytes |

## Build

Wormedit uses CMake (3.25+) with presets, a C++20 compiler and Ninja.

### Headless core and tests (no GUI, no GPU)

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
```

Dependencies (nlohmann/json, Catch2, Lua) are fetched with CMake `FetchContent` and pinned to exact versions. For offline builds, point to local checkouts:

```sh
cmake --preset headless \
  -DWORMEDIT_NLOHMANN_JSON_SOURCE_DIR=/path/to/nlohmann-json-3.12.0 \
  -DWORMEDIT_CATCH2_SOURCE_DIR=/path/to/Catch2 \
  -DWORMEDIT_LUA_SOURCE_DIR=/path/to/lua-5.4.8
```

### The application

The GUI build (`WORMEDIT_BUILD_APP=ON`, the default for a plain configure) also needs:

- **[`libotoworm`](https://github.com/zardoru/libotoworm)** checked out as a sibling directory, at `../libotoworm`.
- SDL3 and Nuklear, fetched automatically by CMake (or point `SDL3_SOURCE_DIR` at a local SDL3 checkout).
- Optional: FFmpeg development libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, found through `pkg-config`) enable the bounded video-frame decoder. Without them the editor builds without video previews.

```sh
git clone https://github.com/zardoru/libotoworm.git
git clone https://github.com/zardoru/wormedit.git
cd wormedit
cmake -S . -B build -G Ninja
cmake --build build
./build/wormedit
```

Run the whole suite with `ctest --test-dir build --output-on-failure`, or a subset by prefix, e.g. `-R '^core\.'`.

More detail, including cross-compiling the headless core for Linux and Windows and reading crash backtraces, is in [`docs/building.md`](docs/building.md).

## Using the editor

1. **Start** from the landing screen: open an existing chart or use the wizard to create a new one or import from another format.
2. **Compose** notes on the canvas. Select with the pointer, extend a range with Shift, and arm note kinds from the toolbar.
3. **Play** with Space (or Ctrl+Space from the cursor), slow the rate down for tricky sections, and use the minimap to jump around.
4. **Time** the song in the Timing tab: BPM, scroll velocity, stops, and the tap-tempo assistant.
5. **Manage** metadata, resources and difficulties from the Project tab and the Sounds/Images/Measures panels.
6. **Save.** Saves are atomic and autosaved sessions can be recovered after a crash.

Keyboard shortcuts are profile-based and configurable in the Options window.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/core` | UI-free document model, timing, commands, editing controllers |
| `src/formats` | BMS, BMSON, StepMania, osu!mania and native project codecs |
| `src/audio` | decoding, streaming playback, waveforms, time-stretch |
| `src/visual` | image and video decoders for previews |
| `src/ui`, `src/editor`, `src/sys` | SDL3/Nuklear frontend and application shell |
| `tests/` | Catch2 tests and frontend state tests |
| `docs/` | plan, building guide, format references |

## Documentation

- [Chart editor plan](docs/chart-editor-plan.md): goals, architecture, stages and task cards
- [Building](docs/building.md)
- [Format and BMS references](docs/references/)

## AI disclosure

Wormedit is developed with heavy AI assistance. Most of the code, tests and documentation are written by AI coding agents (Claude and Codex) working from requirements, designs and reviews supplied and steered by the project's author. The author sets the direction, decides the trade-offs and accepts or rejects the results, but the bulk of the typing is not done by hand. If that matters to you, now you know.

## License

Released into the public domain under [The Unlicense](LICENSE). Do what you like with it. Bundled and fetched dependencies keep their own licenses (for example SDL3, Nuklear, Lua, Catch2, nlohmann/json and miniaudio).

## Credits

The SDL/OpenGL3 Nuklear backend in `src/vendor/nuklear_sdl_gl3_sdl3.h` is a minimal SDL3 port of Nuklear's upstream `demo/sdl_opengl3/nuklear_sdl_gl3.h` and retains the original public domain attribution header. [`libotoworm`](https://github.com/zardoru/libotoworm) (MIT) is a separate project that lives in its own directory next to this one; it is not part of this repository and keeps its own license.
