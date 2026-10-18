# Plan domain: product and architecture

[Back to the chart editor plan index](../chart-editor-plan.md).

Status: reviewed implementation plan. This domain owns product goals, architecture, dependency policy, and rendering direction. Other requirements are linked from the stable plan index rather than duplicated here.

Ongoing milestone state and resumption notes live in the [`memory/chart-editor-status.md`](../../memory/chart-editor-status.md) index. Update the owning status slice after meaningful implementation or planning changes.

## Goals

The editor should open, edit, preview, validate, and save charts in these format families:

- BMS: `.bms`, `.bme`, `.bml`, and `.pms`
- StepMania: `.sm` and `.ssc`
- osu!mania: mania-mode `.osu`
- BMSON: `.bmson`

Its first-class editing surface should include:

- note lanes, including holds and format-specific special lanes such as scratch and BGM/autoplay;
- dedicated BPM and scroll-velocity lanes;
- waveform previews for variable-length MP3 and Ogg music/keysound sources decoded as streams;
- optional labels attached to notes;
- user-defined semantic note layers whose colors identify musical sources or charting intent;
- non-destructive slicing of long audio sources, sufficient to author and round-trip BMSON sound channels.

Later authoring goals include an osu!-style storyboard (`.osb`) editor, the matrix-capable [OSBX Lua DSL](../osbx-draft-spec.md), Lua-powered chart/storyboard generation, and storyboard video export. These are deferred until the core chart, timing, audio, and format round trips are stable.

Quaver chart support is a late-stage interoperability goal. Keep its variable-key layouts, timing/scroll model, metadata, samples, and file organization in the adapter capability design, but do not displace the initial BMS, StepMania, osu!mania, and BMSON milestones. It becomes advertised support only when both its reader and writer meet the same semantic round-trip and negative-test gates.

Import alone is not considered format support. Every advertised BMS/BME/BML/PMS, SM, SSC, osu!mania, and BMSON profile must have both a reader and writer, an explicit semantic round-trip policy, validation rules, and a clear diagnostic for information that cannot be represented on export. A read-only adapter may exist only as an experimental capability and is not listed as supported.

Metadata coverage is symmetric and exhaustive within each advertised profile. Maintain a checked field inventory for every format/version that records spelling, song/chart/file scope, multiplicity, source order, duplicate/default behavior, normalized mapping, and writer capability. Every field accepted by a reader must be emitted by the paired writer, retained losslessly as format-specific source data, or produce an explicit policy-controlled loss diagnostic; it may never disappear merely because no common normalized field exists. Writers must likewise bound and validate every metadata field they emit. Unknown but syntactically valid fields remain source-addressable for stable saves, and edited/new documents receive deterministic ordering without falsely claiming byte-exact preservation.

## Initial scaffold (historical)

The following describes the original scaffold, not the current application. T-678 (2026-10-18) removed the unused legacy Chart and Field files. The current document/import pipeline is described in [the architecture](../architecture.md); `libotoworm` remains a dependency.

`src/common/Chart.h` contains a small normalized chart model. `src/common/chart.cpp` currently imports through `libotoworm`, converts charts to seconds, and copies notes, BPM, warps, stops, and interpolated speed multipliers. BMSON loading is routed through `libotoworm`, but its slice data, sound list, BGM events, measure lengths, source metadata, and most format-specific information are discarded during conversion. This dependency is temporary scaffold code. Wormedit will own its format codecs, timing model, and conversion pipeline; `libotoworm` is source and behavior reference only and should leave the application link once the replacement fixture coverage is in place.

The UI and audio layers are still scaffolds. `src/editor/field.cpp` is empty, and `src/sys/audio.cpp` only initializes SDL audio and validates that an import path is non-empty. There is no save path yet.

Before editor features are built, the internal document model needs to become loss-aware and suitable for editing. Extending the existing `ChartSegment` maps in place would make clean round trips difficult.

## Architecture and implementation style

Use a layered, data-oriented C++20 architecture. The code should feel like enhanced C: plain data structures, explicit ownership, small functions, and visible control flow, with modern C++ used for safety and expressive algorithms. Object orientation is available when it fits the data or boundary, but it is not the default organizing principle.

