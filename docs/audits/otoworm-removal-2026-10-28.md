# Removing the historical otoworm dependency

Scope: post-1.0 dependency cleanup, based on `db4528768c5cb8fc0ed6f60aa0b6c026ba5ae899`.
This changes build and distribution inputs, not chart semantics or the native schema.

## Consumers and compatibility

T-678 (`3f069f73`) removed the unused `src/common/chart.cpp` bridge. Searches of
first-party source, public headers and tests find no remaining calls to the
`otoworm`/`NoteLoader*` APIs or includes of its public headers. Application and
frontend event-harness CMake links were the remaining consumers. The core,
`wormedit-convert`, project opening and conversion reimport use wormedit's codecs
for BMS/BME/BML/PMS, BMSON, SM/SSC, osu!mania and native projects.

The pinned upstream revision was `28c4a64735f539d2b0da11b65d6e869e48615c4b`.
Its FTB/OJN loaders, NPS and conversion utilities have no current application
route to migrate. The old capability audit remains historical fixture research;
it is not a description of current import behavior or a requirement to reproduce
upstream's lossy seconds-based projection.

## Source and notice review

Reviewed the pinned upstream `src`, `include` and `src-thirdparty` against current
first-party `src`/`include`, including the local SHA-256 checksum implementation,
BMS base-36 helpers and format codecs. No remaining upstream source files or API
consumers were identified. A comment/whitespace-insensitive comparison of 50-token
windows found only repeated zero literals in generated icon pixels versus the
embedded JSON header. The local checksum is a separate function rather than
upstream's Stephan Brumme `SHA256` class. This is a bounded source review, not a
proof against every possible renamed or rewritten derivation.

Remove the upstream-specific minizip, utf8, embedded JSON and SHA-256 install
inputs together with the submodule and its MIT/Stephan Brumme notice files.
Current nlohmann/json retains its separate MIT notice. Other dependencies'
notices, including SDL, miniaudio, FFmpeg and the AppImage runtime, remain.

The core still discovers and links iconv for legacy encodings outside Windows;
Windows keeps its NLS code-page implementation. The removed Windows zlib source
provider had no consumer except otoworm. Available native system zlib notices and explicit `WORMEDIT_ZLIB_NOTICE_FILE`
overrides are retained. Missing zlib notices require an override only when a
direct `ZLIB::ZLIB` target exists.
FFmpeg SDK and AppImage runtime notices remain independent; removing otoworm does
not assert that every packaged binary is zlib-free.

## Build and verification contract

Fresh application and event-harness builds must succeed with no submodules,
without `WORMEDIT_OTOWORM_SOURCE_DIR`, and without otoworm headers or archives in
compile/link commands. Run `scripts/gate.sh build --jobs 4` and inspect discovery
failures as failures. Relevant existing suites cover format readers/writers,
encoding, conversion, native migrations and frontend event workflows.

Install the `Wormedit` component to a new staging directory and inspect assets,
notices and runtime dependencies. Test an explicit zlib SDK notice separately.
The pinned AppImage tooling and runtime notices remain unchanged; this cleanup
only removes its submodule preflight. CMake's project minimum remains 3.25;
the otoworm-specific 4.2 requirement is removed. Packaging may continue using its
existing pinned 4.2.3 toolchain.

Native macOS bundle construction/signing and Windows application packaging need
their respective host/toolchains and SDKs. Linux success alone does not establish
those results. No release artifacts are published by this change.

## Verification results

- Fresh Linux Release application/event-harness configuration: CMake 4.2.3,
  GCC 14.2, Lua/audio/image/video enabled; manual generation disabled. No
  `WORMEDIT_OTOWORM_SOURCE_DIR` or submodule is present. Compile-command and
  application/harness link-graph inspection finds no otoworm inputs; the BMS
  compiler command still defines `WORMEDIT_HAS_ICONV=1`.
- Full application configuration also succeeds with CMake 3.25.2. Headless
  preset configuration succeeds separately without SDL or submodules.
- Explicit `WORMEDIT_ZLIB_NOTICE_FILE=/usr/share/doc/zlib1g/copyright` is retained
  in the generated full application install plan. The host FFmpeg links `libz`.
- The Xiph release download host returns HTTP 403 in this environment; local
  source overrides use upstream GitHub tags v1.3.6 (Ogg,
  `be05b13e98b048f0b5a0f5fa8ce514d56db5f822`) and v1.3.7 (Vorbis,
  `0657aee69dec8508a0011f47f3b69d7538e9d262`). Other dependency sources reuse the
  prepared pinned checkouts, not their existing object files or binaries.
- AppImage packaging reaches the pinned Docker image, then fails during apt:
  container DNS cannot resolve `archive.ubuntu.com` or `security.ubuntu.com`.
  No AppImage was produced. Its runtime zlib notices and tooling pins are
  unchanged.
- Windows packaging stops at its existing preflight because
  `x86_64-w64-mingw32-g++` is unavailable. The native macOS script stops at its
  existing real-Mac requirement. Neither platform's package is claimed tested.

- CLI smoke: the freshly built `wormedit-convert` imports
  `tests/fixtures/stepmania/matched-timing.ssc`, writes a native project and
  reports successful reimport validation, with no diagnostics.

Full compilation, full test and installed-application results are pending execution.