The initial dependency direction is:

```text
platform backends (SDL3 GPU/audio, decoders, filesystem, process/FFmpeg)
                         |
format codecs -------- core document/timing/audio data
       |                 |
conversion policies   commands + pure transformations
               \        /
             application controllers
                      |
     replaceable UI frontend adapter
```

Dependencies point toward core data and pure policy code. Views translate input into controller requests and render returned state; they do not parse formats, solve timing, run conversion, or own document mutations. Format codecs do not depend on UI or platform playback. Audio decoding, waveform analysis, transport, and rendering remain separate concerns even when a tab presents them together.

Treat Nuklear as the initial replaceable frontend. Application and tab controllers expose plain backend-neutral `*_view_model_t` values and accept typed `ui_action_t` requests; no document, timing, command, audio, format, or public controller header contains Nuklear types. A future wxWidgets, ImGui, or Clay-based frontend may compose widgets differently while consuming the same semantic state/actions; wxWidgets support is explicitly optional and is not a completion criterion for the current plan. Keep high-volume chart, BGM, timing, waveform, comparison, and storyboard surfaces on shared draw/hit-test data rendered through `render_backend_t`, so changing the panel toolkit does not rewrite or slow the chart renderer. See the [UI frontend boundary](../references/ui-backend-boundary.md).

After the stage-three authoring surfaces stabilize, split the growing Nuklear implementation into frontend-private functional units for workspace shell, Metadata, Timing, Compose, Audio/Slices, and shared widgets/input state. Preserve the current C boundary and backend-neutral view/action contracts, keep cross-pane gestures in application controllers rather than duplicating them in those units, and land the extraction in behavior-preserving slices with the existing architecture checks and UI smoke coverage.

Project code uses `snake_case` for namespaces, functions, methods, variables, files, and project-defined type names. Project-defined types use a descriptive `<type>_t` suffix, such as `song_document_t`, `warp_event_t`, and `conversion_request_t`. Prefer scoped `enum class` declarations, also named with `_t`, so values do not leak into enclosing scopes. Third-party and standard-library names keep their native spelling.

Prefer:

- aggregates and narrow value types for IDs, exact positions, durations, lanes, and format provenance;
- methods on structs when an invariant or operation belongs naturally to that one data abstraction, without turning every record into a getter/setter shell;
- `std::optional` for honest absence, `std::variant` for closed tagged alternatives, and a project `result_t<T, error_t>` for expected failure while the project remains on C++20;
- monadic `transform`/`and_then`/`or_else` style over `result_t` and optional values when it makes success and failure flow easier to follow, with exhaustive visitors for closed variants and scoped enums;
- RAII containers and handles, `std::span`/`std::string_view` for non-owning parameters, and `std::unique_ptr` only where ownership must be indirect;
- namespace-scoped free functions for transformations that combine subsystems, including parsing, validation, conversion, timing evaluation, and document commands;
- C++ ranges and algorithms when they make data flow clearer, with named intermediate values when a long lazy pipeline would obscure lifetime, ordering, or failure behavior;
- explicit immutable input/mutable output parameters or returned values for transformations, so tests can exercise each layer without a window, audio device, or global singleton.

Avoid broad manager classes, inheritance-based event trees, service locators, and exceptions crossing C-library or process boundaries. Translate SDL, decoder, Lua, iconv, and FFmpeg failures into typed diagnostics at their adapters. Runtime polymorphism is appropriate for genuinely open backends such as audio decoders or video encoders; closed format and timing choices should usually be scoped enums plus tables, callables, or `std::variant` visitors.

The requested strategy pattern follows this rule. A `conversion_request_t` owns small policy values such as target profile, warp encoding, branch projection, quantization, and loss handling. The conversion pipeline dispatches through explicit strategy tables or free functions. It should not require one class hierarchy per setting; an interface object or virtual backend is used only when implementations must be selected dynamically across a stable runtime boundary.

Representative style:

```cpp
enum class warp_encoding_t { native, negative_bpm, negative_stop };

struct conversion_request_t {
    format_profile_t target;
    warp_encoding_t warp_encoding;
    std::optional<difficulty_id_t> difficulty;
};

auto convert(const song_document_t& song, const conversion_request_t& request)
    -> result_t<conversion_output_t, diagnostic_list_t>;

return decode_song(input)
    .and_then(normalize_timing)
    .and_then([&](auto&& song) { return convert(song, request); })
    .and_then(publish_atomically);
```

This is a style example rather than a fixed API. Pipelines may use struct methods, free functions, ranges, or named imperative steps according to which form keeps ownership, sequencing, and failure behavior clearest.

## External dependencies and CMake policy

Use maintained cross-platform libraries for established container/codec/parser/runtime work. Wormedit should own the semantic adapters and safety policies around them, but it should not reimplement Lua, Standard MIDI File parsing, Ogg/Vorbis decoding, FLAC decoding, or video encoding.

Prefer the C++ standard library whenever it satisfies the requirement. Boost is permissible for a focused component that supplies missing portability, a mature algorithm, or a material correctness benefit, but record why that component is needed and link it only to the owning layer. Keep Boost types behind wormedit-owned APIs instead of making Boost the default vocabulary of the core document. Re-evaluate each use as the selected C++ standard gains an adequate facility.

Every dependency is exposed through a narrow imported/alias CMake target and linked only to the layer that needs it. Prefer a caller-supplied package or source directory, then a pinned `FetchContent` fallback. Never track an unpinned `main`/`master` for a release build. Disable dependency examples, tools, and tests unless wormedit needs them, record license/version data for packaging, and make offline builds possible from a populated dependency cache.

Initial dependency direction:

- **JSON:** use pinned nlohmann/json 3.12.0 behind `nlohmann_json::nlohmann_json` for BMSON and future native-project parsing. Enable diagnostic byte positions so wormedit can retain exact source spellings while owning semantic validation, size/depth/value limits, path safety, and typed diagnostics.
- **Lua:** adapt raindrop's [`FindLua.cmake`](../../../raindrop/cmake/Modules/FindLua.cmake), which currently builds pinned Lua 5.4.8 `onelua.c` as `Lua::Lua`. Keep the resulting module/target in wormedit so installed and standalone builds do not depend on a sibling raindrop checkout or build directory.
- **MIDI files:** select a dedicated Standard MIDI File library rather than writing a byte parser. [Craig Sapp's `midifile`](https://github.com/craigsapp/midifile) is the leading small-library candidate; stage 0 must test exact ticks, tempo/meter/meta events, running status, malformed bounds, read/write needs, license, CMake consumption, and Windows/macOS/Linux behavior before pinning it. Real-time MIDI device I/O is a separate later dependency and must not drive this choice.
- **Ogg/Vorbis:** add pinned libogg/libvorbis targets, normalized for example as `Ogg::ogg`, `Vorbis::vorbis`, and `Vorbis::vorbisfile`, or use another selected decoder only if it satisfies the same cross-platform seek/sample-frame fixtures. Raindrop's [`FindOggVorbis.cmake`](../../../raindrop/cmake/Modules/FindOggVorbis.cmake) is reference integration.
- **WAV/FLAC:** evaluate libsndfile versus focused WAV/libFLAC backends for keysound decode, seek, package size, and licensing. FLAC decode is required for BMS fallback even if full-song FLAC authoring remains deferred.
- **MP3:** retain a replaceable decoder target and choose it through the streamed-audio spike; it must provide bounded-memory seek behavior on all three desktop targets.
- **Static images:** use dual-public-domain/MIT `stb_image` 2.30, pinned at commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`, behind the wormedit-owned `wormedit::visual_decoder` target for bounded PNG/JPEG/BMP and compatible static-image decode to RGBA8. Keep resource/path resolution, byte/dimension/pixel limits, declared-geometry checks, caching, renderer upload, and diagnostics in wormedit-owned layers; caller-supplied pinned sources must keep offline builds possible. Animated image, video, storyboard evaluation, and GPU texture ownership remain separate capabilities.
- **Catch2:** use the pinned test-only Catch2 v3 target and CTest discovery described below.
- **FFmpeg:** keep video frame decoding and later encode/mux behind dedicated optional targets/process adapters so chart-only builds do not acquire the dependency. Decoder adapters consume the exact player-preview media clock and publish bounded renderer-neutral frames; live delivery remains asynchronous and generation-checked.
- **UI frontend:** begin with pinned Nuklear behind a private frontend target. Select exactly one configured frontend and keep room for ImGui or a Clay-based frontend; core/headless targets never resolve a UI library.

`libotoworm` is deliberately absent from this dependency list. Its parsers, conversions, and edge-case comments may inform fixtures and expected compatibility, but wormedit code does not call into it after the replacement stage. Do not copy its transient seconds-based model into the new core.

### SDL3 GPU rendering

Use SDL3's GPU API as the primary rendering backend and remove the production OpenGL dependency once the replacement is functional. The current `SDL_GLContext`, OpenGL CMake package, and `nuklear_sdl_gl3_sdl3.h` are scaffold constraints, not the intended renderer.

Create a small `render_backend_t` boundary that owns the SDL GPU device, window/swapchain claim, command buffers, transfer buffers, pipelines, samplers, textures, and deferred destruction. Implement a Nuklear adapter that converts its vertex/index/clip command stream into SDL GPU uploads and draw passes. Chart/waveform/storyboard renderers produce backend-neutral draw data and never call SDL GPU functions directly.

Storyboard preview and offline rendering use a reusable dynamically sized frame arena for evaluated draw instances, transformed vertices, temporary command ranges, and hit-test records. Authored storyboard nodes, stable IDs, assets, command trees, and compiled event schedules remain durable data outside the arena. The arena grows by checked chunks up to configured byte/object limits, resets or recycles storage at a frame/evaluation-window boundary, and reports capacity exhaustion instead of falling back to unbounded per-object heap allocation. Compiled storyboard events use pre-sized contiguous storage sorted by exact time/source order; seek binary-searches the first relevant event and sequential evaluation advances a head handle/index without erasing, relinking, or allocating queue nodes. Preview, captured-frame tests, `.osb` emission validation, and video rendering consume the same deterministic evaluator and arena-backed draw list.

Run rendering preparation, waveform analysis, parsing/index construction, and heavy exports through a project-owned bounded continuation scheduler rather than blocking the UI thread or tying features to `std::async`. A task returns a typed result and may publish dependent continuations when its prerequisites complete; failures and `std::stop_token` cancellation propagate through the graph without scheduling invalid dependents. Worker queues, dependency nodes, outstanding bytes/tasks, fan-out, and per-document generations are capped. UI publication is a final generation-checked continuation, while file replacement remains one transactional terminal step. Implement the first backend with C++20 `std::jthread`, stop tokens, atomics, condition variables, and worker-local queues; keep its API backend-neutral so a measured work-stealing library can replace the executor if the standard-only spike cannot provide Rayon-style parallel throughput and predictable cancellation on all three platforms. Do not expose `std::future`, worker threads, or scheduler locks in core document/format APIs.

Ship or generate the shader variants SDL GPU needs through a reproducible build step; evaluate SDL_shadercross for development/build-time compilation while packaging precompiled supported formats where practical. Device and shader creation return typed diagnostics. Startup should select SDL GPU's available platform backend automatically, report the chosen driver, and fail clearly or enter a deliberately implemented SDL software fallback when no supported GPU path exists. Do not silently return to the old OpenGL backend.

Keep renderer tests split between backend-neutral draw-list/snapshot tests and small SDL GPU smoke tests for device creation, texture upload, clipping, high-DPI scaling, resize/swapchain recovery, and one captured frame on supported CI runners. GPU absence should be an explicit skipped capability in headless jobs, not a false product-test pass.

The current scaffold's direct `nk_*` calls in `main.cpp` and public `nk_context*` lifecycle are temporary. Before building substantial panels, move them behind `src/ui/nuklear` and make the executable depend only on frontend lifecycle/input/action APIs. CMake should eventually expose a checked `WORMEDIT_UI_BACKEND=nuklear|imgui|clay` selection whose chosen adapter alone links that third-party library.
